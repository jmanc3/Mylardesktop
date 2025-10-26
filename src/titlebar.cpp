#include "titlebar.h"

#include "second.h"
#include "hypriso.h"
#include "events.h"

#include <assert.h>

#define paint [](Container *root, Container *c)
#define fz std::format
#define nz notify

static float ratio_titlebar_button = 1.4375f;

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
    self->children[0]->wanted_bounds.w = titlebar_h * s * ratio_titlebar_button;
    self->children[1]->wanted_bounds.w = titlebar_h * s * ratio_titlebar_button;
    self->children[2]->wanted_bounds.w = titlebar_h * s * ratio_titlebar_button;
}

void titlebar_button(Container *root, Container *c) {
    
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
        }
    };
    titlebar->receive_events_even_if_obstructed_by_one = true;
    titlebar->when_mouse_motion = request_damage;
    titlebar->when_mouse_enters_container = titlebar->when_mouse_motion;
    titlebar->when_mouse_leaves_container = titlebar->when_mouse_motion;

    titlebar->alignment = ALIGN_RIGHT;
    auto min = titlebar->child(FILL_SPACE, FILL_SPACE);
    min->when_paint = paint {
        auto [rid, s, stage, active_id] = from_root(root);
        auto client = first_above_of(c, TYPE::CLIENT);
        auto cid = *datum<int>(client, "cid");

        auto b = c->real_bounds;
        if (active_id == cid && stage == (int) STAGE::RENDER_POST_WINDOW) {
            //TextureInfo main = gen_text_texture("Segoe Fluent Icons", "\ue921",
                //titlebar_icon_button_h * s, color_titlebar_icon);

            
            if (c->state.mouse_pressing) {
                rect(b, {0, 0, 0, 1}, 12, hypriso->get_rounding(cid), 2.0f);
            } else if (c->state.mouse_hovering) {
                rect(b, {1, 0, 0, 1}, 12, hypriso->get_rounding(cid), 2.0f);
            }
        }
    };
    auto max = titlebar->child(FILL_SPACE, FILL_SPACE);
    max->when_paint = min->when_paint;
    auto close = titlebar->child(FILL_SPACE, FILL_SPACE);
    close->when_paint = min->when_paint;
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
