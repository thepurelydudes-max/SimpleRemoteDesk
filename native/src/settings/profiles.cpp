#include "settings/profiles.h"

#include "settings/settings.h"

#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace srd::settings {

namespace {

std::wstring file_path()
{
    return app_data_directory() + L"\\viewer.ini";
}

std::wstring utf8_to_wide(const std::string& value)
{
    if (value.empty()) return {};

    const int chars = ::MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);

    std::wstring out(static_cast<std::size_t>(chars), L'\0');

    ::MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        out.data(), chars);

    return out;
}

std::string wide_to_utf8(const std::wstring& value)
{
    if (value.empty()) return {};

    const int bytes = ::WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);

    std::string out(static_cast<std::size_t>(bytes), '\0');

    ::WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        out.data(), bytes, nullptr, nullptr);

    return out;
}

std::wstring hex_encode(const BYTE* data, DWORD size)
{
    static constexpr wchar_t digits[] = L"0123456789ABCDEF";

    std::wstring out(static_cast<std::size_t>(size) * 2, L'0');

    for (DWORD i = 0; i < size; ++i) {
        out[static_cast<std::size_t>(i) * 2] =
            digits[(data[i] >> 4) & 0x0f];

        out[static_cast<std::size_t>(i) * 2 + 1] =
            digits[data[i] & 0x0f];
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

std::wstring protect_secret(const std::string& value)
{
    if (value.empty()) return {};

    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(
        const_cast<char*>(value.data()));
    input.cbData = static_cast<DWORD>(value.size());

    DATA_BLOB output{};

    if (!::CryptProtectData(
            &input,
            L"SimpleRemoteDesk Viewer Password",
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &output)) {
        throw std::runtime_error("CryptProtectData failed");
    }

    std::wstring result = hex_encode(output.pbData, output.cbData);
    ::LocalFree(output.pbData);
    return result;
}

std::string unprotect_secret(const std::wstring& value)
{
    auto encrypted = hex_decode(value);
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

    std::string result(
        reinterpret_cast<char*>(output.pbData),
        reinterpret_cast<char*>(output.pbData) + output.cbData);

    ::LocalFree(output.pbData);
    return result;
}

std::wstring read_string(
    const std::wstring& section,
    const wchar_t* key,
    const std::wstring& fallback,
    const std::wstring& path)
{
    std::vector<wchar_t> buffer(32768);

    const DWORD count = ::GetPrivateProfileStringW(
        section.c_str(),
        key,
        fallback.c_str(),
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        path.c_str());

    return std::wstring(buffer.data(), buffer.data() + count);
}

void write_string(
    const std::wstring& section,
    const wchar_t* key,
    const std::wstring& value,
    const std::wstring& path)
{
    if (!::WritePrivateProfileStringW(
            section.c_str(),
            key,
            value.c_str(),
            path.c_str())) {
        throw std::runtime_error("WritePrivateProfileString failed");
    }
}

}

std::string generate_profile_id()
{
    std::array<UCHAR, 16> bytes{};

    if (::BCryptGenRandom(
            nullptr,
            bytes.data(),
            static_cast<ULONG>(bytes.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        throw std::runtime_error("BCryptGenRandom failed");
    }

    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.resize(bytes.size() * 2);

    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out[i * 2] = digits[(bytes[i] >> 4) & 0x0f];
        out[i * 2 + 1] = digits[bytes[i] & 0x0f];
    }

    return out;
}

std::vector<ConnectionProfile> load_profiles()
{
    const std::wstring path = file_path();

    const unsigned int count = std::min(
        256u,
        static_cast<unsigned int>(
            ::GetPrivateProfileIntW(
                L"Viewer", L"Count", 0, path.c_str())));

    std::vector<ConnectionProfile> profiles;
    profiles.reserve(count);

    for (unsigned int i = 0; i < count; ++i) {
        const std::wstring section =
            L"Profile" + std::to_wstring(i);

        ConnectionProfile profile;

        profile.id = wide_to_utf8(
            read_string(section, L"Id", L"", path));

        profile.hostId = wide_to_utf8(
            read_string(section, L"HostId", L"", path));

        profile.name =
            read_string(section, L"Name", L"Компьютер", path);

        profile.host = wide_to_utf8(
            read_string(section, L"Host", L"", path));

        profile.port = static_cast<std::uint16_t>(std::clamp(
            ::GetPrivateProfileIntW(
                section.c_str(), L"Port", 45900, path.c_str()),
            1024u,
            65531u));

        profile.password = unprotect_secret(
            read_string(section, L"Password", L"", path));

        if (profile.id.empty()) {
            profile.id = generate_profile_id();
        }

        if (!profile.host.empty()) {
            profiles.push_back(std::move(profile));
        }
    }

    return profiles;
}

void save_profiles(const std::vector<ConnectionProfile>& profiles)
{
    const std::wstring path = file_path();

    ::WritePrivateProfileStringW(
        L"Viewer",
        nullptr,
        nullptr,
        path.c_str());

    const std::size_t count = std::min<std::size_t>(profiles.size(), 256);

    write_string(
        L"Viewer",
        L"Count",
        std::to_wstring(count),
        path);

    for (std::size_t i = 0; i < count; ++i) {
        const auto& profile = profiles[i];
        const std::wstring section =
            L"Profile" + std::to_wstring(i);

        write_string(section, L"Id", utf8_to_wide(profile.id), path);
        write_string(section, L"HostId", utf8_to_wide(profile.hostId), path);
        write_string(section, L"Name", profile.name, path);
        write_string(section, L"Host", utf8_to_wide(profile.host), path);
        write_string(section, L"Port", std::to_wstring(profile.port), path);
        write_string(
            section,
            L"Password",
            protect_secret(profile.password),
            path);
    }
}

} // namespace srd::settings
