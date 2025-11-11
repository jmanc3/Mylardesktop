
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
    });    
}

void popup::open(int x, int y) {
    auto p = actual_root->child(100, 100);
    consume_everything(p);
    p->custom_type = (int) TYPE::OUR_POPUP;
    p->real_bounds = Bounds(x, y, p->wanted_bounds.w, p->wanted_bounds.h);
    p->when_paint = [](Container *actual_root, Container *c) {
        auto root = get_rendering_root();
        if (!root) return;
        auto [rid, s, stage, active_id] = roots_info(actual_root, root);
        if (stage == (int) STAGE::RENDER_POST_WINDOWS) {
            renderfix
            if (c->state.mouse_hovering) {
                rect(c->real_bounds, {1, 1, 1, 1}, 0, 10);
            } else {
                rect(c->real_bounds, {1, 1, 0, 1}, 0, 10);
            }
        }
    };
}


