#include "settings/settings.h"

#include <windows.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace srd::settings {

namespace {

std::wstring host_ini_path()
{
    return app_data_directory() + L"\\host.ini";
}

std::string wide_to_utf8(const std::wstring& value)
{
    if (value.empty()) return {};

    const int size = ::WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);

    std::string out(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        out.data(), size, nullptr, nullptr);

    return out;
}

std::wstring utf8_to_wide(const std::string& value)
{
    if (value.empty()) return {};

    const int size = ::MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0);

    std::wstring out(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        out.data(), size);

    return out;
}

std::wstring hex_encode(const BYTE* data, DWORD size)
{
    static constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring out;
    out.resize(static_cast<std::size_t>(size) * 2);

    for (DWORD i = 0; i < size; ++i) {
        out[static_cast<std::size_t>(i) * 2] = digits[(data[i] >> 4) & 0x0f];
        out[static_cast<std::size_t>(i) * 2 + 1] = digits[data[i] & 0x0f];
    }
    return out;
}

int hex_value(wchar_t ch)
{
    if (ch >= L'0' && ch <= L'9') return ch - L'0';
    if (ch >= L'A' && ch <= L'F') return 10 + ch - L'A';
    if (ch >= L'a' && ch <= L'f') return 10 + ch - L'a';
    return -1;
}

std::vector<BYTE> hex_decode(const std::wstring& text)
{
    if (text.size() % 2 != 0) return {};

    std::vector<BYTE> out(text.size() / 2);

    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = hex_value(text[i * 2]);
        const int lo = hex_value(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return {};
        out[i] = static_cast<BYTE>((hi << 4) | lo);
    }

    return out;
}

std::wstring protect_password(const std::string& password)
{
    if (password.empty()) return {};

    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(
        const_cast<char*>(password.data()));
    input.cbData = static_cast<DWORD>(password.size());

    DATA_BLOB output{};

    if (!::CryptProtectData(
            &input,
            L"SimpleRemoteDesk Host Password",
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &output)) {
        throw std::runtime_error("CryptProtectData failed");
    }

    const std::wstring encoded = hex_encode(output.pbData, output.cbData);
    ::LocalFree(output.pbData);
    return encoded;
}

std::string unprotect_password(const std::wstring& encoded)
{
    auto encrypted = hex_decode(encoded);
    if (encrypted.empty()) return {};

    DATA_BLOB input{};
    input.pbData = encrypted.data();
    input.cbData = static_cast<DWORD>(encrypted.size());

    DATA_BLOB output{};

    if (!::CryptUnprotectData(
            &input,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &output)) {
        return {};
    }

    std::string password(
        reinterpret_cast<char*>(output.pbData),
        reinterpret_cast<char*>(output.pbData) + output.cbData);

    ::LocalFree(output.pbData);
    return password;
}

unsigned int read_uint(
    const wchar_t* section,
    const wchar_t* key,
    unsigned int fallback,
    const std::wstring& path)
{
    return ::GetPrivateProfileIntW(section, key, fallback, path.c_str());
}

bool read_bool(
    const wchar_t* section,
    const wchar_t* key,
    bool fallback,
    const std::wstring& path)
{
    return read_uint(section, key, fallback ? 1u : 0u, path) != 0;
}

void write_text(
    const wchar_t* section,
    const wchar_t* key,
    const std::wstring& value,
    const std::wstring& path)
{
    if (!::WritePrivateProfileStringW(
            section,
            key,
            value.c_str(),
            path.c_str())) {
        throw std::runtime_error("WritePrivateProfileString failed");
    }
}

} // namespace

std::wstring app_data_directory()
{
    PWSTR path = nullptr;

    if (FAILED(::SHGetKnownFolderPath(
            FOLDERID_RoamingAppData,
            KF_FLAG_CREATE,
            nullptr,
            &path))) {
        throw std::runtime_error("SHGetKnownFolderPath failed");
    }

    std::wstring result(path);
    ::CoTaskMemFree(path);

    result += L"\\SimpleRemoteDesk";
    std::filesystem::create_directories(result);
    return result;
}

std::wstring executable_path()
{
    std::wstring buffer(32768, L'\0');
    const DWORD length = ::GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));

    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("GetModuleFileName failed");
    }

    buffer.resize(length);
    return buffer;
}

