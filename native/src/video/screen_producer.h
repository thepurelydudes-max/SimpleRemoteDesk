#pragma once

#include "capture/screen_capture.h"
#include "video/latest_frame_mailbox.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace srd::video {

class ScreenProducer {
public:
    ScreenProducer(
        std::unique_ptr<capture::IScreenCapture> capture,
        LatestFrameMailbox& mailbox);

    ~ScreenProducer();

    ScreenProducer(const ScreenProducer&) = delete;
    ScreenProducer& operator=(const ScreenProducer&) = delete;

    void start(unsigned int targetFps = 30, float jpegQuality = 0.80f);
    void stop() noexcept;

private:
    void run(unsigned int targetFps, float jpegQuality);

    std::unique_ptr<capture::IScreenCapture> capture_;
    LatestFrameMailbox& mailbox_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

} // namespace srd::video
