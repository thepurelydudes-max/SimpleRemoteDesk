#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    MessageBoxW(
        nullptr,
        L"SimpleRemoteDesk Native bootstrap is running.",
        L"SimpleRemoteDesk Native",
        MB_OK | MB_ICONINFORMATION
    );

    return 0;
}
