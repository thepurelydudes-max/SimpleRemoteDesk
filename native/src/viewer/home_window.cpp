#include "viewer/home_window.h"

#include "ui/theme.h"
#include "viewer/profile_dialog.h"

#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <cwctype>
#include <memory>

namespace srd::viewer {

namespace {

constexpr wchar_t kClassName[] = L"SimpleRemoteDeskViewerHome";
constexpr UINT WM_SRD_DISCOVERED = WM_APP + 31;
constexpr UINT_PTR TIMER_PRESENCE = 1;

enum : int {
    IDC_SEARCH = 4001,
    IDC_SAVED_LIST,
    IDC_DISCOVERED_LIST,
    IDC_ADD,
    IDC_EDIT,
    IDC_DELETE,
    IDC_CONNECT
};

std::wstring widen(const std::string& text)
{
    if (text.empty()) return {};

    const int chars = ::MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);

    std::wstring out(static_cast<std::size_t>(chars), L'\0');

    ::MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        out.data(), chars);

    return out;
}

std::wstring lower(std::wstring text)
{
    for (wchar_t& ch : text) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return text;
}

std::wstring control_text(HWND control)
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

}

ViewerHomeWindow::ViewerHomeWindow()
{
    profiles_ = settings::load_profiles();

    font_ = ui::create_font(10);
    fontSemibold_ = ui::create_font(10, FW_SEMIBOLD);
    fontTitle_ = ui::create_font(18, FW_SEMIBOLD);
    fontSection_ = ui::create_font(12, FW_SEMIBOLD);
    editBrush_ = ::CreateSolidBrush(ui::Surface2);
}

ViewerHomeWindow::~ViewerHomeWindow()
{
    discovery_.stop();

    if (editBrush_) ::DeleteObject(editBrush_);
    if (font_) ::DeleteObject(font_);
    if (fontSemibold_) ::DeleteObject(fontSemibold_);
    if (fontTitle_) ::DeleteObject(fontTitle_);
    if (fontSection_) ::DeleteObject(fontSection_);
}

std::optional<settings::ConnectionProfile> ViewerHomeWindow::run(
    HINSTANCE instance,
    int showCommand)
{
    selectedProfile_.reset();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &ViewerHomeWindow::window_proc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;

    if (!::RegisterClassExW(&wc) &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return std::nullopt;
    }

    hwnd_ = ::CreateWindowExW(
        0,
        kClassName,
        L"Simple Remote Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1180,
        760,
        nullptr,
        nullptr,
        instance,
        this);

    if (!hwnd_) return std::nullopt;

    ui::enable_dark_title_bar(hwnd_);

    discovery_.start([this](network::DiscoveredHost host) {
        HWND hwnd = hwnd_;
        if (!hwnd) return;

        auto copy = new network::DiscoveredHost(std::move(host));

        if (!::PostMessageW(
                hwnd,
                WM_SRD_DISCOVERED,
                0,
                reinterpret_cast<LPARAM>(copy))) {
            delete copy;
        }
    });

    ::SetTimer(hwnd_, TIMER_PRESENCE, 2000, nullptr);

    ::ShowWindow(hwnd_, showCommand);
    ::UpdateWindow(hwnd_);

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);

        if (!hwnd_) break;
    }

    discovery_.stop();

    if (hwnd_) {
        ::KillTimer(hwnd_, TIMER_PRESENCE);
    }

    hwnd_ = nullptr;
    return selectedProfile_;
}

