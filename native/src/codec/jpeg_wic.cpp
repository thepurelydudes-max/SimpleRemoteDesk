#include "codec/jpeg_wic.h"

#include <windows.h>
#include <wincodec.h>
#include <objbase.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

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

std::vector<std::byte> encode_jpeg_wic(
    const capture::Frame& frame,
    float quality)
{
    if (frame.width == 0 || frame.height == 0 || frame.pixels.empty()) {
        throw std::runtime_error("empty frame");
    }

    quality = std::clamp(quality, 0.1f, 1.0f);

    ComPtr<IWICImagingFactory> factory;
    check(::CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.put())),
        "CoCreateInstance WIC failed");

    ComPtr<IStream> stream;
    check(::CreateStreamOnHGlobal(nullptr, TRUE, stream.put()),
        "CreateStreamOnHGlobal failed");

    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(
        GUID_ContainerFormatJpeg,
        nullptr,
        encoder.put()),
        "CreateEncoder JPEG failed");

    check(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache),
        "JPEG encoder initialize failed");

    ComPtr<IWICBitmapFrameEncode> frameEncode;
    ComPtr<IPropertyBag2> properties;
    check(encoder->CreateNewFrame(frameEncode.put(), properties.put()),
        "CreateNewFrame failed");

    PROPBAG2 option{};
    option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");

    VARIANT value;
    ::VariantInit(&value);
    value.vt = VT_R4;
    value.fltVal = quality;

    if (properties.get()) {
        properties->Write(1, &option, &value);
    }
    ::VariantClear(&value);

    check(frameEncode->Initialize(properties.get()),
        "JPEG frame initialize failed");

    check(frameEncode->SetSize(frame.width, frame.height),
        "JPEG SetSize failed");

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGR;
    check(frameEncode->SetPixelFormat(&pixelFormat),
        "JPEG SetPixelFormat failed");

    check(frameEncode->WritePixels(
        frame.height,
        frame.stride,
        static_cast<UINT>(frame.pixels.size()),
        reinterpret_cast<BYTE*>(const_cast<std::byte*>(frame.pixels.data()))),
        "JPEG WritePixels failed");

    check(frameEncode->Commit(), "JPEG frame commit failed");
    check(encoder->Commit(), "JPEG encoder commit failed");

    HGLOBAL memory = nullptr;
    check(::GetHGlobalFromStream(stream.get(), &memory),
        "GetHGlobalFromStream failed");

    const SIZE_T size = ::GlobalSize(memory);
    if (size == 0) throw std::runtime_error("JPEG output is empty");

    void* data = ::GlobalLock(memory);
    if (!data) throw std::runtime_error("GlobalLock failed");

    std::vector<std::byte> output(size);
    std::memcpy(output.data(), data, size);
    ::GlobalUnlock(memory);

    return output;
}

} // namespace srd::codec
