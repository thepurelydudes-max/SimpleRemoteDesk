#include "security/auth.h"

#include "protocol/message.h"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace srd::security {

namespace {

constexpr std::size_t kSaltSize = 16;
constexpr std::size_t kNonceSize = 32;
constexpr std::uint64_t kIterations = 200000;

std::vector<std::byte> concat(
    std::string_view label,
    std::span<const std::byte> a,
    std::span<const std::byte> b = {})
{
    std::vector<std::byte> out;
    out.reserve(label.size() + a.size() + b.size());

    out.insert(
        out.end(),
        reinterpret_cast<const std::byte*>(label.data()),
        reinterpret_cast<const std::byte*>(label.data() + label.size()));
    out.insert(out.end(), a.begin(), a.end());
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

SessionKeys derive_session_keys(
    const Bytes32& master,
    std::span<const std::byte> serverNonce,
    std::span<const std::byte> clientNonce)
{
    auto sessionMaterial = concat("session", serverNonce, clientNonce);
    Bytes32 sessionKey = hmac_sha256(master, sessionMaterial);

    auto encLabel = concat("enc", sessionKey);
    auto macLabel = concat("mac", sessionKey);

    return {
        hmac_sha256(sessionKey, encLabel),
        hmac_sha256(sessionKey, macLabel)
    };
}

std::vector<std::byte> make_challenge(
    std::span<const std::byte, kSaltSize> salt,
    std::span<const std::byte, kNonceSize> nonce)
{
    std::vector<std::byte> payload;
    payload.reserve(kSaltSize + kNonceSize);
    payload.insert(payload.end(), salt.begin(), salt.end());
    payload.insert(payload.end(), nonce.begin(), nonce.end());
    return payload;
}

}

SessionKeys authenticate_server(core::Session& session, std::string_view password)
{
    std::array<std::byte, kSaltSize> salt{};
    std::array<std::byte, kNonceSize> serverNonce{};
    random_bytes(salt);
    random_bytes(serverNonce);

    auto challenge = make_challenge(salt, serverNonce);
    session.send(protocol::MessageType::AuthChallenge, challenge);

    auto response = session.receive();
    if (response.type != protocol::MessageType::AuthResponse) {
        throw std::runtime_error("expected AuthResponse");
    }

    if (response.payload.size() != kNonceSize + 32) {
        throw std::runtime_error("invalid AuthResponse size");
    }

    std::span<const std::byte> clientNonce(response.payload.data(), kNonceSize);
    std::span<const std::byte> receivedProof(response.payload.data() + kNonceSize, 32);

    Bytes32 master = pbkdf2_sha256(password, salt, kIterations);
    auto proofInput = concat("client", serverNonce, clientNonce);
    Bytes32 expectedProof = hmac_sha256(master, proofInput);

    if (!constant_time_equal(receivedProof, expectedProof)) {
        throw std::runtime_error("authentication failed");
    }

    auto serverProofInput = concat("server", serverNonce, clientNonce);
    Bytes32 serverProof = hmac_sha256(master, serverProofInput);
    session.send(protocol::MessageType::AuthOk, serverProof);

    return derive_session_keys(master, serverNonce, clientNonce);
}

SessionKeys authenticate_client(core::Session& session, std::string_view password)
{
    auto challenge = session.receive();
    if (challenge.type != protocol::MessageType::AuthChallenge) {
        throw std::runtime_error("expected AuthChallenge");
    }

    if (challenge.payload.size() != kSaltSize + kNonceSize) {
        throw std::runtime_error("invalid AuthChallenge size");
    }

    std::span<const std::byte> salt(challenge.payload.data(), kSaltSize);
    std::span<const std::byte> serverNonce(challenge.payload.data() + kSaltSize, kNonceSize);

    std::array<std::byte, kNonceSize> clientNonce{};
    random_bytes(clientNonce);

    Bytes32 master = pbkdf2_sha256(password, salt, kIterations);
    auto proofInput = concat("client", serverNonce, clientNonce);
    Bytes32 proof = hmac_sha256(master, proofInput);

    std::vector<std::byte> payload;
    payload.reserve(kNonceSize + proof.size());
    payload.insert(payload.end(), clientNonce.begin(), clientNonce.end());
    payload.insert(payload.end(), proof.begin(), proof.end());

    session.send(protocol::MessageType::AuthResponse, payload);

    auto ok = session.receive();
    if (ok.type != protocol::MessageType::AuthOk || ok.payload.size() != 32) {
        throw std::runtime_error("authentication rejected");
    }

    auto serverProofInput = concat("server", serverNonce, clientNonce);
    Bytes32 expectedServerProof = hmac_sha256(master, serverProofInput);

    if (!constant_time_equal(ok.payload, expectedServerProof)) {
        throw std::runtime_error("server authentication failed");
    }

    return derive_session_keys(master, serverNonce, clientNonce);
}

} // namespace srd::security
