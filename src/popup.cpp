
#include "popup.h"

#include "second.h"
#include "icons.h"

void popup::close(std::string uuid) {
    later_immediate([uuid](Timer *) {
        for (int i = 0; i < actual_root->children.size(); i++) {
           auto child = actual_root->children[i];
           if (child->uuid == uuid) {
               delete child;
               actual_root->children.erase(actual_root->children.begin() + i);
               break;
           }
        }
        for (auto m : actual_monitors) {
            hypriso->damage_entire(*datum<int>(m, "cid"));
        }
    });    
}

struct PopOptionData : UserData {
    PopOption p;
};

void popup::open(std::vector<PopOption> root, int x, int y) {
    static const float option_height = 24;
    float s = scale(hypriso->monitor_from_cursor());

    auto p = actual_root->child(::vbox, 277, root.size()  * ((option_height) * s));
    consume_everything(p);
    p->receive_events_even_if_obstructed = true;
    p->custom_type = (int) TYPE::OUR_POPUP;
    p->real_bounds = Bounds(x, y, p->wanted_bounds.w, p->wanted_bounds.h);
    //p->wanted_pad = Bounds(7, 7, 7, 7);
    p->when_paint = [](Container *actual_root, Container *c) {
        auto root = get_rendering_root();
        if (!root) return;
        auto [rid, s, stage, active_id] = roots_info(actual_root, root);
        if (stage == (int) STAGE::RENDER_POST_WINDOWS) {
            renderfix
            auto b = c->real_bounds;
            rect(b, {1, 1, 1, .8}, 0, 7 * s);
            //b.shrink(1 * s);
            //border(b, {0, 0, 0, .2}, 1 * s, 0, 7 * s);
        }
    };
    p->pre_layout = [](Container *actual_root, Container *c, const Bounds &b) {
        ::layout(actual_root, c, c->real_bounds);
    };
    //p->child(FILL_SPACE, 2 * s);
    for (auto pop_option : root) {
        auto option = p->child(FILL_SPACE, option_height * s);
        auto popdata = new PopOptionData;
        popdata->p = pop_option;
        option->user_data = popdata;
        option->when_paint = [](Container *actual_root, Container *c) {
            auto root = get_rendering_root();
            if (!root) return;
            auto [rid, s, stage, active_id] = roots_info(actual_root, root);
            if (stage == (int) STAGE::RENDER_POST_WINDOWS) {
                renderfix
                if (c->state.mouse_hovering) {
                    auto b = c->real_bounds;
                    rect(b, {1, 1, 1, 1}, 0, 7 * s);
                }

                auto popdata = (PopOptionData*)c->user_data;
                auto pop_option = popdata->p;

                {
                    std::string icon = ":Fluent:window-pin";
                    if (pop_option.text == "Unpin") {
                        icon = ":Fluent:window-unpin";
                    }
                    auto ico = one_shot_icon(14 * s, {icon});
                    if (!ico.empty()) {
                        auto info = gen_texture(ico, 18 * s);
                        draw_texture(info, c->real_bounds.x + 10 * s, center_y(c, info.h));
                        free_text_texture(info.id);
                    }
                }
                
                auto info = gen_text_texture("Segoe UI Variable", pop_option.text, 14 * s, {0, 0, 0, 1});
                draw_texture(info, c->real_bounds.x + 32 * s, center_y(c, info.h));
                free_text_texture(info.id);
            }
        };
        option->when_clicked = paint {
            auto popdata = (PopOptionData *) c->user_data;
            auto pop_option = popdata->p;
            if (pop_option.on_clicked) {
                pop_option.on_clicked();
            }
            for (auto c : actual_root->children) {
                if (c->custom_type == (int) TYPE::OUR_POPUP) {
                    popup::close(c->uuid);
                }
            }
        };
    }
    //p->child(FILL_SPACE, 2 * s);
    
    if (hypriso->on_mouse_move) {
        auto m = mouse();
        hypriso->on_mouse_move(0, m.x, m.y);
    }
}

