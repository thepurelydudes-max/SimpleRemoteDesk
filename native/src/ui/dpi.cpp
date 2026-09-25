#include "ui/dpi.h"

namespace srd::ui {

namespace {

using SetProcessDpiAwarenessContextFn = BOOL (WINAPI*)(HANDLE);
using GetDpiForSystemFn = UINT (WINAPI*)();
using GetDpiForWindowFn = UINT (WINAPI*)(HWND);

}

void enable_dpi_awareness() noexcept
{
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");

    if (user32) {
        auto setContext =
            reinterpret_cast<SetProcessDpiAwarenessContextFn>(
                ::GetProcAddress(
                    user32,
                    "SetProcessDpiAwarenessContext"));

        if (setContext) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            if (setContext(reinterpret_cast<HANDLE>(-4))) {
                return;
            }
        }
    }

    // Windows 7 compatible fallback.
    ::SetProcessDPIAware();
}

UINT system_dpi() noexcept
{
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");

    if (user32) {
        auto getDpi =
            reinterpret_cast<GetDpiForSystemFn>(
                ::GetProcAddress(user32, "GetDpiForSystem"));

        if (getDpi) {
            const UINT dpi = getDpi();
            if (dpi != 0) return dpi;
        }
    }

    HDC dc = ::GetDC(nullptr);
    const UINT dpi = dc
        ? static_cast<UINT>(::GetDeviceCaps(dc, LOGPIXELSX))
        : 96u;

    if (dc) ::ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96u;
}

UINT window_dpi(HWND hwnd) noexcept
{
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");

    if (user32) {
        auto getDpi =
            reinterpret_cast<GetDpiForWindowFn>(
                ::GetProcAddress(user32, "GetDpiForWindow"));

        if (getDpi && hwnd) {
            const UINT dpi = getDpi(hwnd);
            if (dpi != 0) return dpi;
        }
    }

    return system_dpi();
}

int scale_value(int value, UINT dpi) noexcept
{
    return ::MulDiv(value, static_cast<int>(dpi), 96);
}

RECT scale_rect(RECT rect, UINT dpi) noexcept
{
    rect.left = scale_value(rect.left, dpi);
    rect.top = scale_value(rect.top, dpi);
    rect.right = scale_value(rect.right, dpi);
    rect.bottom = scale_value(rect.bottom, dpi);
    return rect;
}

} // namespace srd::ui
