#include <windows.h>
#include <wincodec.h>
#include <vector>
#include <mutex>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")

namespace {
struct CaptureContext {
    HDC screen = nullptr;
    HDC memory = nullptr;
    HBITMAP dib = nullptr;
    HGDIOBJ old = nullptr;
    uint8_t* pixels = nullptr;
    int srcW = 0, srcH = 0;
    int outW = 0, outH = 0;
    int stride = 0;
    int quality = 80;
    IWICImagingFactory* factory = nullptr;
    IWICBitmap* wicBitmap = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    std::vector<uint8_t> bytes;
    std::mutex mutex;

    ~CaptureContext() {
        if (scaler) scaler->Release();
        if (wicBitmap) wicBitmap->Release();
        if (factory) factory->Release();
        if (memory) {
            if (old) SelectObject(memory, old);
            if (dib) DeleteObject(dib);
            DeleteDC(memory);
        }
        if (screen) ReleaseDC(nullptr, screen);
    }
};

HRESULT encodeJpeg(CaptureContext& c) {
    IStream* stream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr)) return hr;

    IWICBitmapEncoder* encoder = nullptr;
    hr = c.factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
    if (FAILED(hr)) { stream->Release(); return hr; }
    hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) { encoder->Release(); stream->Release(); return hr; }

    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    hr = encoder->CreateNewFrame(&frame, &props);
    if (SUCCEEDED(hr) && props) {
        PROPBAG2 option{};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value; VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = std::clamp(c.quality / 100.0f, 0.20f, 1.0f);
        props->Write(1, &option, &value);
        VariantClear(&value);
    }
    if (SUCCEEDED(hr)) hr = frame->Initialize(props);
    if (props) props->Release();
    if (SUCCEEDED(hr)) hr = frame->SetSize(static_cast<UINT>(c.outW), static_cast<UINT>(c.outH));
    WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&pf);

    IWICBitmapSource* source = c.wicBitmap;
    if (SUCCEEDED(hr) && c.scaler) source = c.scaler;
    if (SUCCEEDED(hr)) hr = frame->WriteSource(source, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();

    if (frame) frame->Release();
    encoder->Release();

    if (SUCCEEDED(hr)) {
        HGLOBAL hg = nullptr;
        hr = GetHGlobalFromStream(stream, &hg);
        if (SUCCEEDED(hr)) {
            SIZE_T size = GlobalSize(hg);
            void* ptr = GlobalLock(hg);
            if (!ptr && size) hr = E_FAIL;
            else {
                c.bytes.resize(size);
                if (size) std::memcpy(c.bytes.data(), ptr, size);
                if (ptr) GlobalUnlock(hg);
            }
        }
    }
    stream->Release();
    return hr;
}
}

extern "C" __declspec(dllexport) void* __stdcall SRD_CaptureCreate(int quality, int maxWidth, int* width, int* height) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto* c = new (std::nothrow) CaptureContext();
    if (!c) return nullptr;
    c->quality = std::clamp(quality, 20, 100);
    c->srcW = GetSystemMetrics(SM_CXSCREEN);
    c->srcH = GetSystemMetrics(SM_CYSCREEN);
    if (c->srcW <= 0 || c->srcH <= 0) { delete c; return nullptr; }
    c->outW = c->srcW;
    c->outH = c->srcH;
    if (maxWidth > 0 && c->srcW > maxWidth) {
        c->outW = maxWidth;
        c->outH = std::max(1, static_cast<int>((static_cast<int64_t>(c->srcH) * maxWidth + c->srcW / 2) / c->srcW));
    }
    c->screen = GetDC(nullptr);
    c->memory = CreateCompatibleDC(c->screen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = c->srcW;
    bmi.bmiHeader.biHeight = -c->srcH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    c->stride = ((c->srcW * 3 + 3) & ~3);
    c->dib = CreateDIBSection(c->screen, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&c->pixels), nullptr, 0);
    if (!c->screen || !c->memory || !c->dib || !c->pixels) { delete c; return nullptr; }
    c->old = SelectObject(c->memory, c->dib);

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&c->factory));
    if (FAILED(hr)) { delete c; return nullptr; }
    hr = c->factory->CreateBitmapFromMemory(c->srcW, c->srcH, GUID_WICPixelFormat24bppBGR,
        c->stride, c->stride * c->srcH, c->pixels, &c->wicBitmap);
    if (FAILED(hr)) { delete c; return nullptr; }
    if (c->outW != c->srcW || c->outH != c->srcH) {
        hr = c->factory->CreateBitmapScaler(&c->scaler);
        if (FAILED(hr)) { delete c; return nullptr; }
        hr = c->scaler->Initialize(c->wicBitmap, c->outW, c->outH, WICBitmapInterpolationModeFant);
        if (FAILED(hr)) { delete c; return nullptr; }
    }
    if (width) *width = c->outW;
    if (height) *height = c->outH;
    return c;
}

extern "C" __declspec(dllexport) int __stdcall SRD_CaptureFrame(void* handle, const uint8_t** data, int* length) {
    if (!handle || !data || !length) return 0;
    auto& c = *static_cast<CaptureContext*>(handle);
    std::lock_guard<std::mutex> lock(c.mutex);
    if (!BitBlt(c.memory, 0, 0, c.srcW, c.srcH, c.screen, 0, 0, SRCCOPY | CAPTUREBLT)) return 0;
    if (FAILED(encodeJpeg(c)) || c.bytes.empty() || c.bytes.size() > INT_MAX) return 0;
    *data = c.bytes.data();
    *length = static_cast<int>(c.bytes.size());
    return 1;
}

extern "C" __declspec(dllexport) void __stdcall SRD_CaptureDestroy(void* handle) {
    delete static_cast<CaptureContext*>(handle);
}
