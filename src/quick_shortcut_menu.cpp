
#include "quick_shortcut_menu.h"

#include "second.h"

#include "defer.h"
#include "popup.h"

#include <linux/input-event-codes.h>
#include <vector>
#include <unordered_map>

struct KeyPress {
    int key = 0;
    int state = 0;
};

struct KeySequence {
    std::string representation;
    std::string name;
    std::string command;
    std::vector<KeyPress> keys;
};

static std::vector<KeySequence> sequences;

// and every press is eventually released (per-key balance is zero).
bool balanced(const std::vector<KeyPress> &keys) {
    std::unordered_map<int,int> balance; // balance[key] = number of outstanding presses

    for (const auto &kp : keys) {
        if (kp.state == 1) {
            // press: open one more outstanding press
            ++balance[kp.key];
        } else if (kp.state == 0) {
            // release: must have a prior outstanding press
            auto it = balance.find(kp.key);
            if (it == balance.end() || it->second == 0) {
                continue;
            }
            if (--it->second == 0)
                balance.erase(it); // optional: keep map small
        }
        // ignore other states if any
    }

    // all presses must be released
    return balance.empty();
}

void open_quick_shortcut_menu() {
    static const float option_height = 24;
    static const float seperator_size = 5;
    float s = scale(hypriso->monitor_from_cursor());
    sequences.clear();
    sequences.push_back({"k", "kate", "setsid kate", {{KEY_K, 1}, {KEY_K, 0}}});
    sequences.push_back({"y", "youtube", "setsid firefox https://www.youtube.com", {{KEY_Y, 1}, {KEY_Y, 0}}});

    float height = option_height * s * sequences.size();
    auto p = actual_root->child(::vbox, 277, height);
    p->name = "quick_shortcut_menu";
    consume_everything(p);
    p->receive_events_even_if_obstructed = true;
    p->custom_type = (int) TYPE::OUR_POPUP;

    Bounds mb = bounds_monitor(hypriso->monitor_from_cursor());
    p->real_bounds = Bounds(mb.x + mb.w * .5 - p->wanted_bounds.w * .5, mb.h * .05, p->wanted_bounds.w, p->wanted_bounds.h);
    p->when_mouse_enters_container = paint {
        //hypriso->all_lose_focus();
        setCursorImageUntilUnset("default");
        hypriso->send_false_position(-1, -1);
        consume_event(root, c);
    };
    p->when_mouse_leaves_container = paint {
        //hypriso->all_gain_focus();
        unsetCursorImage(true);
        consume_event(root, c);
    };
    p->when_paint = [](Container *actual_root, Container *c) {
        auto root = get_rendering_root();
        if (!root) return;
        auto [rid, s, stage, active_id] = roots_info(actual_root, root);
        if (stage == (int) STAGE::RENDER_POST_WINDOWS) {
            renderfix
            auto b = c->real_bounds;
            render_drop_shadow(rid, 1.0, {0, 0, 0, .07}, 7 * s, 2.0f, b);
            rect(b, {1, 1, 1, .95}, 0, 7 * s);
        }
    };
    p->pre_layout = [](Container *actual_root, Container *c, const Bounds &b) {
        ::layout(actual_root, c, c->real_bounds);
    };

    for (int i = 0; i < sequences.size(); i++) {
        auto option = p->child(FILL_SPACE, s * option_height);
        option->skip_delete = true;
        option->custom_type = i;

        option->when_paint = [](Container *actual_root, Container *c) {
            auto root = get_rendering_root();
            if (!root) return;
            auto [rid, s, stage, active_id] = roots_info(actual_root, root);
            if (stage == (int)STAGE::RENDER_POST_WINDOWS) {
                renderfix if (c->state.mouse_pressing) {
                    auto b = c->real_bounds;
                    rect(b, {0, 0, 0, .2}, 0, 7 * s, 2.0, false);
                }
                if (c->state.mouse_hovering) {
                    auto b = c->real_bounds;
                    rect(b, {0, 0, 0, .1}, 0, 7 * s, 2.0f, false);
                }

                auto index = c->custom_type;
                std::string text;
                for (int i = 0; i < sequences.size(); i++) {
                    if (i == index) {
                        text = sequences[i].representation + ": " + sequences[i].name;
                    }
                }
                
                auto info = gen_text_texture("Segoe UI Variable", text, 14 * s, {0, 0, 0, 1});
                draw_texture(info, c->real_bounds.x + 14 * s, center_y(c, info.h));
                free_text_texture(info.id);
            }
        };
        option->when_clicked = paint {
            auto index = c->custom_type;
            for (int i = 0; i < sequences.size(); i++) {
                if (i == index) {
                    launch_command(sequences[i].command);
                }
            }

            if (auto above = first_above_of(c, TYPE::OUR_POPUP))
                popup::close(above->uuid);
        };
    }

    for (auto m : actual_monitors)
        hypriso->damage_entire(*datum<int>(m, "cid"));
}

bool quick_shortcut_menu::on_key_press(int id, int key, int state, bool update_mods) {
    static std::vector<KeyPress> keys;
    static int keys_pressed_since_menu_opened = 0; // lets us know how many keys to check for sequence possibility
    
    keys.push_back({key, state});
    bool nothing_held_down = balanced(keys);

    bool consume = false;
    auto c = container_by_name("quick_shortcut_menu", actual_root);
    bool menu_open = c;
    if (menu_open) 
        consume = true;
    if (key == KEY_ESC)
        consume = false;

    if (menu_open) {
        keys_pressed_since_menu_opened++;
        // if no sequence left possible, simply close menu
        bool available_sequence = true;

        

        if (!available_sequence)
            popup::close(c->uuid);
    } else {
        if (keys.size() >= 2 && nothing_held_down) {
            auto before = keys[keys.size() - 2];
            auto now = keys[keys.size() - 1];
            if (before.key == KEY_RIGHTALT && now.key == KEY_RIGHTALT) {
                if (before.state == 1 && now.state == 0) {
                    keys_pressed_since_menu_opened = 0;
                    open_quick_shortcut_menu();
                }
            }
        }
    }

    return consume; // conumse
}

