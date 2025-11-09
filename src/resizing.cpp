
#include "resizing.h"

#include "second.h"

float resize_edge_size() {
    return hypriso->get_varfloat("plugin:mylardesktop:resize_edge_size", 10);
}

void resizing::begin(int cid, int type) {

}

void resizing::motion(int cid) {

}

void resizing::end(int cid) {

}

void paint_resize_edge(Container *actual_root, Container *c) {
    auto root = get_rendering_root();
    if (!root) return;
    auto [rid, s, stage, active_id] = roots_info(actual_root, root);
    auto cid = *datum<int>(c, "cid");

    if (active_id == cid && stage == (int) STAGE::RENDER_POST_WINDOW) {
        renderfix
        //border(c->real_bounds, {1, 0, 1, 1}, 4);
    }
}

// mouse inside rounded rectangle (uniform radius)
bool mouse_inside_rounded(const Bounds& r, float mx, float my, float radius)
{
    // clamp mouse pos to the nearest point *inside* the rect's inner bounds
    // (the area excluding corner arcs)
    float innerLeft   = r.x + radius;
    float innerRight  = r.x + r.w - radius;
    float innerTop    = r.y + radius;
    float innerBottom = r.y + r.h - radius;

    // if mouse is inside the central box, no need to test circles
    if (mx >= innerLeft && mx <= innerRight)
        return true;
    if (my >= innerTop && my <= innerBottom)
        return true;

    // Otherwise: test distance to nearest corner circle center
    float cx = (mx < innerLeft)  ? innerLeft  : innerRight;
    float cy = (my < innerTop)   ? innerTop   : innerBottom;

    float dx = mx - cx;
    float dy = my - cy;
    return dx*dx + dy*dy <= radius * radius;
}

void create_resize_container_for_window(int id) {
    auto c = actual_root->child(FILL_SPACE, FILL_SPACE);
    c->custom_type = (int) TYPE::CLIENT_RESIZE;
    *datum<int>(c, "cid") = id;
    c->when_paint = paint_resize_edge; 
    c->when_mouse_down = paint {
        hypriso->bring_to_front(*datum<int>(c, "cid"));  
        consume_event(root, c);
    };
    c->when_mouse_up = consume_event;
    c->when_mouse_motion = consume_event;
    c->when_mouse_enters_container = consume_event;
    c->when_mouse_leaves_container = consume_event;
    c->when_clicked = paint {
        hypriso->bring_to_front(*datum<int>(c, "cid"));  
        consume_event(root, c);
    };
    c->pre_layout = [](Container *actual_root, Container *c, const Bounds &b) {
       c->real_bounds.grow(resize_edge_size());
    };
    c->handles_pierced = [](Container* c, int x, int y) {
        auto b = c->real_bounds;
        b.shrink(resize_edge_size());
        auto cid = *datum<int>(c, "cid");
        float rounding = hypriso->get_rounding(cid);
       
        if (bounds_contains(c->real_bounds, x, y)) {
            if (bounds_contains(b, x, y)) {
                return false;
            }            
            return true;
        }
        return false; 
    };
}

void remove_resize_container_for_window(int id) {
    for (int i = actual_root->children.size() - 1; i >= 0; i--) {
        auto c = actual_root->children[i];
        auto cid = *datum<int>(c, "cid");
        if (cid == id && c->custom_type == (int) TYPE::CLIENT_RESIZE) {
            delete c;
            actual_root->children.erase(actual_root->children.begin() + i);
        }
    }
}

void resizing::on_window_open(int id) {
    create_resize_container_for_window(id);
}

void resizing::on_window_closed(int id) {
    remove_resize_container_for_window(id);
}

