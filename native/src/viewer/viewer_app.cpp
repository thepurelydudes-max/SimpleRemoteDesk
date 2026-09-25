#include "viewer/viewer_app.h"

#include "viewer/home_window.h"
#include "viewer/session_runner.h"

namespace srd::viewer {

void run_viewer_app(
    HINSTANCE instance,
    int showCommand)
{
    for (;;) {
        ViewerHomeWindow home;
        auto selected = home.run(instance, showCommand);

        if (!selected) {
            return;
        }

        if (selected->password.size() < 6) {
            ::MessageBoxW(
                nullptr,
                L"Для выбранного компьютера не сохранён корректный пароль.",
                L"Simple Remote Viewer",
                MB_OK | MB_ICONINFORMATION);

            showCommand = SW_SHOW;
            continue;
        }

        run_live_session(
            instance,
            selected->host,
            selected->password,
            SW_SHOW);

        showCommand = SW_SHOW;
    }
}

} // namespace srd::viewer
