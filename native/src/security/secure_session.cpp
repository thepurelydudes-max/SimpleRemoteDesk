#include "security/secure_session.h"

#include "security/crypto.h"

#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace srd::security {

namespace {

constexpr std::size_t kIvSize = 16;
constexpr std::size_t kMacSize = 32;
constexpr std::size_t kSequenceSize = 8;

void write_u64_le(std::byte* dst, std::uint64_t value)
{
    for (std::size_t i = 0; i < 8; ++i) {
        dst[i] = static_cast<std::byte>((value >> (i * 8)) & 0xFF);
    }
}

std::uint64_t read_u64_le(const std::byte* src)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(
            std::to_integer<unsigned char>(src[i])) << (i * 8);
    }
    return value;
}

std::vector<std::byte> mac_input(
    std::uint64_t sequence,
    std::span<const std::byte> iv,
    std::span<const std::byte> ciphertext)
{
    std::vector<std::byte> input(kSequenceSize + iv.size() + ciphertext.size());
    write_u64_le(input.data(), sequence);
    std::memcpy(input.data() + kSequenceSize, iv.data(), iv.size());
    if (!ciphertext.empty()) {
        std::memcpy(
            input.data() + kSequenceSize + iv.size(),
            ciphertext.data(),
            ciphertext.size());
    }
    return input;
}

}

SecureSession::SecureSession(core::Session& transport, SessionKeys keys)
    : transport_(transport), keys_(std::move(keys))
{
}

void SecureSession::send(protocol::MessageType type, std::span<const std::byte> payload)
{
    std::vector<std::byte> plaintext(1 + payload.size());
    plaintext[0] = static_cast<std::byte>(type);
    if (!payload.empty()) {
        std::memcpy(plaintext.data() + 1, payload.data(), payload.size());
    }

    std::array<std::byte, kIvSize> iv{};
    random_bytes(iv);

    auto ciphertext = aes256_cbc_encrypt(keys_.encryption, iv, plaintext);
    const std::uint64_t sequence = sendSequence_++;

    auto authenticated = mac_input(sequence, iv, ciphertext);
    Bytes32 mac = hmac_sha256(keys_.authentication, authenticated);

    std::vector<std::byte> envelope(
        kSequenceSize + kIvSize + ciphertext.size() + kMacSize);

    write_u64_le(envelope.data(), sequence);
    std::memcpy(envelope.data() + kSequenceSize, iv.data(), iv.size());
    std::memcpy(
        envelope.data() + kSequenceSize + kIvSize,
        ciphertext.data(),
        ciphertext.size());
    std::memcpy(
        envelope.data() + kSequenceSize + kIvSize + ciphertext.size(),
        mac.data(),
        mac.size());

    transport_.send(protocol::MessageType::SecureEnvelope, envelope);
}

protocol::Message SecureSession::receive()
{
    auto envelope = transport_.receive();
    if (envelope.type != protocol::MessageType::SecureEnvelope) {
        throw std::runtime_error("expected SecureEnvelope");
    }

    if (envelope.payload.size() < kSequenceSize + kIvSize + 16 + kMacSize) {
        throw std::runtime_error("secure envelope too small");
    }

    const std::uint64_t sequence = read_u64_le(envelope.payload.data());
    if (sequence != receiveSequence_) {
        throw std::runtime_error("secure sequence mismatch");
    }

    std::span<const std::byte, kIvSize> iv(
        envelope.payload.data() + kSequenceSize,
        kIvSize);

    const std::size_t cipherOffset = kSequenceSize + kIvSize;
    const std::size_t cipherSize =
        envelope.payload.size() - cipherOffset - kMacSize;

    std::span<const std::byte> ciphertext(
        envelope.payload.data() + cipherOffset,
        cipherSize);

    std::span<const std::byte> receivedMac(
        envelope.payload.data() + cipherOffset + cipherSize,
        kMacSize);

    auto authenticated = mac_input(sequence, iv, ciphertext);
    Bytes32 expectedMac = hmac_sha256(keys_.authentication, authenticated);

    if (!constant_time_equal(receivedMac, expectedMac)) {
        throw std::runtime_error("secure envelope MAC mismatch");
    }

    auto plaintext = aes256_cbc_decrypt(keys_.encryption, iv, ciphertext);
    if (plaintext.empty()) {
        throw std::runtime_error("empty secure payload");
    }

    protocol::Message message;
    message.type = static_cast<protocol::MessageType>(
        std::to_integer<std::uint8_t>(plaintext[0]));
    message.payload.assign(plaintext.begin() + 1, plaintext.end());

    ++receiveSequence_;
    return message;
}

} // namespace srd::security
