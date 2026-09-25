#include "viewer/profile_dialog.h"

#include "ui/theme.h"
#include "ui/dpi.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace srd::viewer {

namespace {

constexpr wchar_t kClassName[] = L"SimpleRemoteDeskProfileDialog";

enum : int {
    IDC_NAME = 3001,
    IDC_HOST,
    IDC_PORT,
    IDC_PASSWORD,
    IDC_SHOW_PASSWORD,
    IDC_OK,
    IDC_CANCEL
};

struct DialogState {
    settings::ConnectionProfile* profile{};
    std::optional<network::DiscoveredHost> suggested;
    bool accepted{false};

    HWND hwnd{};
    HWND nameEdit{};
    HWND hostEdit{};
    HWND portEdit{};
    HWND passwordEdit{};
    HWND showPassword{};
    HWND okButton{};
    HWND cancelButton{};

    HFONT font{};
    HFONT fontSemibold{};
    HFONT fontTitle{};
    HBRUSH editBrush{};
};

std::wstring widen(const std::string& text)
{
    if (text.empty()) return {};

    const int chars = ::MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0);

    std::wstring out(static_cast<std::size_t>(chars), L'\0');

    ::MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        out.data(), chars);

    return out;
}

std::string narrow(const std::wstring& text)
{
    if (text.empty()) return {};

    const int bytes = ::WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);

    std::string out(static_cast<std::size_t>(bytes), '\0');

    ::WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        out.data(), bytes, nullptr, nullptr);

    return out;
}

std::wstring get_text(HWND control)
{
    const int length = ::GetWindowTextLengthW(control);
    if (length <= 0) return {};

    std::wstring out(static_cast<std::size_t>(length + 1), L'\0');
    ::GetWindowTextW(control, out.data(), length + 1);
    out.resize(static_cast<std::size_t>(length));
    return out;
}

void set_font(HWND control, HFONT font)
{
    ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void draw_button(
    DialogState* state,
    const DRAWITEMSTRUCT& item)
{
    const bool primary = item.CtlID == IDC_OK;
    COLORREF fill = primary ? ui::Accent : ui::Surface3;

    if (item.itemState & ODS_SELECTED) {
        fill = primary ? ui::AccentPressed : ui::Surface2;
    }

    ui::fill_round_rect(
        item.hDC,
        item.rcItem,
        16,
        fill,
        ui::Border);

    wchar_t text[64]{};
    ::GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));

    ui::draw_text(
        item.hDC,
        text,
        item.rcItem,
        ui::Text,
        state->fontSemibold,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void create_controls(DialogState* state)
{
    HWND hwnd = state->hwnd;
    const UINT dpi = ui::window_dpi(hwnd);
    const auto S = [dpi](int value) {
        return ui::scale_value(value, dpi);
    };

    auto makeEdit = [&](int id, DWORD style) {
        HWND control = ::CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                ES_AUTOHSCROLL | style,
            0, 0, S(100), S(32),
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            nullptr,
            nullptr);

        set_font(control, state->font);
        return control;
    };

    state->nameEdit = makeEdit(IDC_NAME, 0);
    state->hostEdit = makeEdit(IDC_HOST, 0);
    state->portEdit = makeEdit(IDC_PORT, ES_NUMBER);
    state->passwordEdit = makeEdit(IDC_PASSWORD, ES_PASSWORD);

    state->showPassword = ::CreateWindowExW(
        0, L"BUTTON", L"Показать пароль",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, S(180), S(28),
        hwnd,
        reinterpret_cast<HMENU>(IDC_SHOW_PASSWORD),
        nullptr,
        nullptr);

    state->okButton = ::CreateWindowExW(
        0, L"BUTTON",
        state->profile->host.empty() ? L"Добавить" : L"Сохранить",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, S(144), S(42),
        hwnd,
        reinterpret_cast<HMENU>(IDC_OK),
        nullptr,
        nullptr);

    state->cancelButton = ::CreateWindowExW(
        0, L"BUTTON", L"Отмена",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, S(144), S(42),
        hwnd,
        reinterpret_cast<HMENU>(IDC_CANCEL),
        nullptr,
        nullptr);

    for (HWND control : {
        state->showPassword,
        state->okButton,
        state->cancelButton}) {
        set_font(control, state->fontSemibold);
    }

    ::SendMessageW(
        state->passwordEdit,
        EM_SETPASSWORDCHAR,
        static_cast<WPARAM>(L'●'),
        0);

    settings::ConnectionProfile initial = *state->profile;

    if (state->suggested) {
        if (initial.name.empty() || initial.name == L"Компьютер")
            initial.name = state->suggested->name;

        if (initial.host.empty())
            initial.host = state->suggested->address;

        if (initial.hostId.empty())
            initial.hostId = state->suggested->hostId;

        initial.port = state->suggested->port;
    }

    ::SetWindowTextW(state->nameEdit, initial.name.c_str());
    ::SetWindowTextW(state->hostEdit, widen(initial.host).c_str());
    ::SetWindowTextW(state->portEdit, std::to_wstring(initial.port).c_str());
    ::SetWindowTextW(state->passwordEdit, widen(initial.password).c_str());

    const int labelWidth = S(132);
    const int editLeft = S(190);
    const int editWidth = S(420);
    const int editHeight = S(34);

    ::SetWindowPos(state->nameEdit, nullptr, editLeft, S(150), editWidth, editHeight, SWP_NOZORDER);
    ::SetWindowPos(state->hostEdit, nullptr, editLeft, S(210), editWidth, editHeight, SWP_NOZORDER);
    ::SetWindowPos(state->portEdit, nullptr, editLeft, S(270), S(180), editHeight, SWP_NOZORDER);
    ::SetWindowPos(state->passwordEdit, nullptr, editLeft, S(330), editWidth, editHeight, SWP_NOZORDER);
    ::SetWindowPos(state->showPassword, nullptr, editLeft, S(374), S(190), S(28), SWP_NOZORDER);
    ::SetWindowPos(state->cancelButton, nullptr, S(302), S(440), S(144), S(42), SWP_NOZORDER);
    ::SetWindowPos(state->okButton, nullptr, S(466), S(440), S(144), S(42), SWP_NOZORDER);

    (void)labelWidth;
}