LRESULT CALLBACK ViewerHomeWindow::window_proc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    ViewerHomeWindow* self = nullptr;

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ViewerHomeWindow*>(create->lpCreateParams);

        ::SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<ViewerHomeWindow*>(
            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->handle_message(hwnd, message, wParam, lParam);
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT ViewerHomeWindow::handle_message(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        create_controls(hwnd);
        reload_lists();
        return 0;

    case WM_SIZE:
        layout_controls(hwnd);
        return 0;

    case WM_SRD_DISCOVERED: {
        std::unique_ptr<network::DiscoveredHost> host(
            reinterpret_cast<network::DiscoveredHost*>(lParam));

        if (host) on_discovered(std::move(*host));
        return 0;
    }

    case WM_TIMER:
        if (wParam == TIMER_PRESENCE) {
            prune_discovered();
            reload_lists();
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == IDC_SEARCH && code == EN_CHANGE) {
            reload_lists();
            return 0;
        }

        if (id == IDC_ADD && code == BN_CLICKED) {
            add_profile();
            return 0;
        }

        if (id == IDC_EDIT && code == BN_CLICKED) {
            edit_selected_profile();
            return 0;
        }

        if (id == IDC_DELETE && code == BN_CLICKED) {
            delete_selected_profile();
            return 0;
        }

        if (id == IDC_CONNECT && code == BN_CLICKED) {
            if (selected_saved_index() >= 0) connect_selected(false);
            else connect_selected(true);
            return 0;
        }

        if (id == IDC_SAVED_LIST && code == LBN_DBLCLK) {
            connect_selected(false);
            return 0;
        }

        if (id == IDC_DISCOVERED_LIST && code == LBN_DBLCLK) {
            connect_selected(true);
            return 0;
        }

        if ((id == IDC_SAVED_LIST || id == IDC_DISCOVERED_LIST) &&
            code == LBN_SELCHANGE) {
            if (id == IDC_SAVED_LIST) {
                ::SendMessageW(discoveredList_, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
            } else {
                ::SendMessageW(savedList_, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
            }
            return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);

        if (item.CtlID == IDC_SAVED_LIST) {
            draw_list_item(item, true);
            return TRUE;
        }

        if (item.CtlID == IDC_DISCOVERED_LIST) {
            draw_list_item(item, false);
            return TRUE;
        }

        draw_button(item);
        return TRUE;
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        ::SetTextColor(dc, ui::Text);
        ::SetBkColor(dc, ui::Surface2);
        return reinterpret_cast<LRESULT>(editBrush_);
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        ::SetTextColor(dc, ui::Text);
        ::SetBkColor(dc, ui::Bg);
        return reinterpret_cast<LRESULT>(
            ::GetStockObject(NULL_BRUSH));
    }

    case WM_PAINT:
        paint(hwnd);
        return 0;

    case WM_CLOSE:
        ::DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        ::KillTimer(hwnd, TIMER_PRESENCE);
        hwnd_ = nullptr;
        ::PostQuitMessage(0);
        return 0;
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

void ViewerHomeWindow::create_controls(HWND hwnd)
{
    searchEdit_ = ::CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 250, 40,
        hwnd,
        reinterpret_cast<HMENU>(IDC_SEARCH),
        nullptr,
        nullptr);

    ::SendMessageW(searchEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Поиск компьютеров"));

    savedList_ = ::CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"LISTBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            WS_VSCROLL | LBS_NOTIFY |
            LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
        0, 0, 500, 200,
        hwnd,
        reinterpret_cast<HMENU>(IDC_SAVED_LIST),
        nullptr,
        nullptr);

    discoveredList_ = ::CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"LISTBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            WS_VSCROLL | LBS_NOTIFY |
            LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
        0, 0, 500, 200,
        hwnd,
        reinterpret_cast<HMENU>(IDC_DISCOVERED_LIST),
        nullptr,
        nullptr);

    ::SendMessageW(savedList_, LB_SETITEMHEIGHT, 0, 76);
    ::SendMessageW(discoveredList_, LB_SETITEMHEIGHT, 0, 76);

    auto makeButton = [&](int id, const wchar_t* text) {
        HWND button = ::CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 150, 42,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            nullptr,
            nullptr);

        set_font(button, fontSemibold_);
        return button;
    };

    addButton_ = makeButton(IDC_ADD, L"+  Добавить компьютер");
    editButton_ = makeButton(IDC_EDIT, L"Изменить");
    deleteButton_ = makeButton(IDC_DELETE, L"Удалить");
    connectButton_ = makeButton(IDC_CONNECT, L"Подключиться");

    set_font(searchEdit_, font_);
    set_font(savedList_, font_);
    set_font(discoveredList_, font_);

    layout_controls(hwnd);
}

