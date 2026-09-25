#include "viewer/session_runner.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace {

std::string narrow(const std::wstring& text)
{
    if (text.empty()) return {};

    const int size = ::WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);

    std::string out(static_cast<std::size_t>(size), '\0');

    ::WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        out.data(), size, nullptr, nullptr);

    return out;
}

}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand)
{
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

    const std::wstring hostWide = argc > 1 ? argv[1] : L"127.0.0.1";
    const std::wstring passwordWide = argc > 2 ? argv[2] : L"change-me";

    if (argv) ::LocalFree(argv);

    return srd::viewer::run_live_session(
        instance,
        narrow(hostWide),
        narrow(passwordWide),
        showCommand);
}
