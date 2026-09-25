#pragma once

#include "codec/jpeg_wic_decoder.h"

#include <memory>
#include <mutex>

namespace srd::viewer {

class DecodedFrameMailbox {
public:
    void publish(std::shared_ptr<const codec::DecodedBitmap> frame)
    {
        std::lock_guard lock(mutex_);
        latest_ = std::move(frame);
    }

    std::shared_ptr<const codec::DecodedBitmap> latest() const
    {
        std::lock_guard lock(mutex_);
        return latest_;
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const codec::DecodedBitmap> latest_;
};

} // namespace srd::viewer
