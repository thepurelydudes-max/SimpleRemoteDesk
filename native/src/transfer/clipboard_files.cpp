#include "transfer/clipboard_files.h"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <vector>

namespace srd::transfer {

namespace {

bool open_clipboard_retry()
{
    for (int i = 0; i < 20; ++i) {
        if (::OpenClipboard(nullptr)) return true;
        ::Sleep(20);
    }
    return false;
}

}

std::vector<std::filesystem::path> get_clipboard_files()
{
    std::vector<std::filesystem::path> result;

    if (!open_clipboard_retry()) return result;

    HANDLE handle = ::GetClipboardData(CF_HDROP);

    if (handle) {
        HDROP drop = reinterpret_cast<HDROP>(handle);
        const UINT count = ::DragQueryFileW(
            drop,
            0xffffffff,
            nullptr,
            0);

        for (UINT i = 0; i < std::min<UINT>(count, 64); ++i) {
            const UINT length =
                ::DragQueryFileW(drop, i, nullptr, 0);

            std::wstring path(length + 1, L'\0');

            ::DragQueryFileW(
                drop,
                i,
                path.data(),
                length + 1);

            path.resize(length);
            result.emplace_back(std::move(path));
        }
    }

    ::CloseClipboard();
    return result;
}

bool set_clipboard_files(
    const std::vector<std::filesystem::path>& files)
{
    if (files.empty()) return false;

    std::size_t chars = 1;

    for (const auto& file : files)
        chars += file.wstring().size() + 1;

    const SIZE_T bytes =
        sizeof(DROPFILES) + chars * sizeof(wchar_t);

    HGLOBAL memory =
        ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);

    if (!memory) return false;

    auto* block = static_cast<BYTE*>(::GlobalLock(memory));
    if (!block) {
        ::GlobalFree(memory);
        return false;
    }

    auto* drop = reinterpret_cast<DROPFILES*>(block);
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;

    wchar_t* cursor =
        reinterpret_cast<wchar_t*>(block + sizeof(DROPFILES));

    for (const auto& file : files) {
        const std::wstring path = file.wstring();
        std::memcpy(
            cursor,
            path.c_str(),
            (path.size() + 1) * sizeof(wchar_t));

        cursor += path.size() + 1;
    }

    *cursor = L'\0';
    ::GlobalUnlock(memory);

    if (!open_clipboard_retry()) {
        ::GlobalFree(memory);
        return false;
    }

    ::EmptyClipboard();

    if (!::SetClipboardData(CF_HDROP, memory)) {
        ::CloseClipboard();
        ::GlobalFree(memory);
        return false;
    }

    ::CloseClipboard();
    return true;
}

std::filesystem::path make_transfer_temp_directory()
{
    wchar_t temp[MAX_PATH + 1]{};

    if (!::GetTempPathW(
            static_cast<DWORD>(std::size(temp)),
            temp)) {
        return {};
    }

    const auto now =
        std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();

    std::filesystem::path directory =
        std::filesystem::path(temp) /
        L"SimpleRemoteDesk" /
        L"Clipboard" /
        std::to_wstring(now);

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    return ec ? std::filesystem::path{} : directory;
}

} // namespace srd::transfer
