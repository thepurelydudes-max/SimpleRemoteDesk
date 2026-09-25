#include "protocol/message.h"

#include <cstring>
#include <stdexcept>

namespace srd::protocol {

namespace {

template <typename T>
void write_le(std::byte* dst, T value)
{
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        dst[i] = static_cast<std::byte>((value >> (i * 8)) & 0xFF);
    }
}

template <typename T>
T read_le(const std::byte* src)
{
    T value{};
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(std::to_integer<unsigned char>(src[i])) << (i * 8);
    }
    return value;
}

void validate_header(std::span<const std::byte> header)
{
    if (header.size() != kHeaderSize) {
        throw std::runtime_error("invalid header size");
    }

    const auto magic = read_le<std::uint32_t>(header.data());
    const auto version = read_le<std::uint16_t>(header.data() + 4);

    if (magic != kMagic) {
        throw std::runtime_error("protocol magic mismatch");
    }

    if (version != kVersion) {
        throw std::runtime_error("protocol version mismatch");
    }
}

}

std::vector<std::byte> encode(MessageType type, std::span<const std::byte> payload)
{
    if (payload.size() > kMaxPayload) {
        throw std::runtime_error("payload too large");
    }

    std::vector<std::byte> bytes(kHeaderSize + payload.size());

    write_le<std::uint32_t>(bytes.data(), kMagic);
    write_le<std::uint16_t>(bytes.data() + 4, kVersion);
    bytes[6] = static_cast<std::byte>(type);
    bytes[7] = std::byte{0};
    write_le<std::uint32_t>(bytes.data() + 8, static_cast<std::uint32_t>(payload.size()));

    if (!payload.empty()) {
        std::memcpy(bytes.data() + kHeaderSize, payload.data(), payload.size());
    }

    return bytes;
}

std::uint32_t payload_size_from_header(std::span<const std::byte> header)
{
    validate_header(header);
    const auto size = read_le<std::uint32_t>(header.data() + 8);
    if (size > kMaxPayload) {
        throw std::runtime_error("payload exceeds protocol limit");
    }
    return size;
}

Message decode(std::span<const std::byte> header, std::span<const std::byte> payload)
{
    validate_header(header);

    const auto declared = payload_size_from_header(header);
    if (declared != payload.size()) {
        throw std::runtime_error("payload size mismatch");
    }

    Message message;
    message.type = static_cast<MessageType>(std::to_integer<std::uint8_t>(header[6]));
    message.payload.assign(payload.begin(), payload.end());
    return message;
}

} // namespace srd::protocol
