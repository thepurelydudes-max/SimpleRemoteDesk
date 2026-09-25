#include "video/screen_producer.h"

#include "codec/jpeg_wic.h"

#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

namespace srd::video {

ScreenProducer::ScreenProducer(
    std::unique_ptr<capture::IScreenCapture> capture,
    LatestFrameMailbox& mailbox)
    : capture_(std::move(capture)),
      mailbox_(mailbox)
{
    if (!capture_) {
        throw std::invalid_argument("capture must not be null");
    }
}

ScreenProducer::~ScreenProducer()
{
    stop();
}

void ScreenProducer::start(unsigned int targetFps, float jpegQuality)
{
    if (targetFps == 0) {
        throw std::invalid_argument("targetFps must be > 0");
    }

    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    worker_ = std::thread(
        &ScreenProducer::run,
        this,
        targetFps,
        jpegQuality);
}

void ScreenProducer::stop() noexcept
{
    if (!running_.exchange(false)) {
        return;
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

void ScreenProducer::run(unsigned int targetFps, float jpegQuality)
{
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(com);

    const auto frameInterval =
        std::chrono::microseconds(1000000 / std::max(1u, targetFps));

    std::uint64_t sequence = 0;
    capture::Frame raw;

    auto nextDeadline = std::chrono::steady_clock::now();

    while (running_.load(std::memory_order_relaxed)) {
        nextDeadline += frameInterval;

        if (capture_->next_frame(raw)) {
            auto encoded = std::make_shared<EncodedFrame>();
            encoded->width = raw.width;
            encoded->height = raw.height;
            encoded->sequence = ++sequence;
            encoded->jpeg = codec::encode_jpeg_wic(raw, jpegQuality);
            mailbox_.publish(std::move(encoded));
        }

        std::this_thread::sleep_until(nextDeadline);

        const auto now = std::chrono::steady_clock::now();
        if (nextDeadline + frameInterval < now) {
            nextDeadline = now;
        }
    }

    if (comInitialized) {
        ::CoUninitialize();
    }
}

} // namespace srd::video
