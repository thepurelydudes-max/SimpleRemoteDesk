#include "video/latest_frame_mailbox.h"

namespace srd::video {

void LatestFrameMailbox::publish(std::shared_ptr<const EncodedFrame> frame)
{
    {
        std::lock_guard lock(mutex_);
        if (stopping_) return;
        latest_ = std::move(frame);
    }
    cv_.notify_all();
}

std::shared_ptr<const EncodedFrame> LatestFrameMailbox::wait_for_newer(
    std::uint64_t lastSequence,
    bool& stopping)
{
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] {
        return stopping_ || (latest_ && latest_->sequence > lastSequence);
    });

    stopping = stopping_;
    return latest_;
}

void LatestFrameMailbox::stop() noexcept
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
}

} // namespace srd::video
