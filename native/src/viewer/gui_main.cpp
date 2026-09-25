#include "viewer/session_runner.h"
#include "viewer/viewer_app.h"

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

    if (argc > 2) {
        const std::wstring hostWide = argv[1];
        const std::wstring passwordWide = argv[2];

        if (argv) ::LocalFree(argv);

        return srd::viewer::run_live_session(
            instance,
            narrow(hostWide),
            narrow(passwordWide),
            showCommand);
    }

    if (argv) ::LocalFree(argv);

    srd::viewer::run_viewer_app(instance, showCommand);
    return 0;
}
