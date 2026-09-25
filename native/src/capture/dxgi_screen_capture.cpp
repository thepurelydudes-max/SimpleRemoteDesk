#include "capture/dxgi_screen_capture.h"

#include <windows.h>

#include <cstring>
#include <iterator>

namespace srd::capture {

DxgiScreenCapture::~DxgiScreenCapture()
{
    stop();
}

bool DxgiScreenCapture::start()
{
    stop();

    // The GDI fallback preserves the whole virtual desktop for multi-monitor systems.
    // The first optimized backend intentionally handles one output only.
    if (::GetSystemMetrics(SM_CMONITORS) > 1) {
        return false;
    }

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL actual{};

    HRESULT hr = ::D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels,
        static_cast<UINT>(std::size(levels)),
        D3D11_SDK_VERSION,
        device_.GetAddressOf(),
        &actual,
        context_.GetAddressOf());

    if (FAILED(hr)) {
        stop();
        return false;
    }

    return initialize_duplication();
}

bool DxgiScreenCapture::initialize_duplication()
{
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;

    if (FAILED(device_.As(&dxgiDevice))) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;

    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIOutput> output;

    if (FAILED(adapter->EnumOutputs(0, output.GetAddressOf()))) {
        return false;
    }

    DXGI_OUTPUT_DESC outputDesc{};
    if (FAILED(output->GetDesc(&outputDesc))) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;

    if (FAILED(output.As(&output1))) {
        return false;
    }

    if (FAILED(output1->DuplicateOutput(
            device_.Get(),
            duplication_.GetAddressOf()))) {
        return false;
    }

    width_ = static_cast<std::uint32_t>(
        outputDesc.DesktopCoordinates.right -
        outputDesc.DesktopCoordinates.left);

    height_ = static_cast<std::uint32_t>(
        outputDesc.DesktopCoordinates.bottom -
        outputDesc.DesktopCoordinates.top);

    if (width_ == 0 || height_ == 0) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width_;
    desc.Height = height_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    if (FAILED(device_->CreateTexture2D(
            &desc,
            nullptr,
            staging_.GetAddressOf()))) {
        return false;
    }

    return true;
}

void DxgiScreenCapture::stop() noexcept
{
    staging_.Reset();
    duplication_.Reset();
    context_.Reset();
    device_.Reset();
    width_ = 0;
    height_ = 0;
}

bool DxgiScreenCapture::next_frame(Frame& frame)
{
    if (!duplication_ || !context_ || !staging_) {
        return false;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    Microsoft::WRL::ComPtr<IDXGIResource> resource;

    const HRESULT acquire = duplication_->AcquireNextFrame(
        50,
        &frameInfo,
        resource.GetAddressOf());

    if (acquire == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;
    }

    if (acquire == DXGI_ERROR_ACCESS_LOST) {
        duplication_.Reset();
        staging_.Reset();
        return initialize_duplication() && next_frame(frame);
    }

    if (FAILED(acquire)) {
        return false;
    }

    struct ReleaseGuard {
        IDXGIOutputDuplication* duplication{};
        ~ReleaseGuard()
        {
            if (duplication) duplication->ReleaseFrame();
        }
    } guard{duplication_.Get()};

    Microsoft::WRL::ComPtr<ID3D11Texture2D> source;

    if (FAILED(resource.As(&source))) {
        return false;
    }

    context_->CopyResource(staging_.Get(), source.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};

    if (FAILED(context_->Map(
            staging_.Get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped))) {
        return false;
    }

    struct UnmapGuard {
        ID3D11DeviceContext* context{};
        ID3D11Texture2D* texture{};
        ~UnmapGuard()
        {
            if (context && texture) context->Unmap(texture, 0);
        }
    } unmap{context_.Get(), staging_.Get()};

    frame.width = width_;
    frame.height = height_;
    frame.stride = width_ * 4;

    const std::size_t rowBytes = frame.stride;
    const std::size_t totalBytes =
        rowBytes * static_cast<std::size_t>(height_);

    frame.pixels.resize(totalBytes);

    const auto* src =
        static_cast<const std::byte*>(mapped.pData);

    for (std::uint32_t y = 0; y < height_; ++y) {
        std::memcpy(
            frame.pixels.data() +
                static_cast<std::size_t>(y) * rowBytes,
            src +
                static_cast<std::size_t>(y) * mapped.RowPitch,
            rowBytes);
    }

    return true;
}

} // namespace srd::capture
