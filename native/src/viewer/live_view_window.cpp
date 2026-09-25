#include "viewer/live_view_window.h"

#include "control/control_client.h"
#include <windowsx.h>

#include <algorithm>
#include <memory>

namespace srd::viewer {

namespace {

constexpr wchar_t kWindowClassName[] = L"SimpleRemoteDeskNativeViewer";
constexpr UINT kFrameReadyMessage = WM_APP + 1;

RECT fit_rect(
    int clientWidth,
    int clientHeight,
    std::uint32_t frameWidth,
    std::uint32_t frameHeight)
{
    RECT rect{0, 0, clientWidth, clientHeight};

    if (clientWidth <= 0 || clientHeight <= 0 ||
        frameWidth == 0 || frameHeight == 0) {
        return rect;
    }

    const double sx = static_cast<double>(clientWidth) / frameWidth;
    const double sy = static_cast<double>(clientHeight) / frameHeight;
    const double scale = std::min(sx, sy);

    const int width = std::max(1, static_cast<int>(frameWidth * scale));
    const int height = std::max(1, static_cast<int>(frameHeight * scale));

    rect.left = (clientWidth - width) / 2;
    rect.top = (clientHeight - height) / 2;
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;
    return rect;
}

}

LiveViewWindow::LiveViewWindow(
    DecodedFrameMailbox& mailbox,
    control::ControlClient* control)
    : mailbox_(mailbox),
      control_(control)
{
}

int LiveViewWindow::run(HINSTANCE instance, int showCommand)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &LiveViewWindow::window_proc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;

    if (!::RegisterClassExW(&wc) &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }

    hwnd_ = ::CreateWindowExW(
        0,
        kWindowClassName,
        L"Simple Remote Viewer Native",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        800,
        nullptr,
        nullptr,
        instance,
        this);

    if (!hwnd_) {
        return 1;
    }

    ::ShowWindow(hwnd_, showCommand);
    ::UpdateWindow(hwnd_);
    ::SetFocus(hwnd_);

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    hwnd_ = nullptr;
    return static_cast<int>(msg.wParam);
}

void LiveViewWindow::notify_frame() const noexcept
{
    HWND hwnd = hwnd_;
    if (hwnd) {
        ::PostMessageW(hwnd, kFrameReadyMessage, 0, 0);
    }
}

