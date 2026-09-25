#pragma once

#include <winsock2.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace srd::net {

class TcpSocket {
public:
    TcpSocket() noexcept = default;
    explicit TcpSocket(SOCKET socket) noexcept;
    ~TcpSocket() noexcept;

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    static TcpSocket listen_on(std::uint16_t port, int backlog = 8);
    static TcpSocket connect_to(const std::string& host, std::uint16_t port);

    TcpSocket accept_one() const;

    void send_all(std::span<const std::byte> data) const;
    void recv_all(std::span<std::byte> data) const;

    void set_no_delay(bool enabled = true) const;
    void close() noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] SOCKET native_handle() const noexcept;

private:
    SOCKET socket_{INVALID_SOCKET};
};

} // namespace srd::net
