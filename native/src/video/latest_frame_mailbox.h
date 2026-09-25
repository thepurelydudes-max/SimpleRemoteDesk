#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include <cstddef>

namespace srd::video {

struct EncodedFrame {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t sequence{};
    std::vector<std::byte> jpeg;
};

class LatestFrameMailbox {
public:
    void publish(std::shared_ptr<const EncodedFrame> frame);
    std::shared_ptr<const EncodedFrame> wait_for_newer(
        std::uint64_t lastSequence,
        bool& stopping);
    void stop() noexcept;

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::shared_ptr<const EncodedFrame> latest_;
    bool stopping_{false};
};

} // namespace srd::video
