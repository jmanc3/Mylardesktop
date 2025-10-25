#include "titlebar.h"

#include "second.h"
#include "hypriso.h"

#include <assert.h>

#define paint [](Container *root, Container *c)
#define fz std::format
#define nz notify

void titlebar_pre_layout(Container* root, Container* self, const Bounds& bounds) {
    auto rid = *datum<int>(root, "cid");
    auto cid = *datum<int>(self, "cid");
    auto s = scale(rid);
    self->wanted_bounds.h = titlebar_h * s; 
}

void create_titlebar(Container *root, Container *parent) {
    auto titlebar = parent->child(::hbox, FILL_SPACE, FILL_SPACE); // actual wanted bounds set in pre_layout
    titlebar->pre_layout = titlebar_pre_layout;
    titlebar->skip_delete = true;
    titlebar->user_data = parent;
    titlebar->when_paint = paint {
        auto [rid, s, stage, active_id] = from_root(root);
        auto client = first_above_of(c, TYPE::CLIENT);
        auto cid = *datum<int>(client, "cid");

        auto b = c->real_bounds;
        if (active_id == cid && stage == (int) STAGE::RENDER_POST_WINDOW) {
            rect(b, {1, 1, 1, 1});
        }
    };
}

void titlebar::on_window_open(int id) {
    if (hypriso->wants_titlebar(id)) {
        hypriso->reserve_titlebar(id, titlebar_h);

        for (auto m : monitors) {
            for (auto c : m->children) {
                auto cid = *datum<int>(c, "cid");
                if (cid == id) {
                    create_titlebar(m, c);
                    break;
                }
            }
        }
    }
}

void titlebar::on_window_closed(int id) {
    
}

void titlebar::on_draw_decos(std::string name, int monitor, int id, float a) {
    
}
    
void titlebar::layout_pass() {
    
}
