#include "host/host_service.h"
#include "ui/dpi.h"
#include "host/host_window.h"
#include "settings/settings.h"
#include "viewer/viewer_app.h"

#include <windows.h>
#include <shellapi.h>

#include <memory>
#include <string>

namespace {

constexpr wchar_t kClassName[] = L"SimpleRemoteDeskNativeUnified";
constexpr wchar_t kMutexName[] = L"Local\\SimpleRemoteDesk.Native";

constexpr UINT WM_TRAY = WM_APP + 100;
constexpr UINT WM_SHOW_VIEWER = WM_APP + 101;
constexpr UINT WM_SHOW_SETTINGS = WM_APP + 102;

enum : int {
    ID_TRAY_VIEWER = 5001,
    ID_TRAY_SETTINGS,
    ID_TRAY_EXIT
};

bool has_arg(const wchar_t* value)
{
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    bool found = false;

    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], value) == 0) {
                found = true;
                break;
            }
        }

        ::LocalFree(argv);
    }

    return found;
}

class UnifiedApp {
public:
    explicit UnifiedApp(HINSTANCE instance)
        : instance_(instance)
    {
    }

    ~UnifiedApp()
    {
        remove_tray();
        service_.stop();
    }

    bool initialize()
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &UnifiedApp::window_proc;
        wc.hInstance = instance_;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;

        if (!::RegisterClassExW(&wc) &&
            ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        hwnd_ = ::CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kClassName,
            L"SimpleRemoteDesk",
            WS_POPUP,
            0, 0, 0, 0,
            nullptr,
            nullptr,
            instance_,
            this);

        if (!hwnd_) return false;

        add_tray();

        try {
            auto settings = srd::settings::load_host_settings();

            // Persist generated password/HostId immediately.
            srd::settings::save_host_settings(settings);

            srd::host::HostConfig config;
            config.port = settings.port;
            config.password = settings.password;
            config.fps = settings.fps;
            config.jpegQuality =
                static_cast<float>(settings.jpegQuality) / 100.0f;
            config.hostId = settings.hostId;
            config.audioEnabled = settings.audioEnabled;

            service_.start(std::move(config));
        }
        catch (const std::exception& ex) {
            std::wstring message =
                L"Host не удалось автоматически запустить. Viewer всё равно доступен.\n\n";

            const std::string text = ex.what();
            message.append(text.begin(), text.end());

            ::MessageBoxW(
                nullptr,
                message.c_str(),
                L"Simple Remote Desk",
                MB_OK | MB_ICONWARNING);
        }

        return true;
    }

    int run()
    {
        if (has_arg(L"--settings")) {
            ::PostMessageW(hwnd_, WM_SHOW_SETTINGS, 0, 0);
        }
        else if (!has_arg(L"--autostart")) {
            ::PostMessageW(hwnd_, WM_SHOW_VIEWER, 0, 0);
        }

        MSG msg{};
        while (!exiting_) {
            const BOOL result = ::GetMessageW(&msg, nullptr, 0, 0);
            if (result <= 0) break;
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }

        return static_cast<int>(msg.wParam);
    }

