#include "input/injector.h"

#include <windows.h>
#include <algorithm>
#include <stdexcept>

namespace srd::input {

namespace {

void send_one(INPUT input)
{
    if (::SendInput(1, &input, sizeof(INPUT)) != 1)
        throw std::runtime_error("SendInput failed");
}

DWORD mouse_button_flag(MouseButton button, bool down)
{
    switch (button) {
    case MouseButton::Left:   return down ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP;
    case MouseButton::Right:  return down ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP;
    case MouseButton::Middle: return down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    default: throw std::runtime_error("unsupported mouse button");
    }
}

LONG normalize_axis(int value, int origin, int extent)
{
    if (extent <= 1) return 0;
    const int clamped = std::clamp(value, origin, origin + extent - 1);
    const long long relative = static_cast<long long>(clamped - origin);
    return static_cast<LONG>((relative * 65535LL) / (extent - 1));
}

}

void Injector::mouse_move(MouseMove value) const
{
    const int left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = normalize_axis(value.x, left, width);
    input.mi.dy = normalize_axis(value.y, top, height);
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    send_one(input);
}

void Injector::mouse_button(MouseButtonEvent value) const
{
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = mouse_button_flag(value.button, value.down);
    send_one(input);
}

void Injector::mouse_wheel(MouseWheel value) const
{
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.mouseData = static_cast<DWORD>(value.delta);
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    send_one(input);
}

void Injector::key(KeyEvent value) const
{
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = value.virtualKey;
    input.ki.dwFlags = value.down ? 0 : KEYEVENTF_KEYUP;
    send_one(input);
}

} // namespace srd::input
