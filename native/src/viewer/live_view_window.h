#pragma once

#include "viewer/frame_mailbox.h"

#include <windows.h>
#include <cstdint>

namespace srd::control {
class ControlClient;
}

namespace srd::viewer {

class LiveViewWindow {
public:
    LiveViewWindow(
        DecodedFrameMailbox& mailbox,
        control::ControlClient* control = nullptr);

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
    bool map_to_remote(LPARAM lParam, std::int32_t& x, std::int32_t& y) const;
    void toggle_fullscreen(HWND hwnd);
    void release_pressed_keys() noexcept;

    DecodedFrameMailbox& mailbox_;
    control::ControlClient* control_{nullptr};
    HWND hwnd_{nullptr};

    bool fullscreen_{false};
    RECT windowedRect_{};
    DWORD windowedStyle_{0};
    DWORD windowedExStyle_{0};

    ULONGLONG lastMouseSend_{0};
    bool pressedKeys_[256]{};
};

} // namespace srd::viewer
