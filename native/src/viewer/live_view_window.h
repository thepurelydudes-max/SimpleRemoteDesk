#pragma once

#include "viewer/frame_mailbox.h"

#include <windows.h>

namespace srd::viewer {

class LiveViewWindow {
public:
    explicit LiveViewWindow(DecodedFrameMailbox& mailbox);

    int run(HINSTANCE instance, int showCommand);
    void notify_frame() const noexcept;

    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK window_proc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    LRESULT handle_message(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    void paint(HWND hwnd);

    DecodedFrameMailbox& mailbox_;
    HWND hwnd_{nullptr};
};

} // namespace srd::viewer
