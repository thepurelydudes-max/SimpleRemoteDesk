#include "codec/jpeg_wic_decoder.h"

#include <windows.h>
#include <wincodec.h>
#include <objbase.h>
#include <shlwapi.h>

#include <cstring>
#include <stdexcept>

namespace srd::codec {

namespace {

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** put()
    {
        reset();
        return &ptr_;
    }

    T* get() const noexcept { return ptr_; }
    T* operator->() const noexcept { return ptr_; }

    void reset() noexcept
    {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_{nullptr};
};

void check(HRESULT hr, const char* message)
{
    if (FAILED(hr)) throw std::runtime_error(message);
}

} // namespace

DecodedBitmap decode_jpeg_wic(std::span<const std::byte> jpeg)
{
    if (jpeg.empty()) {
        throw std::runtime_error("empty JPEG");
    }

    ComPtr<IWICImagingFactory> factory;
    check(::CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.put())),
        "CoCreateInstance WIC failed");

    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, jpeg.size());
    if (!memory) throw std::runtime_error("GlobalAlloc failed");

    void* dst = ::GlobalLock(memory);
    if (!dst) {
        ::GlobalFree(memory);
        throw std::runtime_error("GlobalLock failed");
    }

    std::memcpy(dst, jpeg.data(), jpeg.size());
    ::GlobalUnlock(memory);

    ComPtr<IStream> stream;
    HRESULT hr = ::CreateStreamOnHGlobal(memory, TRUE, stream.put());
    if (FAILED(hr)) {
        ::GlobalFree(memory);
        check(hr, "CreateStreamOnHGlobal failed");
    }

    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromStream(
        stream.get(),
        nullptr,
        WICDecodeMetadataCacheOnDemand,
        decoder.put()),
        "CreateDecoderFromStream failed");

    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, frame.put()),
        "GetFrame failed");

    UINT width = 0;
    UINT height = 0;
    check(frame->GetSize(&width, &height),
        "GetSize failed");

    ComPtr<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(converter.put()),
        "CreateFormatConverter failed");

    check(converter->Initialize(
        frame.get(),
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom),
        "FormatConverter initialize failed");

    DecodedBitmap result;
    result.width = width;
    result.height = height;
    result.stride = width * 4u;
    result.bgra.resize(static_cast<std::size_t>(result.stride) * height);

    check(converter->CopyPixels(
        nullptr,
        result.stride,
        static_cast<UINT>(result.bgra.size()),
        reinterpret_cast<BYTE*>(result.bgra.data())),
        "CopyPixels failed");

    return result;
}

} // namespace srd::codec
