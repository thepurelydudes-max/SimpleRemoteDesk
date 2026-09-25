#include "capture/gdi_screen_capture.h"
#include "capture/dxgi_screen_capture.h"

#include <cstring>
#include <stdexcept>

namespace srd::capture {

GdiScreenCapture::~GdiScreenCapture()
{
    stop();
}

bool GdiScreenCapture::start()
{
    stop();

    screenDc_ = ::GetDC(nullptr);
    if (!screenDc_) return false;

    memoryDc_ = ::CreateCompatibleDC(screenDc_);
    if (!memoryDc_) {
        stop();
        return false;
    }

    return recreate_surface();
}

void GdiScreenCapture::stop() noexcept
{
    if (memoryDc_ && oldBitmap_) {
        ::SelectObject(memoryDc_, oldBitmap_);
        oldBitmap_ = nullptr;
    }

    if (bitmap_) {
        ::DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }

    if (memoryDc_) {
        ::DeleteDC(memoryDc_);
        memoryDc_ = nullptr;
    }

    if (screenDc_) {
        ::ReleaseDC(nullptr, screenDc_);
        screenDc_ = nullptr;
    }

    bits_ = nullptr;
    width_ = 0;
    height_ = 0;
    stride_ = 0;
}

bool GdiScreenCapture::recreate_surface()
{
    left_ = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    top_ = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    width_ = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height_ = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (width_ <= 0 || height_ <= 0) return false;

    if (oldBitmap_) {
        ::SelectObject(memoryDc_, oldBitmap_);
        oldBitmap_ = nullptr;
    }

    if (bitmap_) {
        ::DeleteObject(bitmap_);
        bitmap_ = nullptr;
        bits_ = nullptr;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width_;
    info.bmiHeader.biHeight = -height_;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    bitmap_ = ::CreateDIBSection(
        screenDc_,
        &info,
        DIB_RGB_COLORS,
        &bits_,
        nullptr,
        0);

    if (!bitmap_ || !bits_) return false;

    oldBitmap_ = ::SelectObject(memoryDc_, bitmap_);
    if (!oldBitmap_ || oldBitmap_ == HGDI_ERROR) return false;

    stride_ = width_ * 4;
    return true;
}

bool GdiScreenCapture::next_frame(Frame& frame)
{
    if (!screenDc_ || !memoryDc_ || !bitmap_ || !bits_) {
        return false;
    }

    const int currentLeft = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int currentTop = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int currentWidth = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int currentHeight = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (currentLeft != left_ || currentTop != top_ ||
        currentWidth != width_ || currentHeight != height_) {
        if (!recreate_surface()) return false;
    }

    if (!::BitBlt(
            memoryDc_,
            0,
            0,
            width_,
            height_,
            screenDc_,
            left_,
            top_,
            SRCCOPY | CAPTUREBLT)) {
        return false;
    }

    frame.width = static_cast<std::uint32_t>(width_);
    frame.height = static_cast<std::uint32_t>(height_);
    frame.stride = static_cast<std::uint32_t>(stride_);

    const std::size_t size =
        static_cast<std::size_t>(stride_) * static_cast<std::size_t>(height_);

    frame.pixels.resize(size);
    std::memcpy(frame.pixels.data(), bits_, size);
    return true;
}

std::unique_ptr<IScreenCapture> create_best_capture()
{
    {
        auto capture = std::make_unique<DxgiScreenCapture>();
        if (capture->start()) {
            return capture;
        }
    }

    auto fallback = std::make_unique<GdiScreenCapture>();
    if (!fallback->start()) {
        return {};
    }

    return fallback;
}

} // namespace srd::capture
