#include "input/control_message.h"

#include <stdexcept>

namespace srd::input {

namespace {

void write_u16(std::byte* dst, std::uint16_t v)
{
    dst[0] = static_cast<std::byte>(v & 0xff);
    dst[1] = static_cast<std::byte>((v >> 8) & 0xff);
}

void write_u32(std::byte* dst, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        dst[i] = static_cast<std::byte>((v >> (i * 8)) & 0xff);
}

std::uint16_t read_u16(const std::byte* src)
{
    return static_cast<std::uint16_t>(
        std::to_integer<unsigned char>(src[0]) |
        (std::to_integer<unsigned char>(src[1]) << 8));
}

std::uint32_t read_u32(const std::byte* src)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(src[i])) << (i * 8);
    return v;
}

}

std::vector<std::byte> encode_mouse_move(MouseMove value)
{
    std::vector<std::byte> out(8);
    write_u32(out.data(), static_cast<std::uint32_t>(value.x));
    write_u32(out.data() + 4, static_cast<std::uint32_t>(value.y));
    return out;
}

std::vector<std::byte> encode_mouse_button(MouseButtonEvent value)
{
    return {
        static_cast<std::byte>(value.button),
        static_cast<std::byte>(value.down ? 1 : 0)
    };
}

std::vector<std::byte> encode_mouse_wheel(MouseWheel value)
{
    std::vector<std::byte> out(4);
    write_u32(out.data(), static_cast<std::uint32_t>(value.delta));
    return out;
}

std::vector<std::byte> encode_key(KeyEvent value)
{
    std::vector<std::byte> out(3);
    write_u16(out.data(), value.virtualKey);
    out[2] = static_cast<std::byte>(value.down ? 1 : 0);
    return out;
}

MouseMove decode_mouse_move(std::span<const std::byte> payload)
{
    if (payload.size() != 8) throw std::runtime_error("invalid mouse move payload");
    return {
        static_cast<std::int32_t>(read_u32(payload.data())),
        static_cast<std::int32_t>(read_u32(payload.data() + 4))
    };
}

MouseButtonEvent decode_mouse_button(std::span<const std::byte> payload)
{
    if (payload.size() != 2) throw std::runtime_error("invalid mouse button payload");
    return {
        static_cast<MouseButton>(std::to_integer<std::uint8_t>(payload[0])),
        std::to_integer<std::uint8_t>(payload[1]) != 0
    };
}

MouseWheel decode_mouse_wheel(std::span<const std::byte> payload)
{
    if (payload.size() != 4) throw std::runtime_error("invalid mouse wheel payload");
    return { static_cast<std::int32_t>(read_u32(payload.data())) };
}

KeyEvent decode_key(std::span<const std::byte> payload)
{
    if (payload.size() != 3) throw std::runtime_error("invalid key payload");
    return {
        read_u16(payload.data()),
        std::to_integer<std::uint8_t>(payload[2]) != 0
    };
}

} // namespace srd::input