LRESULT CALLBACK LiveViewWindow::window_proc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    LiveViewWindow* self = nullptr;

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<LiveViewWindow*>(create->lpCreateParams);
        ::SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<LiveViewWindow*>(
            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->handle_message(hwnd, message, wParam, lParam);
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT LiveViewWindow::handle_message(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message) {
    case kFrameReadyMessage:
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint(hwnd);
        return 0;

    case WM_MOUSEMOVE: {
        if (!control_ || !control_->connected()) return 0;

        const ULONGLONG now = ::GetTickCount64();
        if (now - lastMouseSend_ < 8) return 0;

        std::int32_t x = 0;
        std::int32_t y = 0;
        if (map_to_remote(lParam, x, y)) {
            lastMouseSend_ = now;
            try { control_->mouse_move(x, y); } catch (...) {}
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        if (!control_ || !control_->connected()) return 0;

        ::SetFocus(hwnd);

        std::int32_t x = 0;
        std::int32_t y = 0;
        if (map_to_remote(lParam, x, y)) {
            try { control_->mouse_move(x, y); } catch (...) {}
        }

        input::MouseButton button = input::MouseButton::Left;
        if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
            button = input::MouseButton::Right;
        else if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP)
            button = input::MouseButton::Middle;

        const bool down =
            message == WM_LBUTTONDOWN ||
            message == WM_RBUTTONDOWN ||
            message == WM_MBUTTONDOWN;

        try { control_->mouse_button(button, down); } catch (...) {}
        return 0;
    }

    case WM_MOUSEWHEEL:
        if (control_ && control_->connected()) {
            try {
                control_->mouse_wheel(
                    static_cast<std::int16_t>(HIWORD(wParam)));
            } catch (...) {}
        }
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        const std::uint16_t vk = static_cast<std::uint16_t>(wParam & 0xffff);

        if (vk == VK_F11 && (lParam & (1u << 30)) == 0) {
            toggle_fullscreen(hwnd);
            return 0;
        }

        if (vk == VK_ESCAPE && fullscreen_) {
            toggle_fullscreen(hwnd);
            return 0;
        }

        if (control_ && control_->connected() && vk < 256) {
            if (!pressedKeys_[vk]) {
                pressedKeys_[vk] = true;
                try { control_->key(vk, true); } catch (...) {}
            }
            return 0;
        }
        break;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
        const std::uint16_t vk = static_cast<std::uint16_t>(wParam & 0xffff);
        if (control_ && control_->connected() && vk < 256) {
            pressedKeys_[vk] = false;
            try { control_->key(vk, false); } catch (...) {}
            return 0;
        }
        break;
    }

    case WM_KILLFOCUS:
        release_pressed_keys();
        return 0;

    case WM_CLOSE:
        release_pressed_keys();
        ::DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        release_pressed_keys();
        ::PostQuitMessage(0);
        return 0;
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

bool LiveViewWindow::map_to_remote(
    LPARAM lParam,
    std::int32_t& x,
    std::int32_t& y) const
{
    auto frame = mailbox_.latest();
    if (!frame || frame->width == 0 || frame->height == 0 || !hwnd_) {
        return false;
    }

    RECT client{};
    ::GetClientRect(hwnd_, &client);
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;

    const RECT dst = fit_rect(
        clientWidth,
        clientHeight,
        frame->width,
        frame->height);

    const int px = GET_X_LPARAM(lParam);
    const int py = GET_Y_LPARAM(lParam);

    if (px < dst.left || py < dst.top ||
        px >= dst.right || py >= dst.bottom) {
        return false;
    }

    const double sx =
        static_cast<double>(frame->width) /
        std::max(1L, dst.right - dst.left);
    const double sy =
        static_cast<double>(frame->height) /
        std::max(1L, dst.bottom - dst.top);

    x = std::clamp(
        static_cast<std::int32_t>((px - dst.left) * sx),
        0,
        static_cast<std::int32_t>(frame->width - 1));

    y = std::clamp(
        static_cast<std::int32_t>((py - dst.top) * sy),
        0,
        static_cast<std::int32_t>(frame->height - 1));

    return true;
}

void LiveViewWindow::toggle_fullscreen(HWND hwnd)
{
    if (!fullscreen_) {
        windowedStyle_ = static_cast<DWORD>(
            ::GetWindowLongPtrW(hwnd, GWL_STYLE));
        windowedExStyle_ = static_cast<DWORD>(
            ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE));

        ::GetWindowRect(hwnd, &windowedRect_);

        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);

        if (::GetMonitorInfoW(
                ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST),
                &monitor)) {
            ::SetWindowLongPtrW(
                hwnd,
                GWL_STYLE,
                static_cast<LONG_PTR>(
                    windowedStyle_ & ~(WS_CAPTION | WS_THICKFRAME)));

            ::SetWindowLongPtrW(
                hwnd,
                GWL_EXSTYLE,
                static_cast<LONG_PTR>(
                    windowedExStyle_ &
                    ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                      WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)));

            ::SetWindowPos(
                hwnd,
                HWND_TOP,
                monitor.rcMonitor.left,
                monitor.rcMonitor.top,
                monitor.rcMonitor.right - monitor.rcMonitor.left,
                monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW);

            fullscreen_ = true;
        }
    } else {
        ::SetWindowLongPtrW(hwnd, GWL_STYLE, windowedStyle_);
        ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, windowedExStyle_);

        ::SetWindowPos(
            hwnd,
            nullptr,
            windowedRect_.left,
            windowedRect_.top,
            windowedRect_.right - windowedRect_.left,
            windowedRect_.bottom - windowedRect_.top,
            SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW);

        fullscreen_ = false;
    }
}

void LiveViewWindow::release_pressed_keys() noexcept
{
    if (!control_ || !control_->connected()) {
        for (bool& pressed : pressedKeys_) pressed = false;
        return;
    }

    for (std::uint16_t vk = 0; vk < 256; ++vk) {
        if (!pressedKeys_[vk]) continue;
        pressedKeys_[vk] = false;
        try { control_->key(vk, false); } catch (...) {}
    }
}

void LiveViewWindow::paint(HWND hwnd)
{
    PAINTSTRUCT ps{};
    HDC dc = ::BeginPaint(hwnd, &ps);

    RECT client{};
    ::GetClientRect(hwnd, &client);
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;

    HBRUSH background = ::CreateSolidBrush(RGB(10, 20, 36));
    ::FillRect(dc, &client, background);
    ::DeleteObject(background);

    auto frame = mailbox_.latest();
    if (frame && !frame->bgra.empty()) {
        const RECT dst = fit_rect(
            clientWidth,
            clientHeight,
            frame->width,
            frame->height);

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(frame->width);
        info.bmiHeader.biHeight = -static_cast<LONG>(frame->height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        ::SetStretchBltMode(dc, HALFTONE);
        ::StretchDIBits(
            dc,
            dst.left,
            dst.top,
            dst.right - dst.left,
            dst.bottom - dst.top,
            0,
            0,
            static_cast<int>(frame->width),
            static_cast<int>(frame->height),
            frame->bgra.data(),
            &info,
            DIB_RGB_COLORS,
            SRCCOPY);
    }

    ::EndPaint(hwnd, &ps);
}

} // namespace srd::viewer
