#include "viewer/home_window.h"
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

    for (;;) {
        srd::viewer::ViewerHomeWindow home;
        auto selected = home.run(instance, showCommand);

        if (!selected) {
            return 0;
        }

        if (selected->password.size() < 6) {
            ::MessageBoxW(
                nullptr,
                L"Для выбранного компьютера не сохранён корректный пароль.",
                L"Simple Remote Viewer",
                MB_OK | MB_ICONINFORMATION);
            continue;
        }

        srd::viewer::run_live_session(
            instance,
            selected->host,
            selected->password,
            SW_SHOW);

        showCommand = SW_SHOW;
    }
}
