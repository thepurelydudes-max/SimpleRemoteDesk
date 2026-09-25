#include "capture/screen_capture.h"
#include "video/latest_frame_mailbox.h"
#include "video/screen_producer.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

int main()
{
    try {
        auto capture = srd::capture::create_best_capture();
        if (!capture)
            throw std::runtime_error("capture initialization failed");

        srd::video::LatestFrameMailbox mailbox;
        srd::video::ScreenProducer producer(std::move(capture), mailbox);

        producer.start(15, 0.70f);

        std::uint64_t lastSequence = 0;

        for (int i = 0; i < 8; ++i) {
            bool stopping = false;
            auto frame = mailbox.wait_for_newer(lastSequence, stopping);
            if (stopping || !frame)
                break;

            std::cout
                << "consumer received sequence=" << frame->sequence
                << " size=" << frame->width << "x" << frame->height
                << " jpeg=" << frame->jpeg.size() << " bytes\n";

            lastSequence = frame->sequence;

            // Deliberately consume slower than the producer.
            // The mailbox must replace stale frames instead of queueing them.
            std::this_thread::sleep_for(std::chrono::milliseconds(220));
        }

        producer.stop();
        mailbox.stop();

        std::cout << "Latest-frame mailbox smoke test completed\n";
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "Stream smoke test failed: " << ex.what() << "\n";
        return 1;
    }
}
