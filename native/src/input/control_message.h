#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace srd::input {

enum class MouseButton : std::uint8_t {
    Left = 1,
    Right = 2,
    Middle = 3,
};

struct MouseMove {
    std::int32_t x{};
    std::int32_t y{};
};

struct MouseButtonEvent {
    MouseButton button{};
    bool down{};
};

struct MouseWheel {
    std::int32_t delta{};
};

struct KeyEvent {
    std::uint16_t virtualKey{};
    bool down{};
};

std::vector<std::byte> encode_mouse_move(MouseMove value);
std::vector<std::byte> encode_mouse_button(MouseButtonEvent value);
std::vector<std::byte> encode_mouse_wheel(MouseWheel value);
std::vector<std::byte> encode_key(KeyEvent value);

MouseMove decode_mouse_move(std::span<const std::byte> payload);
MouseButtonEvent decode_mouse_button(std::span<const std::byte> payload);
MouseWheel decode_mouse_wheel(std::span<const std::byte> payload);
KeyEvent decode_key(std::span<const std::byte> payload);

} // namespace srd::input
