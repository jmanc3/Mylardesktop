#include "titlebar.h"

#include "second.h"
#include "hypriso.h"
#include "events.h"

#include <assert.h>
#include <unistd.h>
#include <math.h>

#define paint [](Container *root, Container *c)
#define fz std::format
#define nz notify

static float ratio_titlebar_button = 1.4375f;
static int titlebar_text_h = 15;
static int titlebar_icon_h = 13;

RGBA color_titlebar_focused() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_focused_color", RGBA("ffffffff")); };
RGBA color_titlebar_unfocused() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_unfocused_color", RGBA("f0f0f0ff")); };
RGBA color_titlebar_text_focused() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_focused_text_color", RGBA("000000ff")); };
RGBA color_titlebar_text_unfocused() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_unfocused_text_color", RGBA("303030ff")); };

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
    self->children[1]->wanted_bounds.w = titlebar_h * s * ratio_titlebar_button;
    self->children[2]->wanted_bounds.w = titlebar_h * s * ratio_titlebar_button;
    self->children[3]->wanted_bounds.w = titlebar_h * s * ratio_titlebar_button;
}

void titlebar_button(Container *root, Container *c) {
    
}

TextureInfo *get_cached_texture(Container *root, Container *target, std::string needle, std::string font, std::string text, RGBA color, int wanted_h) {
    auto [rid, s, stage, active_id] = from_root(root);
    TextureInfo *info = datum<TextureInfo>(target, needle);
    
    int h = std::round(wanted_h * s);
    if (info->id != -1) {
        if (info->cached_color != color || info->cached_h != h || info->cached_text != text) {
            free_text_texture(info->id);
            info->id = -1;
        }
    }
    
    if (info->id == -1) {
        if (info->reattempts_count < 10) {
            auto current = get_current_time_in_ms();
            if (current - info->last_reattempt_time > 1000) {
                info->last_reattempt_time = current;
                info->reattempts_count++;

                auto texture = gen_text_texture(font, text, h, color);
                if (texture.id != -1) {
                    info->id = texture.id;
                    info->w = texture.w;
                    info->h = texture.h;
                    info->cached_color = color;
                    info->cached_h = h;
                    info->cached_text = text;
                }
            }
        }
    }

    return info; 
}

void simple_button(Container *root, Container *c, std::string name, std::string icon) {
    auto [rid, s, stage, active_id] = from_root(root);
    auto client = first_above_of(c, TYPE::CLIENT);
    auto cid = *datum<int>(client, "cid");

    auto b = c->real_bounds;
    if (active_id == cid && stage == (int) STAGE::RENDER_POST_WINDOW) {
        auto focused = get_cached_texture(root, root, name + "_focused", "Segoe Fluent Icons",
            icon, color_titlebar_text_focused(), titlebar_icon_h);
        auto unfocused = get_cached_texture(root, root, name + "min_unfocused", "Segoe Fluent Icons", 
            icon, color_titlebar_text_unfocused(), titlebar_icon_h);
        if (c->state.mouse_pressing) {
            rect(b, {0, 0, 0, 1}, 12, hypriso->get_rounding(cid), 2.0f);
        } else if (c->state.mouse_hovering) {
            rect(b, {1, 0, 0, 1}, 12, hypriso->get_rounding(cid), 2.0f);
        }

        auto texture_info = focused;
        if (!hypriso->has_focus(cid))
            texture_info = unfocused;
        if (texture_info->id != -1) {
            draw_texture(*texture_info,
                c->real_bounds.x + c->real_bounds.w * .5 - texture_info->w * .5,
                c->real_bounds.y + c->real_bounds.h * .5 - texture_info->h * .5, 1.0);
        }
    }
}

void paint_titlebar(Container *root, Container *c) {
    auto [rid, s, stage, active_id] = from_root(root);
    auto client = first_above_of(c, TYPE::CLIENT);
    auto cid = *datum<int>(client, "cid");
    if (active_id == cid && stage == (int) STAGE::RENDER_POST_WINDOW) {
        std::string title_text = hypriso->title_name(cid);
        if (!title_text.empty()) {
            auto focused = get_cached_texture(root, client, "title_focused", "Segoe UI Variable", 
                title_text, color_titlebar_text_focused(), titlebar_text_h);
            auto unfocused = get_cached_texture(root, client, "title_unfocused", "Segoe UI Variable", 
                title_text, color_titlebar_text_unfocused(), titlebar_text_h);

            auto texture_info = focused;
            if (!hypriso->has_focus(cid))
                texture_info = unfocused;
            
            if (texture_info->id != -1) {
                auto overflow = std::max((c->real_bounds.h - texture_info->h), 0.0);
                draw_texture(*texture_info, c->real_bounds.x + overflow, c->real_bounds.y + overflow * .5, 1.0);
            }
        }
    }
}

void create_titlebar(Container *root, Container *parent) {
    auto titlebar_parent = parent->child(::hbox, FILL_SPACE, FILL_SPACE); // actual wanted bounds set in pre_layout
    titlebar_parent->pre_layout = titlebar_pre_layout;
    titlebar_parent->when_paint = paint {
        auto [rid, s, stage, active_id] = from_root(root);
        auto client = first_above_of(c, TYPE::CLIENT);
        auto cid = *datum<int>(client, "cid");
        //notify("repaint");

        auto b = c->real_bounds;
        if (active_id == cid && stage == (int) STAGE::RENDER_POST_WINDOW) {
            auto titlebar_color = color_titlebar_focused();
            if (!hypriso->has_focus(cid))
                titlebar_color = color_titlebar_unfocused();
            rect(b, titlebar_color, 12, hypriso->get_rounding(cid), 2.0f);
        }
    };
    titlebar_parent->receive_events_even_if_obstructed_by_one = true;
    titlebar_parent->when_mouse_motion = request_damage;
    titlebar_parent->when_mouse_enters_container = titlebar_parent->when_mouse_motion;
    titlebar_parent->when_mouse_leaves_container = titlebar_parent->when_mouse_motion;

    auto titlebar = titlebar_parent->child(FILL_SPACE, FILL_SPACE);
    titlebar->when_paint = paint {
        paint_titlebar(root, c);
    };

    titlebar_parent->alignment = ALIGN_RIGHT;
    auto min = titlebar_parent->child(FILL_SPACE, FILL_SPACE);
    min->when_paint = paint {
        simple_button(root, c, "min", "\ue921");
    };
    auto max = titlebar_parent->child(FILL_SPACE, FILL_SPACE);
    max->when_paint = paint {
        bool snapped = false;
        if (snapped) {
            simple_button(root, c, "max", "\ue923");
        } else {
            simple_button(root, c, "max", "\ue922");
        }
    };
    auto close = titlebar_parent->child(FILL_SPACE, FILL_SPACE);
    close->when_paint = paint {
        simple_button(root, c, "close", "\ue8bb");
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
