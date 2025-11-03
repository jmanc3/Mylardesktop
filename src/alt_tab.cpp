
#include "alt_tab.h"

#include "second.h"

#define LAST_TIME_ACTIVE "last_time_active"

void alt_tab::on_window_open(int id) {
    Container *c = get_cid_container(id);

    assert(c && "alt_tab::on_window_open assumes Container for id has already been created");
    
    *datum<long>(c, LAST_TIME_ACTIVE) = get_current_time_in_ms();
}

void alt_tab::on_window_closed(int id) {
    
}

void fill_root(Container *root, Container *alt_tab_parent) {
    *datum<bool>(alt_tab_parent, "shown_yet") = false;
    alt_tab_parent->custom_type = (int) TYPE::ALT_TAB;
    alt_tab_parent->type = ::vbox;
    //alt_tab_parent->receive_events_even_if_obstructed = true;
    //alt_tab_parent->when_mouse_down = consume_event;
    //alt_tab_parent->when_mouse_motion = consume_event;
    //alt_tab_parent->when_drag = consume_event;
    //alt_tab_parent->when_mouse_up = consume_event;
    alt_tab_parent->pre_layout = [](Container *root, Container *c, const Bounds &b) {
        auto [rid, s, stage, active_id] = from_root(root);

        if (c->children.empty()) {
            auto windows = get_window_stacking_order();
            for (auto w : windows) {
                auto child = c->child(FILL_SPACE, 20 * s);
                *datum<int>(child, "cid") = w;
                child->when_paint = paint {
                    auto [rid, s, stage, active_id] = from_root(root);

                    if (stage != (int) STAGE::RENDER_LAST_MOMENT) {
                        return;
                    }
                    
                    auto b = c->real_bounds;
                    auto w = *datum<int>(c, "cid");
                    
                    b.shrink(2 * s);
                    if (c->state.mouse_pressing) {
                        rect(b, {1, 1, 1, 1});
                    } else if (c->state.mouse_hovering) {
                        rect(b, {1, 0, 1, 1});
                    }
                    rect(b, {0, 0, 0, 1});

                    auto info = datum<TextureInfo>(c, "title");
                    auto title = hypriso->title_name(w);
                    if (title != info->cached_text) {
                        free_text_texture(info->id);
                        info->id = -1;
                    }
                    if (info->id == -1 && !title.empty()) {
                        auto new_gen = gen_text_texture("Segoe UI", hypriso->title_name(w), 14 * s, {1, 1, 1, 1});
                        info->id = new_gen.id;
                        info->w = new_gen.w;
                        info->h = new_gen.h;
                        info->cached_text = title;
                    }

                    if (info->id != -1) {
                        draw_texture(*info, c->real_bounds.x, c->real_bounds.y);
                    }
                };
                child->when_clicked = paint {
                    auto w = *datum<int>(c, "cid");
                    later_immediate([w](Timer *) {
                        alt_tab::close(false);
                        hypriso->bring_to_front(w);
                    });
                };
            }
        }

        c->wanted_bounds = Bounds(center_x(root, 600 *s), center_y(root, 450 * s), 600 * s, 450 *s);
        c->real_bounds = c->wanted_bounds;
        ::layout(root, c, c->real_bounds);
    };
    alt_tab_parent->when_paint = paint {
        auto [rid, s, stage, active_id] = from_root(root);
        if (stage != (int) STAGE::RENDER_POST_WINDOWS)
            return;
        bool any_subpart_damaged = false;
        auto shown_yet = datum<bool>(c, "shown_yet");
        if (any_subpart_damaged || !(*shown_yet)) {
            request_damage(root, c); // request full redamage
        }
        *shown_yet = true;
        if (c->state.mouse_pressing) {
            rect(c->real_bounds, {1, 1, 1, 1});
        } else if (c->state.mouse_hovering) {
            rect(c->real_bounds, {1, 0, 1, 1});
        }
 
        //rect(c->real_bounds, {1, 1, 1, .4}, 20);
    };
}

static bool showing = false;

void alt_tab::show() {
    showing = true;
    later_immediate([](Timer *) {
        hypriso->screenshot_all(); 
    });
    for (auto m : monitors) {
        auto alt_tab_parent = m->child(FILL_SPACE, FILL_SPACE);
        fill_root(m, alt_tab_parent);
        break;
    }
}

void alt_tab::close(bool focus) {
    if (!showing)
        return;
    for (auto m : monitors) {
        for (int i = m->children.size() - 1; i >= 0; i--) {
            auto c = m->children[i];
            if (c->custom_type == (int) TYPE::ALT_TAB) {
                request_damage(m, c);
                delete c;
                m->children.erase(m->children.begin() + i);
            }
        }
    }
}

void alt_tab::on_activated(int id) {
    Container *c = get_cid_container(id);

    assert(c && "alt_tab::on_activated assumes Container for id exists");    
    
    *datum<long>(c, LAST_TIME_ACTIVE) = get_current_time_in_ms();
}



