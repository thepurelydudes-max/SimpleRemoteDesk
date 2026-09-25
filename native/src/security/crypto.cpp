#include "security/crypto.h"

#include <bcrypt.h>
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace srd::security {

namespace {

class Algorithm {
public:
    Algorithm(LPCWSTR id, ULONG flags = 0)
    {
        NTSTATUS status = ::BCryptOpenAlgorithmProvider(&handle_, id, nullptr, flags);
        if (status < 0) throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }

    ~Algorithm() noexcept
    {
        if (handle_) ::BCryptCloseAlgorithmProvider(handle_, 0);
    }

    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;

    BCRYPT_ALG_HANDLE get() const noexcept { return handle_; }

private:
    BCRYPT_ALG_HANDLE handle_{nullptr};
};

std::vector<UCHAR> hash_object_buffer(BCRYPT_ALG_HANDLE algorithm)
{
    DWORD objectLength = 0;
    DWORD cbResult = 0;
    NTSTATUS status = ::BCryptGetProperty(
        algorithm,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength),
        sizeof(objectLength),
        &cbResult,
        0);

    if (status < 0) throw std::runtime_error("BCryptGetProperty OBJECT_LENGTH failed");
    return std::vector<UCHAR>(objectLength);
}

Bytes32 hmac_impl(std::span<const std::byte> key, std::span<const std::byte> data)
{
    Algorithm alg(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    auto object = hash_object_buffer(alg.get());

    BCRYPT_HASH_HANDLE hash = nullptr;
    NTSTATUS status = ::BCryptCreateHash(
        alg.get(),
        &hash,
        object.data(),
        static_cast<ULONG>(object.size()),
        reinterpret_cast<PUCHAR>(const_cast<std::byte*>(key.data())),
        static_cast<ULONG>(key.size()),
        0);

    if (status < 0) throw std::runtime_error("BCryptCreateHash failed");

    struct HashGuard {
        BCRYPT_HASH_HANDLE handle{};
        ~HashGuard() { if (handle) ::BCryptDestroyHash(handle); }
    } guard{hash};

    if (!data.empty()) {
        status = ::BCryptHashData(
            hash,
            reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.data())),
            static_cast<ULONG>(data.size()),
            0);
        if (status < 0) throw std::runtime_error("BCryptHashData failed");
    }

    Bytes32 output{};
    status = ::BCryptFinishHash(
        hash,
        reinterpret_cast<PUCHAR>(output.data()),
        static_cast<ULONG>(output.size()),
        0);

    if (status < 0) throw std::runtime_error("BCryptFinishHash failed");
    return output;
}

