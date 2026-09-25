#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace srd::protocol {

constexpr std::uint32_t kMagic = 0x31524453; // "SRD1" in little-endian memory
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderSize = 12;
constexpr std::uint32_t kMaxPayload = 16u * 1024u * 1024u;

enum class MessageType : std::uint8_t {
    Hello = 1,
    HelloAck = 2,
    Ping = 3,
    Pong = 4,

    AuthChallenge = 10,
    AuthResponse = 11,
    AuthOk = 12,
    SecureEnvelope = 13,

    MouseMove = 20,
    MouseButton = 21,
    MouseWheel = 22,
    Key = 23,
    Disconnect = 24,

    SnapshotRequest = 30,
    ScreenFrame = 31,

    AudioFormat = 40,
    AudioData = 41,

    FileGet = 50,
    FilePut = 51,
    FileManifest = 52,
    FileChunk = 53,
    FileEnd = 54,
    FileAck = 55,
};

struct Message {
    MessageType type{};
    std::vector<std::byte> payload;
};

std::vector<std::byte> encode(MessageType type, std::span<const std::byte> payload);
Message decode(std::span<const std::byte> header, std::span<const std::byte> payload);
std::uint32_t payload_size_from_header(std::span<const std::byte> header);

} // namespace srd::protocol