private:
    static LRESULT CALLBACK window_proc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        UnifiedApp* self = nullptr;

        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<CREATESTRUCTW*>(lParam);

            self =
                static_cast<UnifiedApp*>(create->lpCreateParams);

            ::SetWindowLongPtrW(
                hwnd,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<UnifiedApp*>(
                ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self) {
            return self->handle_message(
                hwnd,
                message,
                wParam,
                lParam);
        }

        return ::DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);
    }

    LRESULT handle_message(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        switch (message) {
        case WM_SHOW_VIEWER:
            show_viewer();
            return 0;

        case WM_SHOW_SETTINGS:
            show_settings();
            return 0;

        case WM_TRAY:
            if (lParam == WM_LBUTTONDBLCLK) {
                show_viewer();
                return 0;
            }

            if (lParam == WM_RBUTTONUP ||
                lParam == WM_CONTEXTMENU) {
                show_tray_menu();
                return 0;
            }
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case ID_TRAY_VIEWER:
                show_viewer();
                return 0;

            case ID_TRAY_SETTINGS:
                show_settings();
                return 0;

            case ID_TRAY_EXIT:
                ::DestroyWindow(hwnd);
                return 0;
            }
            break;

        case WM_DESTROY:
            exiting_ = true;
            remove_tray();
            service_.stop();
            hwnd_ = nullptr;
            ::PostQuitMessage(0);
            return 0;
        }

        return ::DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);
    }

    void show_viewer()
    {
        if (viewerOpen_) return;

        viewerOpen_ = true;
        srd::viewer::run_viewer_app(instance_, SW_SHOW);
        viewerOpen_ = false;
    }

    void show_settings()
    {
        if (settingsOpen_) return;

        settingsOpen_ = true;

        try {
            srd::host::HostWindow window(
                service_,
                srd::settings::load_host_settings(),
                false);

            window.run(instance_, SW_SHOW);
        }
        catch (...) {
        }

        settingsOpen_ = false;
    }

    void add_tray()
    {
        tray_ = {};
        tray_.cbSize = sizeof(tray_);
        tray_.hWnd = hwnd_;
        tray_.uID = 1;
        tray_.uFlags =
            NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;

        tray_.uCallbackMessage = WM_TRAY;
        tray_.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);

        wcscpy_s(
            tray_.szTip,
            L"Simple Remote Desk — Host работает");

        ::Shell_NotifyIconW(NIM_ADD, &tray_);
        trayAdded_ = true;
    }

    void remove_tray() noexcept
    {
        if (!trayAdded_) return;

        ::Shell_NotifyIconW(NIM_DELETE, &tray_);
        trayAdded_ = false;
    }

    void show_tray_menu()
    {
        POINT point{};
        ::GetCursorPos(&point);

        HMENU menu = ::CreatePopupMenu();
        if (!menu) return;

        ::AppendMenuW(
            menu,
            MF_STRING,
            ID_TRAY_VIEWER,
            L"Открыть Viewer");

        ::AppendMenuW(
            menu,
            MF_STRING,
            ID_TRAY_SETTINGS,
            L"Настройки Host");

        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        ::AppendMenuW(
            menu,
            MF_STRING,
            ID_TRAY_EXIT,
            L"Выход");

        ::SetForegroundWindow(hwnd_);

        ::TrackPopupMenu(
            menu,
            TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
            point.x,
            point.y,
            0,
            hwnd_,
            nullptr);

        ::DestroyMenu(menu);
    }

    HINSTANCE instance_{};
    HWND hwnd_{nullptr};

    srd::host::HostService service_;

    NOTIFYICONDATAW tray_{};
    bool trayAdded_{false};
    bool viewerOpen_{false};
    bool settingsOpen_{false};
    bool exiting_{false};
};

}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int)
{
    srd::ui::enable_dpi_awareness();

    HANDLE mutex = ::CreateMutexW(
        nullptr,
        TRUE,
        kMutexName);

    if (!mutex) return 1;

    const bool alreadyRunning =
        ::GetLastError() == ERROR_ALREADY_EXISTS;

    if (alreadyRunning) {
        HWND existing =
            ::FindWindowW(kClassName, L"SimpleRemoteDesk");

        if (existing) {
            ::PostMessageW(
                existing,
                has_arg(L"--settings")
                    ? WM_SHOW_SETTINGS
                    : WM_SHOW_VIEWER,
                0,
                0);
        }

        ::CloseHandle(mutex);
        return 0;
    }

    int exitCode = 1;

    {
        UnifiedApp app(instance);

        if (app.initialize()) {
            exitCode = app.run();
        }
    }

    ::ReleaseMutex(mutex);
    ::CloseHandle(mutex);

    return exitCode;
}
