#pragma once

#include "host/host_service.h"
#include "settings/settings.h"

#include <windows.h>
#include <string>

namespace srd::host {

class HostWindow {
public:
    HostWindow(HostService& service, settings::HostSettings settings, bool stopServiceOnClose = true);
    ~HostWindow();

    int run(HINSTANCE instance, int showCommand);
    void show_settings();

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle_message(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void create_controls(HWND hwnd);
    void layout_controls(HWND hwnd);
    void paint(HWND hwnd);
    void draw_owner_button(const DRAWITEMSTRUCT& item);
    void toggle_server();
    void sync_controls_from_settings();
    void read_settings_from_controls();
    void save_settings();
    void update_service_status(const std::wstring& text);
    void update_status_controls();

    static std::wstring get_text(HWND control);
    static unsigned int get_uint(HWND control, unsigned int fallback);
    static void set_text(HWND control, const std::wstring& text);

    HostService& service_;
    settings::HostSettings settings_;

    HWND hwnd_{nullptr};
    HWND portEdit_{nullptr};
    HWND passwordEdit_{nullptr};
    HWND fpsEdit_{nullptr};
    HWND qualityEdit_{nullptr};
    HWND showPassword_{nullptr};
    HWND audioEnabled_{nullptr};
    HWND autostart_{nullptr};
    HWND startButton_{nullptr};
    HWND disconnectButton_{nullptr};
    HWND newPasswordButton_{nullptr};

    HFONT font_{nullptr};
    HFONT fontSemibold_{nullptr};
    HFONT fontTitle_{nullptr};
    HFONT fontLarge_{nullptr};

    HBRUSH editBrush_{nullptr};

    std::wstring serviceStatus_{L"Host остановлен"};
    bool stopServiceOnClose_{true};
};

} // namespace srd::host
