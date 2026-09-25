#include "codec/jpeg_wic_decoder.h"
#include "video/video_channel.h"
#include "viewer/frame_mailbox.h"
#include "viewer/live_view_window.h"

#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR commandLine,
    int showCommand)
{
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

    std::wstring hostWide = argc > 1 ? argv[1] : L"127.0.0.1";
    std::wstring passwordWide = argc > 2 ? argv[2] : L"change-me";

    if (argv) {
        ::LocalFree(argv);
    }

    auto narrow = [](const std::wstring& text) {
        if (text.empty()) return std::string{};

        const int size = ::WideCharToMultiByte(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            nullptr, 0, nullptr, nullptr);

        std::string out(static_cast<std::size_t>(size), '\0');
        ::WideCharToMultiByte(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            out.data(), size, nullptr, nullptr);

        return out;
    };

    const std::string host = narrow(hostWide);
    const std::string password = narrow(passwordWide);

    srd::viewer::DecodedFrameMailbox mailbox;
    srd::viewer::LiveViewWindow window(mailbox);

    std::atomic<bool> windowReady{false};

    std::thread videoThread([&] {
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool comInitialized = SUCCEEDED(hr);

        try {
            while (!windowReady.load(std::memory_order_acquire)) {
                ::Sleep(10);
            }

            srd::video::VideoClient client(host, password, 45902);
            client.receive_forever([&](srd::video::EncodedFrame&& frame) {
                auto decoded = std::make_shared<srd::codec::DecodedBitmap>(
                    srd::codec::decode_jpeg_wic(frame.jpeg));

                mailbox.publish(std::move(decoded));
                window.notify_frame();
            });
        }
        catch (...) {
            // First GUI milestone: connection errors simply stop the video thread.
            // Status UI and reconnect policy are added in the next step.
        }

        if (comInitialized) {
            ::CoUninitialize();
        }
    });

    windowReady.store(true, std::memory_order_release);
    const int exitCode = window.run(instance, showCommand);

    // The process is exiting; a blocking network receive is allowed to end with process
    // teardown in this first GUI milestone. A cancellable VideoClient follows next.
    if (videoThread.joinable()) {
        videoThread.detach();
    }

    return exitCode;
}
