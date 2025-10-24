#include "titlebar.h"

#include "hypriso.h"

void titlebar::on_window_open(int id) {
    if (hypriso->wants_titlebar(id)) {
        hypriso->reserve_titlebar(id, titlebar_h);
    }
}

void titlebar::on_window_closed(int id) {
    
}

void titlebar::on_draw_decos(std::string name, int monitor, int id, float a) {
    for (auto w : hypriso->windows) {
        if (w->id == id) {
            auto b = bounds(w);
            auto s = scale(get_monitor(id));
            auto rounding = hypriso->get_rounding(id);
            b.y -= titlebar_h;
            b.h = titlebar_h;
            b.scale(s);
            rect(b, {1, 1, 1, 1}, 12, rounding);
        } 
    }
}
    