void paint(DialogState* state)
{
    PAINTSTRUCT ps{};
    HDC dc = ::BeginPaint(state->hwnd, &ps);

    RECT client{};
    ::GetClientRect(state->hwnd, &client);

    const UINT dpi = ui::window_dpi(state->hwnd);
    const auto S = [dpi](int value) {
        return ui::scale_value(value, dpi);
    };

    ui::fill_rect(dc, client, ui::Bg);
    ui::fill_rect(dc, RECT{0, 0, client.right, S(96)}, ui::Header);

    ui::draw_text(
        dc,
        state->profile->host.empty()
            ? L"Добавить компьютер"
            : L"Параметры подключения",
        RECT{S(72), S(18), client.right - S(32), S(50)},
        ui::Text,
        state->fontTitle,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"Укажите адрес Simple Remote Host и пароль доступа",
        RECT{S(73), S(52), client.right - S(32), S(78)},
        ui::Muted,
        state->font,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    ui::fill_round_rect(
        dc,
        RECT{S(24), S(116), client.right - S(24), client.bottom - S(24)},
        S(18),
        ui::Surface,
        ui::BorderSoft);

    const wchar_t* labels[] = {
        L"Название",
        L"IP / Host",
        L"Базовый порт",
        L"Пароль"
    };

    const int tops[] = {150, 210, 270, 330};

    for (int i = 0; i < 4; ++i) {
        ui::draw_text(
            dc,
            labels[i],
            RECT{S(50), S(tops[i]), S(176), S(tops[i] + 34)},
            ui::Muted,
            state->font,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    ::EndPaint(state->hwnd, &ps);
}

LRESULT CALLBACK dialog_proc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    auto* state = reinterpret_cast<DialogState*>(
        ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<DialogState*>(create->lpCreateParams);
        state->hwnd = hwnd;

        ::SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
    }

    if (!state) return ::DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE:
        create_controls(state);
        ui::enable_dark_title_bar(hwnd);
        return 0;

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == IDC_SHOW_PASSWORD && code == BN_CLICKED) {
            const bool checked =
                ::SendMessageW(
                    state->showPassword,
                    BM_GETCHECK,
                    0,
                    0) == BST_CHECKED;

            ::SendMessageW(
                state->passwordEdit,
                EM_SETPASSWORDCHAR,
                checked ? 0 : static_cast<WPARAM>(L'●'),
                0);

            ::InvalidateRect(state->passwordEdit, nullptr, TRUE);
            return 0;
        }

        if (id == IDC_CANCEL && code == BN_CLICKED) {
            ::DestroyWindow(hwnd);
            return 0;
        }

        if (id == IDC_OK && code == BN_CLICKED) {
            const std::wstring name = get_text(state->nameEdit);
            const std::wstring host = get_text(state->hostEdit);
            const std::wstring portText = get_text(state->portEdit);
            const std::wstring password = get_text(state->passwordEdit);

            if (name.empty() || host.empty()) {
                ::MessageBoxW(
                    hwnd,
                    L"Укажите название и адрес компьютера.",
                    L"Параметры подключения",
                    MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            if (password.size() < 6) {
                ::MessageBoxW(
                    hwnd,
                    L"Пароль должен содержать хотя бы 6 символов.",
                    L"Параметры подключения",
                    MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            unsigned long port = 45900;
            try {
                port = std::stoul(portText);
            }
            catch (...) {
                port = 45900;
            }

            port = std::clamp<unsigned long>(port, 1024, 65531);

            state->profile->name = name;
            state->profile->host = narrow(host);
            state->profile->port = static_cast<std::uint16_t>(port);
            state->profile->password = narrow(password);

            if (state->profile->id.empty()) {
                state->profile->id = settings::generate_profile_id();
            }

            if (state->suggested && state->profile->hostId.empty()) {
                state->profile->hostId = state->suggested->hostId;
            }

            state->accepted = true;
            ::DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_DRAWITEM:
        draw_button(
            state,
            *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        ::SetTextColor(dc, ui::Text);
        ::SetBkColor(dc, ui::Surface2);
        return reinterpret_cast<LRESULT>(state->editBrush);
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        ::SetTextColor(dc, ui::Text);
        ::SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(
            ::GetStockObject(NULL_BRUSH));
    }

    case WM_PAINT:
        paint(state);
        return 0;

    case WM_CLOSE:
        ::DestroyWindow(hwnd);
        return 0;
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

}

bool edit_profile(
    HWND owner,
    settings::ConnectionProfile& profile,
    const std::optional<network::DiscoveredHost>& suggested)
{
    static bool registered = false;

    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &dialog_proc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kClassName;

        if (!::RegisterClassExW(&wc) &&
            ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        registered = true;
    }

    DialogState state;
    state.profile = &profile;
    state.suggested = suggested;
    state.font = ui::create_font(10);
    state.fontSemibold = ui::create_font(10, FW_SEMIBOLD);
    state.fontTitle = ui::create_font(16, FW_SEMIBOLD);
    state.editBrush = ::CreateSolidBrush(ui::Surface2);

    const UINT dpi = ui::system_dpi();
    RECT rect{
        0,
        0,
        ui::scale_value(660, dpi),
        ui::scale_value(540, dpi)};

    ::AdjustWindowRectEx(
        &rect,
        WS_CAPTION | WS_SYSMENU,
        FALSE,
        WS_EX_DLGMODALFRAME);

    HWND hwnd = ::CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kClassName,
        profile.host.empty()
            ? L"Добавить компьютер"
            : L"Изменить компьютер",
        WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        owner,
        nullptr,
        ::GetModuleHandleW(nullptr),
        &state);

    if (!hwnd) {
        ::DeleteObject(state.font);
        ::DeleteObject(state.fontSemibold);
        ::DeleteObject(state.fontTitle);
        ::DeleteObject(state.editBrush);
        return false;
    }

    if (owner) ::EnableWindow(owner, FALSE);

    ::ShowWindow(hwnd, SW_SHOW);
    ::UpdateWindow(hwnd);

    MSG msg{};
    bool quitReceived = false;

    while (::IsWindow(hwnd)) {
        const BOOL result = ::GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
            quitReceived = result == 0;
            break;
        }

        if (!::IsDialogMessageW(hwnd, &msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    if (owner) {
        ::EnableWindow(owner, TRUE);
        ::SetForegroundWindow(owner);
    }

    ::DeleteObject(state.font);
    ::DeleteObject(state.fontSemibold);
    ::DeleteObject(state.fontTitle);
    ::DeleteObject(state.editBrush);

    if (quitReceived) {
        ::PostQuitMessage(static_cast<int>(msg.wParam));
    }

    return state.accepted;
}

} // namespace srd::viewer