std::vector<std::byte> aes_crypt(
    bool encrypt,
    std::span<const std::byte, 32> key,
    std::span<const std::byte, 16> iv,
    std::span<const std::byte> input)
{
    Algorithm alg(BCRYPT_AES_ALGORITHM);

    NTSTATUS status = ::BCryptSetProperty(
        alg.get(),
        BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
        static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_CBC)),
        0);

    if (status < 0) throw std::runtime_error("BCryptSetProperty CBC failed");

    DWORD keyObjectLength = 0;
    DWORD cbResult = 0;
    status = ::BCryptGetProperty(
        alg.get(),
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&keyObjectLength),
        sizeof(keyObjectLength),
        &cbResult,
        0);
    if (status < 0) throw std::runtime_error("BCryptGetProperty AES object length failed");

    std::vector<UCHAR> keyObject(keyObjectLength);
    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    status = ::BCryptGenerateSymmetricKey(
        alg.get(),
        &keyHandle,
        keyObject.data(),
        static_cast<ULONG>(keyObject.size()),
        reinterpret_cast<PUCHAR>(const_cast<std::byte*>(key.data())),
        static_cast<ULONG>(key.size()),
        0);
    if (status < 0) throw std::runtime_error("BCryptGenerateSymmetricKey failed");

    struct KeyGuard {
        BCRYPT_KEY_HANDLE handle{};
        ~KeyGuard() { if (handle) ::BCryptDestroyKey(handle); }
    } guard{keyHandle};

    std::array<UCHAR, 16> ivCopy{};
    std::memcpy(ivCopy.data(), iv.data(), iv.size());

    ULONG outputLength = 0;
    if (encrypt) {
        status = ::BCryptEncrypt(
            keyHandle,
            reinterpret_cast<PUCHAR>(const_cast<std::byte*>(input.data())),
            static_cast<ULONG>(input.size()),
            nullptr,
            ivCopy.data(),
            static_cast<ULONG>(ivCopy.size()),
            nullptr,
            0,
            &outputLength,
            BCRYPT_BLOCK_PADDING);
    } else {
        status = ::BCryptDecrypt(
            keyHandle,
            reinterpret_cast<PUCHAR>(const_cast<std::byte*>(input.data())),
            static_cast<ULONG>(input.size()),
            nullptr,
            ivCopy.data(),
            static_cast<ULONG>(ivCopy.size()),
            nullptr,
            0,
            &outputLength,
            BCRYPT_BLOCK_PADDING);
    }
    if (status < 0) throw std::runtime_error("BCrypt AES size query failed");

    std::vector<std::byte> output(outputLength);
    std::memcpy(ivCopy.data(), iv.data(), iv.size());

    ULONG actual = 0;
    if (encrypt) {
        status = ::BCryptEncrypt(
            keyHandle,
            reinterpret_cast<PUCHAR>(const_cast<std::byte*>(input.data())),
            static_cast<ULONG>(input.size()),
            nullptr,
            ivCopy.data(),
            static_cast<ULONG>(ivCopy.size()),
            reinterpret_cast<PUCHAR>(output.data()),
            static_cast<ULONG>(output.size()),
            &actual,
            BCRYPT_BLOCK_PADDING);
    } else {
        status = ::BCryptDecrypt(
            keyHandle,
            reinterpret_cast<PUCHAR>(const_cast<std::byte*>(input.data())),
            static_cast<ULONG>(input.size()),
            nullptr,
            ivCopy.data(),
            static_cast<ULONG>(ivCopy.size()),
            reinterpret_cast<PUCHAR>(output.data()),
            static_cast<ULONG>(output.size()),
            &actual,
            BCRYPT_BLOCK_PADDING);
    }
    if (status < 0) throw std::runtime_error("BCrypt AES operation failed");

    output.resize(actual);
    return output;
}

} // namespace

void random_bytes(std::span<std::byte> out)
{
    if (out.empty()) return;
    NTSTATUS status = ::BCryptGenRandom(
        nullptr,
        reinterpret_cast<PUCHAR>(out.data()),
        static_cast<ULONG>(out.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    if (status < 0) throw std::runtime_error("BCryptGenRandom failed");
}

Bytes32 pbkdf2_sha256(
    std::string_view password,
    std::span<const std::byte> salt,
    std::uint64_t iterations)
{
    Algorithm alg(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    Bytes32 output{};

    NTSTATUS status = ::BCryptDeriveKeyPBKDF2(
        alg.get(),
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()),
        reinterpret_cast<PUCHAR>(const_cast<std::byte*>(salt.data())),
        static_cast<ULONG>(salt.size()),
        iterations,
        reinterpret_cast<PUCHAR>(output.data()),
        static_cast<ULONG>(output.size()),
        0);

    if (status < 0) throw std::runtime_error("BCryptDeriveKeyPBKDF2 failed");
    return output;
}

Bytes32 hmac_sha256(
    std::span<const std::byte> key,
    std::span<const std::byte> data)
{
    return hmac_impl(key, data);
}

std::vector<std::byte> aes256_cbc_encrypt(
    std::span<const std::byte, 32> key,
    std::span<const std::byte, 16> iv,
    std::span<const std::byte> plaintext)
{
    return aes_crypt(true, key, iv, plaintext);
}

std::vector<std::byte> aes256_cbc_decrypt(
    std::span<const std::byte, 32> key,
    std::span<const std::byte, 16> iv,
    std::span<const std::byte> ciphertext)
{
    return aes_crypt(false, key, iv, ciphertext);
}

bool constant_time_equal(
    std::span<const std::byte> a,
    std::span<const std::byte> b) noexcept
{
    if (a.size() != b.size()) return false;

    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(
            std::to_integer<unsigned char>(a[i]) ^
            std::to_integer<unsigned char>(b[i]));
    }
    return diff == 0;
}

} // namespace srd::security
