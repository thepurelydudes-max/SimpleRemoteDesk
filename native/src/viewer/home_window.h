#pragma once

#include "network/discovery.h"
#include "settings/profiles.h"

#include <windows.h>
#include <optional>
#include <string>
#include <vector>

namespace srd::viewer {

class ViewerHomeWindow {
public:
    ViewerHomeWindow();
    ~ViewerHomeWindow();

    std::optional<settings::ConnectionProfile> run(
        HINSTANCE instance,
        int showCommand = SW_SHOW);

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle_message(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void create_controls(HWND hwnd);
    void layout_controls(HWND hwnd);
    void paint(HWND hwnd);
    void draw_button(const DRAWITEMSTRUCT& item);
    void draw_list_item(const DRAWITEMSTRUCT& item, bool savedList);

    void reload_lists();
    void add_profile();
    void edit_selected_profile();
    void delete_selected_profile();
    void connect_selected(bool discoveredList);
    void on_discovered(network::DiscoveredHost host);
    void prune_discovered();

    int selected_saved_index() const;
    int selected_discovered_index() const;

    bool profile_online(const settings::ConnectionProfile& profile) const;
    bool matches_search(const std::wstring& name, const std::string& address) const;

    HWND hwnd_{nullptr};
    HWND searchEdit_{nullptr};
    HWND savedList_{nullptr};
    HWND discoveredList_{nullptr};
    HWND addButton_{nullptr};
    HWND editButton_{nullptr};
    HWND deleteButton_{nullptr};
    HWND connectButton_{nullptr};

    HFONT font_{nullptr};
    HFONT fontSemibold_{nullptr};
    HFONT fontTitle_{nullptr};
    HFONT fontSection_{nullptr};
    HBRUSH editBrush_{nullptr};

    std::vector<settings::ConnectionProfile> profiles_;
    std::vector<network::DiscoveredHost> discovered_;
    std::vector<std::size_t> savedVisible_;
    std::vector<std::size_t> discoveredVisible_;

    network::DiscoveryListener discovery_;
    std::optional<settings::ConnectionProfile> selectedProfile_;
};

} // namespace srd::viewer
