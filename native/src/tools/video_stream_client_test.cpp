#include "video/video_channel.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::string password = argc > 2 ? argv[2] : "change-me";

    try {
        srd::video::VideoClient client(host, password);

        std::size_t count = 0;
        client.receive_forever([&](srd::video::EncodedFrame&& frame) {
            ++count;

            if (count % 30 == 0) {
                std::ofstream out(
                    "video_stream_latest.jpg",
                    std::ios::binary | std::ios::trunc);

                if (!out) {
                    throw std::runtime_error("failed to open output image");
                }

                out.write(
                    reinterpret_cast<const char*>(frame.jpeg.data()),
                    static_cast<std::streamsize>(frame.jpeg.size()));

                if (!out) {
                    throw std::runtime_error("failed to write output image");
                }

                std::cout
                    << "frame #" << frame.sequence
                    << " " << frame.width << "x" << frame.height
                    << " jpeg=" << frame.jpeg.size()
                    << " bytes\n";
            }

            if (count >= 120) {
                throw std::runtime_error("test complete");
            }
        });
    }
    catch (const std::exception& ex) {
        if (std::string(ex.what()) == "test complete") {
            std::cout << "Video stream smoke test completed\n";
            return 0;
        }

        std::cerr << "Video stream test failed: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
