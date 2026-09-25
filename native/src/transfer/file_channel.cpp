#include "transfer/file_channel.h"

#include "core/session.h"
#include "core/socket_runtime.h"
#include "core/tcp_socket.h"
#include "protocol/message.h"
#include "security/auth.h"
#include "security/secure_session.h"
#include "transfer/clipboard_files.h"
#include "transfer/file_manifest.h"

#include <array>
#include <cstring>
#include <span>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace srd::transfer {

namespace {

constexpr std::size_t kChunkSize = 1024 * 1024;

void write_u32(std::byte* dst, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        dst[i] = static_cast<std::byte>((value >> (i * 8)) & 0xff);
}

std::uint32_t read_u32(std::span<const std::byte> data)
{
    if (data.size() < 4) throw std::runtime_error("file chunk too small");

    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[i])) << (i * 8);
    return value;
}

void send_files(
    security::SecureSession& secure,
    const FileManifest& manifest,
    const std::vector<std::filesystem::path>& sourceFiles)
{
    auto manifestBytes = serialize_manifest(manifest);
    secure.send(protocol::MessageType::FileManifest, manifestBytes);

    std::vector<char> buffer(kChunkSize);

    for (std::size_t i = 0;
         i < manifest.entries.size() && i < sourceFiles.size();
         ++i) {
        if (manifest.entries[i].directory || sourceFiles[i].empty())
            continue;

        std::ifstream input(sourceFiles[i], std::ios::binary);
        if (!input) continue;

        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize read = input.gcount();
            if (read <= 0) break;

            std::vector<std::byte> payload(
                4 + static_cast<std::size_t>(read));

            write_u32(
                payload.data(),
                static_cast<std::uint32_t>(i));

            std::memcpy(
                payload.data() + 4,
                buffer.data(),
                static_cast<std::size_t>(read));

            secure.send(protocol::MessageType::FileChunk, payload);
        }
    }

    secure.send(protocol::MessageType::FileEnd);
}

std::vector<std::filesystem::path> receive_files(
    security::SecureSession& secure)
{
    auto manifestMessage = secure.receive();

    if (manifestMessage.type != protocol::MessageType::FileManifest)
        throw std::runtime_error("FileManifest expected");

    FileManifest manifest =
        deserialize_manifest(manifestMessage.payload);

    const auto temp = make_transfer_temp_directory();
    if (temp.empty())
        throw std::runtime_error("unable to create transfer temp directory");

    std::vector<std::unique_ptr<std::ofstream>> outputs(
        manifest.entries.size());

    for (std::size_t i = 0; i < manifest.entries.size(); ++i) {
        const auto relative =
            std::filesystem::u8path(manifest.entries[i].relativePath);

        const auto full = temp / relative;
        std::error_code ec;

        if (manifest.entries[i].directory) {
            std::filesystem::create_directories(full, ec);
        } else {
            std::filesystem::create_directories(full.parent_path(), ec);
        }
    }

    for (;;) {
        auto message = secure.receive();

        if (message.type == protocol::MessageType::FileEnd)
            break;

        if (message.type != protocol::MessageType::FileChunk ||
            message.payload.size() < 4)
            throw std::runtime_error("unexpected file transfer packet");

        const std::uint32_t index =
            read_u32(message.payload);

        if (index >= manifest.entries.size() ||
            manifest.entries[index].directory)
            throw std::runtime_error("invalid file chunk index");

        if (!outputs[index]) {
            const auto full =
                temp /
                std::filesystem::u8path(
                    manifest.entries[index].relativePath);

            outputs[index] = std::make_unique<std::ofstream>(
                full,
                std::ios::binary | std::ios::trunc);

            if (!*outputs[index])
                throw std::runtime_error("unable to create transferred file");
        }

        outputs[index]->write(
            reinterpret_cast<const char*>(message.payload.data() + 4),
            static_cast<std::streamsize>(message.payload.size() - 4));

        if (!*outputs[index])
            throw std::runtime_error("unable to write transferred file");
    }

    outputs.clear();

    std::vector<std::filesystem::path> roots;
    roots.reserve(manifest.roots.size());

    for (const auto& root : manifest.roots) {
        const auto relative = std::filesystem::u8path(root);
        if (!safe_relative_path(relative))
            throw std::runtime_error("unsafe transfer root");

        const auto full = temp / relative;
        if (std::filesystem::exists(full))
            roots.push_back(full);
    }

    return roots;
}

}

