#pragma once

#include "core/session.h"
#include "security/auth.h"

#include <cstdint>
#include <span>

namespace srd::security {

class SecureSession {
public:
    SecureSession(core::Session& transport, SessionKeys keys);

    void send(protocol::MessageType type, std::span<const std::byte> payload = {});
    protocol::Message receive();

private:
    core::Session& transport_;
    SessionKeys keys_;
    std::uint64_t sendSequence_{0};
    std::uint64_t receiveSequence_{0};
};

} // namespace srd::security
