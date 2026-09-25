#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace srd::security {

using Bytes32 = std::array<std::byte, 32>;

void random_bytes(std::span<std::byte> out);

Bytes32 pbkdf2_sha256(
    std::string_view password,
    std::span<const std::byte> salt,
    std::uint64_t iterations = 200000);

Bytes32 hmac_sha256(
    std::span<const std::byte> key,
    std::span<const std::byte> data);

std::vector<std::byte> aes256_cbc_encrypt(
    std::span<const std::byte, 32> key,
    std::span<const std::byte, 16> iv,
    std::span<const std::byte> plaintext);

std::vector<std::byte> aes256_cbc_decrypt(
    std::span<const std::byte, 32> key,
    std::span<const std::byte, 16> iv,
    std::span<const std::byte> ciphertext);

bool constant_time_equal(
    std::span<const std::byte> a,
    std::span<const std::byte> b) noexcept;

} // namespace srd::security