void ViewerHomeWindow::layout_controls(HWND hwnd)
{
    RECT client{};
    ::GetClientRect(hwnd, &client);

    const int width = client.right - client.left;
    const int contentWidth = std::max(700, width - 64);

    ::SetWindowPos(searchEdit_, nullptr, width - 556, 34, 250, 42, SWP_NOZORDER);
    ::SetWindowPos(addButton_, nullptr, width - 288, 34, 224, 42, SWP_NOZORDER);

    ::SetWindowPos(savedList_, nullptr, 32, 180, contentWidth, 202, SWP_NOZORDER);
    ::SetWindowPos(discoveredList_, nullptr, 32, 470, contentWidth, 202, SWP_NOZORDER);

    ::SetWindowPos(editButton_, nullptr, 32, 394, 132, 42, SWP_NOZORDER);
    ::SetWindowPos(deleteButton_, nullptr, 174, 394, 132, 42, SWP_NOZORDER);
    ::SetWindowPos(connectButton_, nullptr, width - 224, 394, 192, 42, SWP_NOZORDER);
}

void ViewerHomeWindow::paint(HWND hwnd)
{
    PAINTSTRUCT ps{};
    HDC dc = ::BeginPaint(hwnd, &ps);

    RECT client{};
    ::GetClientRect(hwnd, &client);

    ui::fill_rect(dc, client, ui::Bg);
    ui::fill_rect(dc, RECT{0, 0, client.right, 112}, ui::Header);

    ui::draw_text(
        dc,
        L"Simple Remote Viewer",
        RECT{98, 22, 520, 54},
        ui::Text,
        fontTitle_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"Удалённый доступ к вашим компьютерам",
        RECT{99, 56, 560, 82},
        ui::Muted,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"Мои компьютеры",
        RECT{32, 132, 400, 166},
        ui::Text,
        fontSection_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"Доступные в сети",
        RECT{32, 438, 400, 466},
        ui::Text,
        fontSection_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ui::draw_text(
        dc,
        L"Локальная сеть и VPN",
        RECT{240, 438, 520, 466},
        ui::Muted,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ::EndPaint(hwnd, &ps);
}

void ViewerHomeWindow::draw_button(const DRAWITEMSTRUCT& item)
{
    const bool primary =
        item.CtlID == IDC_ADD ||
        item.CtlID == IDC_CONNECT;

    const bool danger = item.CtlID == IDC_DELETE;

    COLORREF fill =
        danger ? ui::Danger :
        primary ? ui::Accent :
        ui::Surface3;

    if (item.itemState & ODS_SELECTED) {
        fill = primary ? ui::AccentPressed : ui::Surface2;
    }

    ui::fill_round_rect(
        item.hDC,
        item.rcItem,
        16,
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

void ViewerHomeWindow::draw_list_item(
    const DRAWITEMSTRUCT& item,
    bool savedList)
{
    if (item.itemID == static_cast<UINT>(-1)) return;

    const auto& visible = savedList ? savedVisible_ : discoveredVisible_;
    if (item.itemID >= visible.size()) return;

    RECT row = item.rcItem;
    ui::fill_rect(item.hDC, row, ui::Bg);

    RECT card{
        row.left + 6,
        row.top + 5,
        row.right - 6,
        row.bottom - 5
    };

    const bool selected =
        (item.itemState & ODS_SELECTED) != 0;

    ui::fill_round_rect(
        item.hDC,
        card,
        16,
        selected ? ui::Surface3 : ui::Surface,
        selected ? ui::Border : ui::BorderSoft);

    std::wstring name;
    std::wstring address;
    bool online = true;

    if (savedList) {
        const auto& profile = profiles_[visible[item.itemID]];
        name = profile.name;
        address =
            widen(profile.host) + L":" +
            std::to_wstring(profile.port);
        online = profile_online(profile);
    } else {
        const auto& host = discovered_[visible[item.itemID]];
        name = host.name;
        address =
            widen(host.address) + L":" +
            std::to_wstring(host.port);
    }

    RECT nameRect{
        card.left + 22,
        card.top + 10,
        card.right - 150,
        card.top + 36
    };

    ui::draw_text(
        item.hDC,
        name.c_str(),
        nameRect,
        ui::Text,
        fontSemibold_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT addressRect{
        card.left + 22,
        card.top + 38,
        card.right - 150,
        card.bottom - 8
    };

    ui::draw_text(
        item.hDC,
        address.c_str(),
        addressRect,
        ui::Muted,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    HBRUSH dot = ::CreateSolidBrush(
        online ? ui::Success : ui::Offline);

    HGDIOBJ oldBrush = ::SelectObject(item.hDC, dot);
    HPEN pen = ::CreatePen(PS_NULL, 0, 0);
    HGDIOBJ oldPen = ::SelectObject(item.hDC, pen);

    ::Ellipse(
        item.hDC,
        card.right - 118,
        card.top + 25,
        card.right - 106,
        card.top + 37);

    ::SelectObject(item.hDC, oldBrush);
    ::SelectObject(item.hDC, oldPen);
    ::DeleteObject(dot);
    ::DeleteObject(pen);

    ui::draw_text(
        item.hDC,
        online ? L"В сети" : L"Не в сети",
        RECT{
            card.right - 96,
            card.top + 17,
            card.right - 14,
            card.top + 45},
        online ? ui::Success : ui::Offline,
        font_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void ViewerHomeWindow::reload_lists()
{
    if (!savedList_ || !discoveredList_) return;

    savedVisible_.clear();
    discoveredVisible_.clear();

    ::SendMessageW(savedList_, LB_RESETCONTENT, 0, 0);
    ::SendMessageW(discoveredList_, LB_RESETCONTENT, 0, 0);

    for (std::size_t i = 0; i < profiles_.size(); ++i) {
        if (!matches_search(profiles_[i].name, profiles_[i].host)) continue;

        savedVisible_.push_back(i);
        ::SendMessageW(savedList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(profiles_[i].name.c_str()));
    }

    for (std::size_t i = 0; i < discovered_.size(); ++i) {
        if (!matches_search(discovered_[i].name, discovered_[i].address)) continue;

        discoveredVisible_.push_back(i);
        ::SendMessageW(discoveredList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(discovered_[i].name.c_str()));
    }
}

void ViewerHomeWindow::add_profile()
{
    settings::ConnectionProfile profile;
    profile.id = settings::generate_profile_id();

    if (edit_profile(hwnd_, profile)) {
        profiles_.push_back(std::move(profile));
        settings::save_profiles(profiles_);
        reload_lists();
    }
}

void ViewerHomeWindow::edit_selected_profile()
{
    const int selected = selected_saved_index();
    if (selected < 0) return;

    const std::size_t profileIndex = savedVisible_[selected];
    auto copy = profiles_[profileIndex];

    if (edit_profile(hwnd_, copy)) {
        profiles_[profileIndex] = std::move(copy);
        settings::save_profiles(profiles_);
        reload_lists();
    }
}

void ViewerHomeWindow::delete_selected_profile()
{
    const int selected = selected_saved_index();
    if (selected < 0) return;

    if (::MessageBoxW(
            hwnd_,
            L"Удалить выбранный компьютер из списка?",
            L"Simple Remote Viewer",
            MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    const std::size_t profileIndex = savedVisible_[selected];

    profiles_.erase(
        profiles_.begin() +
        static_cast<std::ptrdiff_t>(profileIndex));

    settings::save_profiles(profiles_);
    reload_lists();
}

void ViewerHomeWindow::connect_selected(bool discoveredList)
{
    if (!discoveredList) {
        const int selected = selected_saved_index();
        if (selected < 0) return;

        auto profile = profiles_[savedVisible_[selected]];

        if (profile.password.size() < 6) {
            if (!edit_profile(hwnd_, profile)) return;

            profiles_[savedVisible_[selected]] = profile;
            settings::save_profiles(profiles_);
        }

        selectedProfile_ = std::move(profile);
        ::DestroyWindow(hwnd_);
        return;
    }

    const int selected = selected_discovered_index();
    if (selected < 0) return;

    const auto host = discovered_[discoveredVisible_[selected]];

    for (auto& profile : profiles_) {
        if (!host.hostId.empty() &&
            profile.hostId == host.hostId) {
            profile.host = host.address;
            profile.port = host.port;
            settings::save_profiles(profiles_);
            selectedProfile_ = profile;
            ::DestroyWindow(hwnd_);
            return;
        }
    }

    settings::ConnectionProfile profile;
    profile.id = settings::generate_profile_id();
    profile.hostId = host.hostId;
    profile.name = host.name;
    profile.host = host.address;
    profile.port = host.port;

    if (!edit_profile(hwnd_, profile, host)) return;

    profiles_.push_back(profile);
    settings::save_profiles(profiles_);

    selectedProfile_ = std::move(profile);
    ::DestroyWindow(hwnd_);
}

void ViewerHomeWindow::on_discovered(network::DiscoveredHost host)
{
    auto it = std::find_if(
        discovered_.begin(),
        discovered_.end(),
        [&](const network::DiscoveredHost& existing) {
            if (!host.hostId.empty() &&
                existing.hostId == host.hostId &&
                existing.address == host.address) {
                return true;
            }

            return host.hostId.empty() &&
                existing.address == host.address &&
                existing.port == host.port;
        });

    if (it == discovered_.end()) {
        discovered_.push_back(std::move(host));
    } else {
        *it = std::move(host);
    }

    reload_lists();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void ViewerHomeWindow::prune_discovered()
{
    const std::uint64_t now = ::GetTickCount64();

    discovered_.erase(
        std::remove_if(
            discovered_.begin(),
            discovered_.end(),
            [&](const network::DiscoveredHost& host) {
                return now > host.lastSeenTick &&
                    now - host.lastSeenTick > 7000;
            }),
        discovered_.end());
}

int ViewerHomeWindow::selected_saved_index() const
{
    if (!savedList_) return -1;
    return static_cast<int>(
        ::SendMessageW(savedList_, LB_GETCURSEL, 0, 0));
}

int ViewerHomeWindow::selected_discovered_index() const
{
    if (!discoveredList_) return -1;
    return static_cast<int>(
        ::SendMessageW(discoveredList_, LB_GETCURSEL, 0, 0));
}

bool ViewerHomeWindow::profile_online(
    const settings::ConnectionProfile& profile) const
{
    const std::uint64_t now = ::GetTickCount64();

    return std::any_of(
        discovered_.begin(),
        discovered_.end(),
        [&](const network::DiscoveredHost& host) {
            const bool fresh =
                now >= host.lastSeenTick &&
                now - host.lastSeenTick <= 7000;

            if (!fresh) return false;

            if (!profile.hostId.empty() &&
                !host.hostId.empty()) {
                return profile.hostId == host.hostId;
            }

            return profile.host == host.address &&
                profile.port == host.port;
        });
}

bool ViewerHomeWindow::matches_search(
    const std::wstring& name,
    const std::string& address) const
{
    const std::wstring query = lower(control_text(searchEdit_));
    if (query.empty()) return true;

    const std::wstring target =
        lower(name + L" " + widen(address));

    return target.find(query) != std::wstring::npos;
}

} // namespace srd::viewer
