#include "video/frame_packet.h"

#include <cstring>
#include <stdexcept>

namespace srd::video {

namespace {

void write_u32(std::byte* dst, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i)
        dst[i] = static_cast<std::byte>((value >> (i * 8)) & 0xff);
}

void write_u64(std::byte* dst, std::uint64_t value)
{
    for (std::size_t i = 0; i < 8; ++i)
        dst[i] = static_cast<std::byte>((value >> (i * 8)) & 0xff);
}

std::uint32_t read_u32(const std::byte* src)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(
            std::to_integer<unsigned char>(src[i])) << (i * 8);
    return value;
}

std::uint64_t read_u64(const std::byte* src)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(
            std::to_integer<unsigned char>(src[i])) << (i * 8);
    return value;
}

}

std::vector<std::byte> serialize_frame(const EncodedFrame& frame)
{
    constexpr std::size_t headerSize = 16;
    std::vector<std::byte> out(headerSize + frame.jpeg.size());

    write_u32(out.data(), frame.width);
    write_u32(out.data() + 4, frame.height);
    write_u64(out.data() + 8, frame.sequence);

    if (!frame.jpeg.empty())
        std::memcpy(out.data() + headerSize, frame.jpeg.data(), frame.jpeg.size());

    return out;
}

EncodedFrame deserialize_frame(std::span<const std::byte> payload)
{
    constexpr std::size_t headerSize = 16;
    if (payload.size() < headerSize)
        throw std::runtime_error("screen frame payload too small");

    EncodedFrame frame;
    frame.width = read_u32(payload.data());
    frame.height = read_u32(payload.data() + 4);
    frame.sequence = read_u64(payload.data() + 8);
    frame.jpeg.assign(payload.begin() + headerSize, payload.end());

    if (frame.width == 0 || frame.height == 0 || frame.jpeg.empty())
        throw std::runtime_error("invalid screen frame payload");

    return frame;
}

} // namespace srd::video
