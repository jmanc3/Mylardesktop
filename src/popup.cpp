
#include "popup.h"

#include "second.h"

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

void popup::open(int id, int x, int y) {
    auto p = actual_root->child(::vbox, 277, 100);
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
            rect(b, {1, 1, 1, .8}, 0, 14);
            b.shrink(1 * s);
            border(b, {0, 0, 0, .2}, 1 * s, 0, 14);
        }
    };
    p->pre_layout = [](Container *actual_root, Container *c, const Bounds &b) {
        ::layout(actual_root, c, c->real_bounds);
    };
    p->child(FILL_SPACE, 5);
    auto one = p->child(FILL_SPACE, 28);
    one->when_paint = [](Container *actual_root, Container *c) {
        auto root = get_rendering_root();
        if (!root) return;
        auto [rid, s, stage, active_id] = roots_info(actual_root, root);
        if (stage == (int) STAGE::RENDER_POST_WINDOWS) {
            renderfix
            if (c->state.mouse_hovering) {
                auto b = c->real_bounds;
                b.x += 7;
                b.w -= 14;
                b.h -= 1;
                rect(b, {1, 1, 1, 1}, 0, 14);
            }
        }
    };
    one->when_clicked = paint {
        auto id = *datum<int>(c, "cid");
        bool state = hypriso->is_pinned(id);
        hypriso->pin(id, !state);
    };
    *datum<int>(one, "cid") = id;
    auto two = p->child(FILL_SPACE, 28);
    two->when_paint = one->when_paint;

    if (hypriso->on_mouse_move) {
        auto m = mouse();
        hypriso->on_mouse_move(0, m.x, m.y);
    }
}


void popup::open(std::vector<PopOption> root, int x, int y) {
    
}

