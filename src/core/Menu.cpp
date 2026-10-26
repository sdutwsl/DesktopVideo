#include "Menu.h"
#include "Wallpaper.h"

namespace muralop {

std::vector<MenuItem> buildAppMenu(Wallpaper& wallpaper, const MenuActions& actions)
{
    std::vector<MenuItem> menu;
    const std::vector<MonitorInfo> monitors = wallpaper.monitors();

    MenuItem choose;
    choose.label = monitors.size() > 1 ? "选择视频（所有显示器）…" : "选择视频…";
    choose.activate = [=] { actions.chooseVideo(""); };
    menu.push_back(choose);

    if (monitors.size() > 1) {
        for (const MonitorInfo& m : monitors) {
            MenuItem sub;
            sub.label = m.id + "  " + std::to_string(m.width) + "×" + std::to_string(m.height) + (m.primary ? "  ♪" : "");

            MenuItem pick;
            pick.label = "选择视频…";
            pick.activate = [=] { actions.chooseVideo(m.id); };
            sub.children.push_back(pick);

            MenuItem reset;
            reset.label = "使用默认视频";
            reset.enabled = wallpaper.hasOverride(m.id);
            reset.activate = [&wallpaper, id = m.id] { wallpaper.clearOverride(id); };
            sub.children.push_back(reset);

            menu.push_back(sub);
        }
    }

    menu.push_back(MenuItem::Separator());

    MenuItem pause;
    pause.label = "暂停";
    pause.checkable = true;
    pause.checked = wallpaper.userPaused();
    pause.activate = [&wallpaper] { wallpaper.setUserPaused(!wallpaper.userPaused()); };
    menu.push_back(pause);

    MenuItem mute;
    mute.label = "静音";
    mute.checkable = true;
    mute.checked = wallpaper.mute();
    mute.activate = [&wallpaper] { wallpaper.setMute(!wallpaper.mute()); };
    menu.push_back(mute);

    menu.push_back(MenuItem::Separator());

    MenuItem quit;
    quit.label = "退出";
    quit.activate = actions.quit;
    menu.push_back(quit);

    return menu;
}

} // namespace muralop
