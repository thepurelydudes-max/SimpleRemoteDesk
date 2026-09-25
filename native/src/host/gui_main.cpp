#include "host/host_service.h"
#include "host/host_window.h"
#include "settings/settings.h"

#include <windows.h>

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand)
{
    try {
        srd::host::HostService service;
        srd::host::HostWindow window(
            service,
            srd::settings::load_host_settings());

        return window.run(instance, showCommand);
    }
    catch (const std::exception& ex) {
        std::wstring message =
            L"Simple Remote Host не смог запуститься.\n\n";

        const std::string text = ex.what();
        message.append(text.begin(), text.end());

        ::MessageBoxW(
            nullptr,
            message.c_str(),
            L"Simple Remote Host",
            MB_OK | MB_ICONERROR);

        return 1;
    }
}
