
#include "splash.h"

#include "second.h"

static bool showing = false;

void splash::input() {
    if (showing) {
        for (auto m : actual_monitors) {
            hypriso->damage_entire(*datum<int>(m, "cid"));
        }
    }
    showing = false;
}

void splash::render(int id, int stage) {
    int current_monitor = current_rendering_monitor();
    int current_window = current_rendering_window();
    int active_id = current_window == -1 ? current_monitor : current_window;

    if (showing && stage == (int) STAGE::RENDER_LAST_MOMENT) {
        auto b = bounds_monitor(current_monitor);
        b.x = 0;
        b.y = 0;
        b.scale(scale(current_monitor));
        rect(b, {0, 0, 0, 1});
    } 
}

