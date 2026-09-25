#pragma once

#include "capture/screen_capture.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace srd::capture {

class DxgiScreenCapture final : public IScreenCapture {
public:
    DxgiScreenCapture() = default;
    ~DxgiScreenCapture() override;

    bool start() override;
    void stop() noexcept override;
    bool next_frame(Frame& frame) override;

private:
    bool initialize_duplication();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;

    std::uint32_t width_{0};
    std::uint32_t height_{0};
};

} // namespace srd::capture
