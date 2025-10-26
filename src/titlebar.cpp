#include "titlebar.h"

#include "second.h"
#include "hypriso.h"
#include "events.h"

#include <assert.h>

#define paint [](Container *root, Container *c)
#define fz std::format
#define nz notify

void request_damage(Container *root, Container *c) {
    auto [rid, s, stage, active_id] = from_root(root);
    auto b = c->real_bounds;
    b.scale(1.0 / s);
    hypriso->damage_box(b);
}

void titlebar_pre_layout(Container* root, Container* self, const Bounds& bounds) {
    auto rid = *datum<int>(root, "cid");
    auto cid = *datum<int>(self, "cid");
    auto s = scale(rid);
    self->wanted_bounds.h = titlebar_h * s; 
}

void create_titlebar(Container *root, Container *parent) {
    auto titlebar = parent->child(::hbox, FILL_SPACE, FILL_SPACE); // actual wanted bounds set in pre_layout
    titlebar->pre_layout = titlebar_pre_layout;
    titlebar->when_paint = paint {
        auto [rid, s, stage, active_id] = from_root(root);
        auto client = first_above_of(c, TYPE::CLIENT);
        auto cid = *datum<int>(client, "cid");
        //notify("repaint");

        auto b = c->real_bounds;
        if (active_id == cid && stage == (int) STAGE::RENDER_POST_WINDOW) {
            rect(b, {.8, .8, .8, 1}, 12, hypriso->get_rounding(cid), 2.0f);

            /*if (c->state.mouse_hovering) {
                auto curs = mouse();
                curs.x -= 20;
                curs.y -= 20;
                curs.scale(s);
                curs.w = 10;
                curs.h = 10;
                rect(curs, {.1, .1, .1, 1});
            }*/
        }
    };
    titlebar->when_mouse_motion = request_damage;
    titlebar->when_mouse_enters_container = titlebar->when_mouse_motion;
    titlebar->when_mouse_leaves_container = titlebar->when_mouse_motion;
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
    for (auto m : monitors) {
       for (auto c : m->children) {
           if (c->custom_type == (int) TYPE::CLIENT) {
               auto cid = *datum<int>(c, "cid");
               if (cid == id) {
                   paint_outline(m, c);
               }
           } 
       } 
    }
}
    
void titlebar::layout_pass() {
    
}
