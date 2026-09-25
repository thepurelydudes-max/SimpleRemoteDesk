#pragma once

#include "capture/screen_capture.h"

#include <windows.h>
#include <memory>

namespace srd::capture {

class GdiScreenCapture final : public IScreenCapture {
public:
    GdiScreenCapture() = default;
    ~GdiScreenCapture() override;

    bool start() override;
    void stop() noexcept override;
    bool next_frame(Frame& frame) override;

private:
    bool recreate_surface();

    HDC screenDc_{nullptr};
    HDC memoryDc_{nullptr};
    HBITMAP bitmap_{nullptr};
    HGDIOBJ oldBitmap_{nullptr};
    void* bits_{nullptr};

    int left_{0};
    int top_{0};
    int width_{0};
    int height_{0};
    int stride_{0};
};

} // namespace srd::capture
