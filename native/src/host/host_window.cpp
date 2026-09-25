#include "host/host_window.h"

#include "ui/theme.h"
#include "ui/dpi.h"

#include <windowsx.h>

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace srd::host {

namespace {

constexpr wchar_t kClassName[] = L"SimpleRemoteDeskHostWindow";
constexpr UINT WM_SRD_STATUS = WM_APP + 20;

enum : int {
    IDC_PORT = 1001,
    IDC_PASSWORD,
    IDC_FPS,
    IDC_QUALITY,
    IDC_SHOW_PASSWORD,
    IDC_AUDIO,
    IDC_AUTOSTART,
    IDC_START,
    IDC_DISCONNECT,
    IDC_NEW_PASSWORD
};

RECT make_rect(int l, int t, int r, int b)
{
    return RECT{l, t, r, b};
}

std::wstring widen_utf8(const std::string& text)
{
    if (text.empty()) return {};

    const int chars = ::MultiByteToWideChar(
        CP_UTF8, 0,
        text.data(), static_cast<int>(text.size()),
        nullptr, 0);

    std::wstring out(static_cast<std::size_t>(chars), L'\0');

    ::MultiByteToWideChar(
        CP_UTF8, 0,
        text.data(), static_cast<int>(text.size()),
        out.data(), chars);

    return out;
}

std::string narrow_utf8(const std::wstring& text)
{
    if (text.empty()) return {};

    const int bytes = ::WideCharToMultiByte(
        CP_UTF8, 0,
        text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);

    std::string out(static_cast<std::size_t>(bytes), '\0');

    ::WideCharToMultiByte(
        CP_UTF8, 0,
        text.data(), static_cast<int>(text.size()),
        out.data(), bytes, nullptr, nullptr);

    return out;
}

void set_control_font(HWND control, HFONT font)
{
    ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

}

HostWindow::HostWindow(
    HostService& service,
    settings::HostSettings settings,
    bool stopServiceOnClose)
    : service_(service),
      settings_(std::move(settings)),
      stopServiceOnClose_(stopServiceOnClose)
{
    font_ = ui::create_font(10);
    fontSemibold_ = ui::create_font(10, FW_SEMIBOLD);
    fontTitle_ = ui::create_font(18, FW_SEMIBOLD);
    fontLarge_ = ui::create_font(15, FW_SEMIBOLD);
    editBrush_ = ::CreateSolidBrush(ui::Surface2);

    service_.set_status_callback([this](const std::wstring& text) {
        HWND hwnd = hwnd_;
        if (!hwnd) return;

        auto copy = new std::wstring(text);
        if (!::PostMessageW(
                hwnd,
                WM_SRD_STATUS,
                0,
                reinterpret_cast<LPARAM>(copy))) {
            delete copy;
        }
    });
}

HostWindow::~HostWindow()
{
    service_.set_status_callback({});

    if (editBrush_) ::DeleteObject(editBrush_);
    if (font_) ::DeleteObject(font_);
    if (fontSemibold_) ::DeleteObject(fontSemibold_);
    if (fontTitle_) ::DeleteObject(fontTitle_);
    if (fontLarge_) ::DeleteObject(fontLarge_);
}

int HostWindow::run(HINSTANCE instance, int showCommand)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &HostWindow::window_proc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;

    if (!::RegisterClassExW(&wc) &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }

    const UINT dpi = ui::system_dpi();
    RECT desired{
        0,
        0,
        ui::scale_value(1060, dpi),
        ui::scale_value(820, dpi)};

    ::AdjustWindowRectEx(
        &desired,
        WS_OVERLAPPEDWINDOW,
        FALSE,
        0);

    hwnd_ = ::CreateWindowExW(
        0,
        kClassName,
        L"Simple Remote Host",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        desired.right - desired.left,
        desired.bottom - desired.top,
        nullptr,
        nullptr,
        instance,
        this);

    if (!hwnd_) return 1;

    ui::enable_dark_title_bar(hwnd_);

    ::ShowWindow(hwnd_, showCommand);
    ::UpdateWindow(hwnd_);

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    hwnd_ = nullptr;
    return static_cast<int>(msg.wParam);
}

void HostWindow::show_settings()
{
    if (!hwnd_) return;
    ::ShowWindow(hwnd_, SW_RESTORE);
    ::SetForegroundWindow(hwnd_);
}

LRESULT CALLBACK HostWindow::window_proc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    HostWindow* self = nullptr;

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<HostWindow*>(create->lpCreateParams);
        ::SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<HostWindow*>(
            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->handle_message(hwnd, message, wParam, lParam);
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT HostWindow::handle_message(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        create_controls(hwnd);
        sync_controls_from_settings();
        update_status_controls();
        return 0;

    case WM_SIZE:
        layout_controls(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = ui::window_dpi(hwnd);
        info->ptMinTrackSize.x = ui::scale_value(900, dpi);
        info->ptMinTrackSize.y = ui::scale_value(700, dpi);
        return 0;
    }

    case WM_SRD_STATUS: {
        std::unique_ptr<std::wstring> text(
            reinterpret_cast<std::wstring*>(lParam));
        if (text) {
            update_service_status(*text);
        }
        return 0;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == IDC_START && code == BN_CLICKED) {
            toggle_server();
            return 0;
        }

        if (id == IDC_DISCONNECT && code == BN_CLICKED) {
            service_.disconnect_client();
            return 0;
        }

        if (id == IDC_NEW_PASSWORD && code == BN_CLICKED) {
            try {
                const std::string generated = settings::generate_password();
                set_text(
                    passwordEdit_,
                    std::wstring(generated.begin(), generated.end()));
            }
            catch (...) {
            }
            return 0;
        }

        if (id == IDC_SHOW_PASSWORD && code == BN_CLICKED) {
            const bool checked =
                ::SendMessageW(showPassword_, BM_GETCHECK, 0, 0) == BST_CHECKED;

            ::SendMessageW(
                passwordEdit_,
                EM_SETPASSWORDCHAR,
                checked ? 0 : static_cast<WPARAM>(L'●'),
                0);

            ::InvalidateRect(passwordEdit_, nullptr, TRUE);
            return 0;
        }

        if (id == IDC_AUTOSTART && code == BN_CLICKED) {
            try {
                read_settings_from_controls();
                save_settings();
            }
            catch (...) {
            }
            return 0;
        }
        break;
    }

    case WM_DRAWITEM:
        draw_owner_button(
            *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        ::SetTextColor(dc, ui::Text);
        ::SetBkColor(dc, ui::Surface2);
        return reinterpret_cast<LRESULT>(editBrush_);
    }

    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        ::SetTextColor(dc, ui::Text);
        ::SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(
            ::GetStockObject(NULL_BRUSH));
    }

    case WM_PAINT:
        paint(hwnd);
        return 0;

    case WM_CLOSE:
        try {
            read_settings_from_controls();
            save_settings();
        }
        catch (...) {
        }

        if (stopServiceOnClose_) service_.stop();
        ::DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

void HostWindow::create_controls(HWND hwnd)
{
    auto makeEdit = [&](int id, DWORD extraStyle) {
        HWND control = ::CreateWindowExW(
            0,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                ES_AUTOHSCROLL | extraStyle,
            0, 0, 100, 32,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            nullptr,
            nullptr);

        set_control_font(control, font_);
        return control;
    };

    portEdit_ = makeEdit(IDC_PORT, ES_NUMBER);
    passwordEdit_ = makeEdit(IDC_PASSWORD, ES_PASSWORD);
    fpsEdit_ = makeEdit(IDC_FPS, ES_NUMBER);
    qualityEdit_ = makeEdit(IDC_QUALITY, ES_NUMBER);

    showPassword_ = ::CreateWindowExW(
        0,
        L"BUTTON",
        L"Показать пароль",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 160, 26,
        hwnd,
        reinterpret_cast<HMENU>(IDC_SHOW_PASSWORD),
        nullptr,
        nullptr);

    audioEnabled_ = ::CreateWindowExW(
        0,
        L"BUTTON",
        L"Передавать системный звук",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 320, 30,
        hwnd,
        reinterpret_cast<HMENU>(IDC_AUDIO),
        nullptr,
        nullptr);

    autostart_ = ::CreateWindowExW(
        0,
        L"BUTTON",
        L"Запускать вместе с Windows и сразу ждать подключение",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 500, 30,
        hwnd,
        reinterpret_cast<HMENU>(IDC_AUTOSTART),
        nullptr,
        nullptr);

    startButton_ = ::CreateWindowExW(
        0,
        L"BUTTON",
        L"Запустить Host",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 190, 42,
        hwnd,
        reinterpret_cast<HMENU>(IDC_START),
        nullptr,
        nullptr);

    disconnectButton_ = ::CreateWindowExW(
        0,
        L"BUTTON",
        L"Отключить клиента",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 190, 42,
        hwnd,
        reinterpret_cast<HMENU>(IDC_DISCONNECT),
        nullptr,
        nullptr);

    newPasswordButton_ = ::CreateWindowExW(
        0,
        L"BUTTON",
        L"Новый пароль",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 170, 42,
        hwnd,
        reinterpret_cast<HMENU>(IDC_NEW_PASSWORD),
        nullptr,
        nullptr);

    for (HWND control : {
        showPassword_,
        audioEnabled_,
        autostart_,
        startButton_,
        disconnectButton_,
        newPasswordButton_}) {
        set_control_font(control, fontSemibold_);
    }

    ::SendMessageW(
        passwordEdit_,
        EM_SETPASSWORDCHAR,
        static_cast<WPARAM>(L'●'),
        0);

    layout_controls(hwnd);
}

void HostWindow::layout_controls(HWND hwnd)
{
    RECT client{};
    ::GetClientRect(hwnd, &client);

    const UINT dpi = ui::window_dpi(hwnd);
    const auto S = [dpi](int value) {
        return ui::scale_value(value, dpi);
    };

    const int width = std::max(1, client.right - client.left);
    const int height = std::max(1, client.bottom - client.top);

    const int margin = S(24);
    const int gap = S(20);
    const int headerHeight = S(92);

    const int cardLeft = margin;
    const int cardRight = width - margin;
    const int cardWidth = std::max(S(600), cardRight - cardLeft);

    const int accessTop = headerHeight + S(18);
    const int accessHeight = S(230);
    const int qualityTop = accessTop + accessHeight + gap;
    const int qualityHeight = S(205);
    const int statusTop = qualityTop + qualityHeight + gap;
    const int statusBottom = height - margin;
    const int statusHeight = std::max(S(150), statusBottom - statusTop);

    const int innerLeft = cardLeft + S(38);
    const int innerRight = cardRight - S(38);
    const int columnGap = S(54);
    const int columnWidth =
        std::max(S(260), (innerRight - innerLeft - columnGap) / 2);

    const int leftX = innerLeft;
    const int rightX = innerLeft + columnWidth + columnGap;

    const int editHeight = S(34);

    ::SetWindowPos(
        portEdit_, nullptr,
        leftX,
        accessTop + S(88),
        columnWidth,
        editHeight,
        SWP_NOZORDER);

    ::SetWindowPos(
        passwordEdit_, nullptr,
        rightX,
        accessTop + S(88),
        columnWidth,
        editHeight,
        SWP_NOZORDER);

    const int passwordActionTop = accessTop + S(132);

    ::SetWindowPos(
        showPassword_, nullptr,
        rightX,
        passwordActionTop,
        S(180),
        S(28),
        SWP_NOZORDER);

    ::SetWindowPos(
        newPasswordButton_, nullptr,
        std::max(rightX + S(190), rightX + columnWidth - S(176)),
        passwordActionTop - S(6),
        S(176),
        S(40),
        SWP_NOZORDER);

    ::SetWindowPos(
        fpsEdit_, nullptr,
        leftX,
        qualityTop + S(88),
        columnWidth,
        editHeight,
        SWP_NOZORDER);

    ::SetWindowPos(
        qualityEdit_, nullptr,
        rightX,
        qualityTop + S(88),
        columnWidth,
        editHeight,
        SWP_NOZORDER);

    ::SetWindowPos(
        audioEnabled_, nullptr,
        leftX,
        qualityTop + S(134),
        S(330),
        S(28),
        SWP_NOZORDER);

    ::SetWindowPos(
        autostart_, nullptr,
        leftX,
        qualityTop + S(166),
        std::min(S(610), cardWidth - S(76)),
        S(28),
        SWP_NOZORDER);

    const int buttonHeight = S(42);
    const int buttonWidth = S(190);
    const int buttonGap = S(12);
    const int buttonsTop =
        std::max(statusTop + S(94), statusBottom - buttonHeight - S(18));

    ::SetWindowPos(
        disconnectButton_, nullptr,
        cardRight - S(38) - buttonWidth,
        buttonsTop,
        buttonWidth,
        buttonHeight,
        SWP_NOZORDER);

    ::SetWindowPos(
        startButton_, nullptr,
        cardRight - S(38) - buttonWidth * 2 - buttonGap,
        buttonsTop,
        buttonWidth,
        buttonHeight,
        SWP_NOZORDER);

    (void)statusHeight;
}

void HostWindow::paint(HWND hwnd)
{
    PAINTSTRUCT ps{};
    HDC dc = ::BeginPaint(hwnd, &ps);

    RECT client{};
    ::GetClientRect(hwnd, &client);

    const UINT dpi = ui::window_dpi(hwnd);
    const auto S = [dpi](int value) {
        return ui::scale_value(value, dpi);
    };

    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    const int margin = S(24);
    const int gap = S(20);
    const int headerHeight = S(92);

    const int cardLeft = margin;
    const int cardRight = width - margin;
    const int accessTop = headerHeight + S(18);
    const int accessHeight = S(230);
    const int qualityTop = accessTop + accessHeight + gap;
    const int qualityHeight = S(205);
    const int statusTop = qualityTop + qualityHeight + gap;
    const int statusBottom = height - margin;

    const int innerLeft = cardLeft + S(28);
    const int innerRight = cardRight - S(28);
    const int columnGap = S(54);
    const int columnWidth =
        std::max(S(260), (innerRight - innerLeft - columnGap) / 2);

    const int leftX = innerLeft + S(10);
    const int rightX = leftX + columnWidth + columnGap;

    ui::fill_rect(dc, client, ui::Bg);
    ui::fill_rect(dc, RECT{0, 0, width, headerHeight}, ui::Header);

    ui::draw_text(
        dc,
        L"Simple Remote Host",
        RECT{S(84), S(16), std::min(width - margin, S(620)), S(50)},
        ui::Text,
        fontTitle_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"Постоянный удалённый доступ к этому компьютеру",
        RECT{S(85), S(50), width - margin, S(78)},
        ui::Muted,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    const RECT accessCard{
        cardLeft, accessTop, cardRight, accessTop + accessHeight};
    const RECT qualityCard{
        cardLeft, qualityTop, cardRight, qualityTop + qualityHeight};
    const RECT statusCard{
        cardLeft, statusTop, cardRight, std::max(statusTop + S(140), statusBottom)};

    ui::fill_round_rect(dc, accessCard, S(18), ui::Surface, ui::BorderSoft);
    ui::fill_round_rect(dc, qualityCard, S(18), ui::Surface, ui::BorderSoft);
    ui::fill_round_rect(dc, statusCard, S(18), ui::Surface, ui::BorderSoft);

    ui::draw_text(
        dc,
        L"Доступ",
        RECT{innerLeft, accessTop + S(18), innerRight, accessTop + S(48)},
        ui::Text,
        fontLarge_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"Порт, пароль и параметры подключения",
        RECT{innerLeft, accessTop + S(48), innerRight, accessTop + S(72)},
        ui::Muted,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"Базовый порт",
        RECT{leftX, accessTop + S(68), leftX + columnWidth, accessTop + S(90)},
        ui::Muted,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"Пароль",
        RECT{rightX, accessTop + S(68), rightX + columnWidth, accessTop + S(90)},
        ui::Muted,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"Качество",
        RECT{innerLeft, qualityTop + S(18), innerRight, qualityTop + S(48)},
        ui::Text,
        fontLarge_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"Частота кадров, качество изображения и системный звук",
        RECT{innerLeft, qualityTop + S(48), innerRight, qualityTop + S(72)},
        ui::Muted,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"FPS",
        RECT{leftX, qualityTop + S(68), leftX + columnWidth, qualityTop + S(90)},
        ui::Muted,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"JPEG качество, %",
        RECT{rightX, qualityTop + S(68), rightX + columnWidth, qualityTop + S(90)},
        ui::Muted,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const bool running = service_.running();
    const bool connected = service_.client_connected();

    HBRUSH dotBrush = ::CreateSolidBrush(
        running ? ui::Success : ui::Offline);
    HPEN nullPen = ::CreatePen(PS_NULL, 0, 0);
    HGDIOBJ oldBrush = ::SelectObject(dc, dotBrush);
    HGDIOBJ oldPen = ::SelectObject(dc, nullPen);

    ::Ellipse(
        dc,
        innerLeft,
        statusTop + S(28),
        innerLeft + S(14),
        statusTop + S(42));

    ::SelectObject(dc, oldBrush);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(dotBrush);
    ::DeleteObject(nullPen);

    ui::draw_text(
        dc,
        running ? L"Host запущен" : L"Host остановлен",
        RECT{
            innerLeft + S(28),
            statusTop + S(15),
            innerRight,
            statusTop + S(52)},
        running ? ui::Success : ui::Offline,
        fontLarge_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        serviceStatus_.c_str(),
        RECT{
            innerLeft + S(28),
            statusTop + S(50),
            innerRight,
            statusTop + S(76)},
        ui::Muted,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    std::wstring details =
        L"Порт: " + std::to_wstring(settings_.port) +
        L"   •   Видео: " + std::to_wstring(settings_.port + 2) +
        L"   •   Звук: " + (settings_.audioEnabled ? L"включён" : L"выключен") +
        L"   •   Клиент: " + (connected ? L"подключён" : L"нет подключений");

    ui::draw_text(
        dc,
        details.c_str(),
        RECT{
            innerLeft,
            statusTop + S(82),
            innerRight,
            std::min(statusBottom - S(8), statusTop + S(112))},
        ui::Text,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    ::EndPaint(hwnd, &ps);
}

void HostWindow::draw_owner_button(const DRAWITEMSTRUCT& item)
{
    const int id = static_cast<int>(item.CtlID);
    COLORREF fill = ui::Surface3;

    if (id == IDC_START) {
        fill = service_.running() ? ui::Surface3 : ui::Accent;
    } else if (id == IDC_DISCONNECT && service_.client_connected()) {
        fill = ui::Danger;
    }

    if (item.itemState & ODS_SELECTED) {
        if (id == IDC_START && !service_.running()) fill = ui::AccentPressed;
        else fill = RGB(
            std::max(0, GetRValue(fill) - 20),
            std::max(0, GetGValue(fill) - 20),
            std::max(0, GetBValue(fill) - 20));
    }

    ui::fill_round_rect(
        item.hDC,
        item.rcItem,
        18,
        fill,
        ui::Border);

    wchar_t text[128]{};
    ::GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));

    ui::draw_text(
        item.hDC,
        text,
        item.rcItem,
        ui::Text,
        fontSemibold_,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void HostWindow::toggle_server()
{
    if (service_.running()) {
        service_.stop();
        update_status_controls();
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    try {
        read_settings_from_controls();
        save_settings();

        HostConfig config;
        config.port = settings_.port;
        config.password = settings_.password;
        config.fps = settings_.fps;
        config.jpegQuality =
            static_cast<float>(settings_.jpegQuality) / 100.0f;
        config.hostId = settings_.hostId;
        config.audioEnabled = settings_.audioEnabled;

        service_.start(std::move(config));
    }
    catch (const std::exception& ex) {
        std::wstring message =
            L"Host не удалось запустить.\n\n";

        std::string text = ex.what();
        message.append(text.begin(), text.end());

        ::MessageBoxW(
            hwnd_,
            message.c_str(),
            L"Simple Remote Host",
            MB_OK | MB_ICONERROR);
    }

    update_status_controls();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void HostWindow::sync_controls_from_settings()
{
    set_text(portEdit_, std::to_wstring(settings_.port));
    set_text(
        passwordEdit_,
        widen_utf8(settings_.password));
    set_text(fpsEdit_, std::to_wstring(settings_.fps));
    set_text(
        qualityEdit_,
        std::to_wstring(settings_.jpegQuality));

    ::SendMessageW(
        audioEnabled_,
        BM_SETCHECK,
        settings_.audioEnabled ? BST_CHECKED : BST_UNCHECKED,
        0);

    ::SendMessageW(
        autostart_,
        BM_SETCHECK,
        settings_.autostart ? BST_CHECKED : BST_UNCHECKED,
        0);
}

void HostWindow::read_settings_from_controls()
{
    settings_.port = static_cast<std::uint16_t>(
        std::clamp(get_uint(portEdit_, 45900), 1024u, 65531u));

    settings_.fps =
        std::clamp(get_uint(fpsEdit_, 30), 1u, 60u);

    settings_.jpegQuality =
        std::clamp(get_uint(qualityEdit_, 90), 40u, 100u);

    const std::wstring password = get_text(passwordEdit_);
    settings_.password = narrow_utf8(password);

    if (settings_.password.size() < 6) {
        throw std::runtime_error("Пароль должен содержать минимум 6 символов.");
    }

    settings_.audioEnabled =
        ::SendMessageW(audioEnabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;

    settings_.autostart =
        ::SendMessageW(autostart_, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void HostWindow::save_settings()
{
    settings::save_host_settings(settings_);
}

void HostWindow::update_service_status(const std::wstring& text)
{
    serviceStatus_ = text;
    update_status_controls();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void HostWindow::update_status_controls()
{
    const bool running = service_.running();
    const bool connected = service_.client_connected();

    ::SetWindowTextW(
        startButton_,
        running ? L"Остановить Host" : L"Запустить Host");

    ::EnableWindow(disconnectButton_, connected ? TRUE : FALSE);
}

std::wstring HostWindow::get_text(HWND control)
{
    const int length = ::GetWindowTextLengthW(control);
    if (length <= 0) return {};

    std::wstring text(static_cast<std::size_t>(length + 1), L'\0');
    ::GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

unsigned int HostWindow::get_uint(HWND control, unsigned int fallback)
{
    const std::wstring text = get_text(control);
    if (text.empty()) return fallback;

    try {
        return static_cast<unsigned int>(std::stoul(text));
    }
    catch (...) {
        return fallback;
    }
}

void HostWindow::set_text(HWND control, const std::wstring& text)
{
    ::SetWindowTextW(control, text.c_str());
}

} // namespace srd::host