HostSettings load_host_settings()
{
    HostSettings settings;
    const std::wstring path = host_ini_path();

    settings.port = static_cast<std::uint16_t>(std::clamp(
        read_uint(L"Host", L"Port", 45900, path),
        1024u,
        65531u));

    settings.fps = std::clamp(
        read_uint(L"Host", L"Fps", 30, path),
        1u,
        60u);

    settings.jpegQuality = std::clamp(
        read_uint(L"Host", L"JpegQuality", 90, path),
        40u,
        100u);

    settings.audioEnabled =
        read_bool(L"Host", L"AudioEnabled", true, path);

    wchar_t passwordBuffer[8192]{};
    ::GetPrivateProfileStringW(
        L"Host",
        L"Password",
        L"",
        passwordBuffer,
        static_cast<DWORD>(std::size(passwordBuffer)),
        path.c_str());

    settings.password = unprotect_password(passwordBuffer);

    if (settings.password.size() < 6) {
        settings.password = generate_password();
    }

    wchar_t hostIdBuffer[256]{};
    ::GetPrivateProfileStringW(
        L"Host", L"HostId", L"", hostIdBuffer,
        static_cast<DWORD>(std::size(hostIdBuffer)), path.c_str());

    settings.hostId = wide_to_utf8(hostIdBuffer);

    if (settings.hostId.empty()) {
        const std::string generated = generate_password(24);
        settings.hostId = generated;
    }

    settings.autostart = autostart_enabled();
    return settings;
}

void save_host_settings(const HostSettings& settings)
{
    const std::wstring path = host_ini_path();

    write_text(L"Host", L"Port", std::to_wstring(settings.port), path);
    write_text(L"Host", L"Fps", std::to_wstring(settings.fps), path);
    write_text(
        L"Host",
        L"JpegQuality",
        std::to_wstring(settings.jpegQuality),
        path);

    write_text(
        L"Host",
        L"AudioEnabled",
        settings.audioEnabled ? L"1" : L"0",
        path);

    write_text(
        L"Host",
        L"Password",
        protect_password(settings.password),
        path);

    write_text(
        L"Host",
        L"HostId",
        utf8_to_wide(settings.hostId),
        path);

    set_autostart(settings.autostart);
}

void set_autostart(bool enabled)
{
    HKEY key = nullptr;

    const LONG openResult = ::RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0,
        nullptr,
        0,
        KEY_SET_VALUE | KEY_QUERY_VALUE,
        nullptr,
        &key,
        nullptr);

    if (openResult != ERROR_SUCCESS) {
        throw std::runtime_error("Unable to open Windows Run registry key");
    }

    const wchar_t* valueName = L"SimpleRemoteDesk";

    if (enabled) {
        std::wstring command = L"\"" + executable_path() + L"\" --autostart";
        const DWORD bytes = static_cast<DWORD>(
            (command.size() + 1) * sizeof(wchar_t));

        const LONG result = ::RegSetValueExW(
            key,
            valueName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            bytes);

        ::RegCloseKey(key);

        if (result != ERROR_SUCCESS) {
            throw std::runtime_error("Unable to enable autostart");
        }
    } else {
        ::RegDeleteValueW(key, valueName);
        ::RegCloseKey(key);
    }
}

bool autostart_enabled()
{
    HKEY key = nullptr;

    if (::RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0,
            KEY_QUERY_VALUE,
            &key) != ERROR_SUCCESS) {
        return false;
    }

    const LONG result = ::RegQueryValueExW(
        key,
        L"SimpleRemoteDesk",
        nullptr,
        nullptr,
        nullptr,
        nullptr);

    ::RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

std::string generate_password(std::size_t length)
{
    static constexpr char alphabet[] =
        "ABCDEFGHJKLMNPQRSTUVWXYZ"
        "abcdefghijkmnopqrstuvwxyz"
        "23456789";

    if (length < 6) length = 6;

    std::vector<UCHAR> random(length);

    if (::BCryptGenRandom(
            nullptr,
            random.data(),
            static_cast<ULONG>(random.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        throw std::runtime_error("BCryptGenRandom failed");
    }

    std::string result;
    result.reserve(length);

    for (UCHAR value : random) {
        result.push_back(
            alphabet[value % (sizeof(alphabet) - 1)]);
    }

    return result;
}

} // namespace srd::settings
