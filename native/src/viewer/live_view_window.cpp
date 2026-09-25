#include "viewer/live_view_window.h"

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

LiveViewWindow::LiveViewWindow(DecodedFrameMailbox& mailbox)
    : mailbox_(mailbox)
{
}

int LiveViewWindow::run(HINSTANCE instance, int showCommand)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &LiveViewWindow::window_proc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;

    if (!::RegisterClassExW(&wc)) {
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

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

void LiveViewWindow::paint(HWND hwnd)
{
    PAINTSTRUCT ps{};
    HDC dc = ::BeginPaint(hwnd, &ps);

    RECT client{};
    ::GetClientRect(hwnd, &client);
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;

    HBRUSH background = ::CreateSolidBrush(RGB(18, 18, 18));
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