FileServer::FileServer(
    std::string password,
    std::uint16_t port)
    : password_(std::move(password)),
      port_(port)
{
}

FileServer::~FileServer()
{
    stop();
}

void FileServer::start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true))
        return;

    worker_ = std::thread(&FileServer::run, this);
}

void FileServer::stop() noexcept
{
    if (!running_.exchange(false))
        return;

    {
        std::lock_guard lock(sessionMutex_);
        if (activeSession_) activeSession_->close();
    }

    try {
        net::SocketRuntime runtime;
        auto wake = net::TcpSocket::connect_to("127.0.0.1", port_);
        wake.close();
    }
    catch (...) {
    }

    if (worker_.joinable())
        worker_.join();
}

void FileServer::run()
{
    try {
        net::SocketRuntime runtime;
        auto listener = net::TcpSocket::listen_on(port_);

        while (running_.load(std::memory_order_acquire)) {
            auto socket = listener.accept_one();
            if (!running_.load(std::memory_order_acquire)) break;

            try {
                core::Session transport(std::move(socket));

                {
                    std::lock_guard lock(sessionMutex_);
                    activeSession_ = &transport;
                }

                auto keys = security::authenticate_server(
                    transport,
                    password_);

                security::SecureSession secure(
                    transport,
                    std::move(keys));

                auto command = secure.receive();

                if (command.type == protocol::MessageType::FileGet)
                    send_clipboard(secure);
                else if (command.type == protocol::MessageType::FilePut)
                    receive_clipboard(secure);
            }
            catch (...) {
            }

            {
                std::lock_guard lock(sessionMutex_);
                activeSession_ = nullptr;
            }
        }
    }
    catch (...) {
        running_.store(false, std::memory_order_release);
    }
}

void FileServer::send_clipboard(
    security::SecureSession& secure)
{
    const auto roots = get_clipboard_files();

    std::vector<std::filesystem::path> sources;
    const auto manifest = build_manifest(roots, sources);

    send_files(secure, manifest, sources);
}

void FileServer::receive_clipboard(
    security::SecureSession& secure)
{
    const auto roots = receive_files(secure);

    if (!roots.empty())
        set_clipboard_files(roots);

    secure.send(protocol::MessageType::FileAck);
}

FileClient::FileClient(
    std::string host,
    std::string password,
    std::uint16_t port)
    : host_(std::move(host)),
      password_(std::move(password)),
      port_(port)
{
}

std::size_t FileClient::download_remote_clipboard()
{
    net::SocketRuntime runtime;
    auto socket = net::TcpSocket::connect_to(host_, port_);
    core::Session transport(std::move(socket));

    auto keys = security::authenticate_client(
        transport,
        password_);

    security::SecureSession secure(
        transport,
        std::move(keys));

    secure.send(protocol::MessageType::FileGet);

    const auto roots = receive_files(secure);

    if (!roots.empty())
        set_clipboard_files(roots);

    return roots.size();
}

bool FileClient::upload_local_clipboard()
{
    const auto roots = get_clipboard_files();
    if (roots.empty()) return false;

    std::vector<std::filesystem::path> sources;
    const auto manifest = build_manifest(roots, sources);

    net::SocketRuntime runtime;
    auto socket = net::TcpSocket::connect_to(host_, port_);
    core::Session transport(std::move(socket));

    auto keys = security::authenticate_client(
        transport,
        password_);

    security::SecureSession secure(
        transport,
        std::move(keys));

    secure.send(protocol::MessageType::FilePut);
    send_files(secure, manifest, sources);

    auto ack = secure.receive();

    if (ack.type != protocol::MessageType::FileAck)
        throw std::runtime_error("FileAck expected");

    return true;
}

} // namespace srd::transfer
