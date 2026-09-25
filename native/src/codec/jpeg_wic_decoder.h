#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace srd::codec {

struct DecodedBitmap {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t stride{};
    std::vector<std::byte> bgra;
};

DecodedBitmap decode_jpeg_wic(std::span<const std::byte> jpeg);

} // namespace srd::codec
