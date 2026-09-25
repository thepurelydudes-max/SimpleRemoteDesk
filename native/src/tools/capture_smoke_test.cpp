#include "capture/screen_capture.h"
#include "codec/jpeg_wic.h"

#include <objbase.h>

#include <fstream>
#include <iostream>
#include <stdexcept>

int main()
{
    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "COM initialization failed\n";
        return 1;
    }

    try {
        auto capture = srd::capture::create_best_capture();
        if (!capture) {
            throw std::runtime_error("screen capture initialization failed");
        }

        srd::capture::Frame frame;
        if (!capture->next_frame(frame)) {
            throw std::runtime_error("screen capture failed");
        }

        auto jpeg = srd::codec::encode_jpeg_wic(frame, 0.80f);

        std::ofstream out("capture_smoke_test.jpg", std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("failed to create output file");
        }

        out.write(
            reinterpret_cast<const char*>(jpeg.data()),
            static_cast<std::streamsize>(jpeg.size()));

        if (!out) {
            throw std::runtime_error("failed to write output file");
        }

        std::cout
            << "Captured "
            << frame.width << "x" << frame.height
            << ", JPEG bytes: " << jpeg.size() << "\n";

        ::CoUninitialize();
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "Capture smoke test failed: " << ex.what() << "\n";
        ::CoUninitialize();
        return 1;
    }
}
