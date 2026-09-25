#pragma once

namespace srd::net {

class SocketRuntime {
public:
    SocketRuntime();
    ~SocketRuntime() noexcept;

    SocketRuntime(const SocketRuntime&) = delete;
    SocketRuntime& operator=(const SocketRuntime&) = delete;

private:
    bool initialized_{false};
};

} // namespace srd::net
