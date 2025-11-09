
#include "resizing.h"

#include "second.h"

static bool is_resizing = false;
static int window_resizing = -1;
static int initial_x = 0;
static int initial_y = 0;
static int active_resize_type = 0;
static Bounds initial_win_box;

static float titlebar_button_ratio() {
    return hypriso->get_varfloat("plugin:mylardesktop:titlebar_button_ratio", 1.4375f);
}

float titlebar_icon_h() {
    return hypriso->get_varfloat("plugin:mylardesktop:titlebar_icon_h", 21);
}

float resize_edge_size() {
    return hypriso->get_varfloat("plugin:mylardesktop:resize_edge_size", 10);
}

bool resizing::resizing() {
    return is_resizing;
}

int resizing::resizing_window() {
    return window_resizing;
}

void update_cursor(int type) {
   if (type == (int) RESIZE_TYPE::NONE) {
        unsetCursorImage();
    } else if (type == (int) RESIZE_TYPE::BOTTOM) {
        setCursorImageUntilUnset("s-resize");
    } else if (type == (int) RESIZE_TYPE::BOTTOM_LEFT) {
        setCursorImageUntilUnset("sw-resize");
    } else if (type == (int) RESIZE_TYPE::BOTTOM_RIGHT) {
        setCursorImageUntilUnset("se-resize");
    } else if (type == (int) RESIZE_TYPE::TOP) {
        setCursorImageUntilUnset("n-resize");
    } else if (type == (int) RESIZE_TYPE::TOP_LEFT) {
        setCursorImageUntilUnset("nw-resize");
    } else if (type == (int) RESIZE_TYPE::TOP_RIGHT) {
        setCursorImageUntilUnset("ne-resize");
    } else if (type == (int) RESIZE_TYPE::LEFT) {
        setCursorImageUntilUnset("w-resize");
    } else if (type == (int) RESIZE_TYPE::RIGHT) {
        setCursorImageUntilUnset("e-resize");
    }
}

void resizing::begin(int cid, int type) {
    update_cursor(type);
    window_resizing = cid;
    is_resizing = true;
    initial_win_box = bounds_client(cid);
    auto m = mouse();
    initial_x = m.x;
    initial_y = m.y;
    active_resize_type = type;
    //notify("resizing");
}

void resize_client(int cid, int resize_type) {
    auto m = mouse();
    Bounds diff = {m.x - initial_x, m.y - initial_y, 0, 0};

    int change_x = 0;
    int change_y = 0;
    int change_w = 0;
    int change_h = 0;

    if (resize_type == (int) RESIZE_TYPE::NONE) {
    } else if (resize_type == (int) RESIZE_TYPE::BOTTOM) {
        change_h += diff.y;
    } else if (resize_type == (int) RESIZE_TYPE::BOTTOM_LEFT) {
        change_w -= diff.x;
        change_x += diff.x;
        change_h += diff.y;
    } else if (resize_type == (int) RESIZE_TYPE::BOTTOM_RIGHT) {
        change_w += diff.x;
        change_h += diff.y;
    } else if (resize_type == (int) RESIZE_TYPE::TOP) {
        change_y += diff.y;
        change_h -= diff.y;
    } else if (resize_type == (int) RESIZE_TYPE::TOP_LEFT) {
        change_y += diff.y;
        change_h -= diff.y;
        change_w -= diff.x;
        change_x += diff.x;
    } else if (resize_type == (int) RESIZE_TYPE::TOP_RIGHT) {
        change_y += diff.y;
        change_h -= diff.y;
        change_w += diff.x;
    } else if (resize_type == (int) RESIZE_TYPE::LEFT) {
        change_w -= diff.x;
        change_x += diff.x;
    } else if (resize_type == (int) RESIZE_TYPE::RIGHT) {
        change_w += diff.x;
    }
    auto size = initial_win_box;
    size.w += change_w;
    size.h += change_h;
    bool y_clipped = false;
    bool x_clipped = false;
    if (size.w < 100) {
        size.w    = 100;
        x_clipped = true;
    }
    if (size.h < 50) {
        size.h    = 50;
        y_clipped = true;
    }
    auto min = hypriso->min_size(cid);

    auto mini = 200;

    min.x = 10;
    min.y = 10;
    min.w = 10;
    min.h = 10;
    if (min.w < mini) {
        min.w = mini;
    }
    
    if (hypriso->is_x11(cid)) {
        //min.x /= s;
        //min.y /= s;
    }
    if (size.w < min.w) {
        size.w    = min.w;
        x_clipped = true;
    }
    if (size.h < min.h) {
        size.h    = min.h;
        y_clipped = true;
    }

    auto pos = initial_win_box;
    auto real = bounds_client(cid);
    if (x_clipped) {
        pos.x = real.x;
    } else {
        pos.x += change_x;
    }
    if (y_clipped) {
        pos.y = real.y;
    } else {
        pos.y += change_y;
    }
    auto fb = Bounds(pos.x, pos.y, size.w, size.h);
    hypriso->move_resize(cid, fb.x, fb.y, fb.w, fb.h);
    fb.grow(50);
    hypriso->damage_box(fb);   
}

void resizing::motion(int cid) {
    for (auto c : actual_root->children) {
        if (c->custom_type == (int) TYPE::CLIENT_RESIZE) {
            resize_client(cid, active_resize_type);
        }
    }
}

void resizing::end(int cid) {
    is_resizing = false;
    window_resizing = -1;
    //notify("stop resizing");
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
    *datum<int>(c, "resize_type") = (int) RESIZE_TYPE::NONE;
    c->when_paint = paint_resize_edge; 
    c->when_mouse_down = paint {
        hypriso->bring_to_front(*datum<int>(c, "cid"));  
        consume_event(root, c);
    };
    c->when_mouse_up = consume_event;
    c->when_mouse_enters_container = paint {
        setCursorImageUntilUnset("grabbing");

        auto box = c->real_bounds;
        auto m = mouse();
        box.shrink(resize_edge_size());
        int corner = 20;
 
        bool left = false;
        bool right = false;
        bool top = false;
        bool bottom = false;
        int resize_type = (int) RESIZE_TYPE::NONE;
        if (m.x < box.x + corner)
            left = true;
        if (m.x > box.x + box.w - corner)
            right = true;
        if (m.y < box.y + corner)
            top = true;
        if (m.y > box.y + box.h - corner)
            bottom = true;
        if (top && left) {
            resize_type = (int) RESIZE_TYPE::TOP_LEFT;
        } else if (top && right) {
            resize_type = (int) RESIZE_TYPE::TOP_RIGHT;
        } else if (bottom && left) {
            resize_type = (int) RESIZE_TYPE::BOTTOM_LEFT;
        } else if (bottom && right) {
            resize_type = (int) RESIZE_TYPE::BOTTOM_RIGHT;
        } else if (top) {
            resize_type = (int) RESIZE_TYPE::TOP;
        } else if (right) {
            resize_type = (int) RESIZE_TYPE::RIGHT;
        } else if (bottom) {
            resize_type = (int) RESIZE_TYPE::BOTTOM;
        } else if (left) {
            resize_type = (int) RESIZE_TYPE::LEFT;
        }

        /*
        auto rtype = resize_type;
        // Don't allow every type of resize for snapped windows
        if (client->snapped && client->snap_type == SnapPosition::MAX) {
            client->resize_type = (int) RESIZE_TYPE::NONE;
        }
        if (client->snapped && client->snap_type == SnapPosition::LEFT) {
            if (rtype != (int) RESIZE_TYPE::RIGHT) {
                client->resize_type = (int) RESIZE_TYPE::NONE;
            }
        }
        if (client->snapped && client->snap_type == SnapPosition::RIGHT) {
            if (rtype != (int) RESIZE_TYPE::LEFT) {
                client->resize_type = (int) RESIZE_TYPE::NONE;
            }
        }
        if (client->snapped && client->snap_type == SnapPosition::TOP_RIGHT) {
            auto valid = rtype == (int) RESIZE_TYPE::BOTTOM || 
                         rtype == (int) RESIZE_TYPE::BOTTOM_LEFT || 
                         rtype == (int) RESIZE_TYPE::LEFT;
            if (!valid)
                client->resize_type = (int) RESIZE_TYPE::NONE;
        }
        if (client->snapped && client->snap_type == SnapPosition::BOTTOM_RIGHT) {
            auto valid = rtype == (int) RESIZE_TYPE::TOP || 
                         rtype == (int) RESIZE_TYPE::TOP_LEFT || 
                         rtype == (int) RESIZE_TYPE::LEFT;
            if (!valid)
                client->resize_type = (int) RESIZE_TYPE::NONE;
        }
        if (client->snapped && client->snap_type == SnapPosition::BOTTOM_LEFT) {
            auto valid = rtype == (int) RESIZE_TYPE::TOP || 
                         rtype == (int) RESIZE_TYPE::TOP_RIGHT || 
                         rtype == (int) RESIZE_TYPE::RIGHT;
            if (!valid)
                client->resize_type = (int) RESIZE_TYPE::NONE;
        }
        if (client->snapped && client->snap_type == SnapPosition::TOP_LEFT) {
            auto valid = rtype == (int) RESIZE_TYPE::BOTTOM || 
                         rtype == (int) RESIZE_TYPE::BOTTOM_RIGHT || 
                         rtype == (int) RESIZE_TYPE::RIGHT;
            if (!valid)
                client->resize_type = (int) RESIZE_TYPE::NONE;
        }
        */

        *datum<int>(c, "resize_type") = resize_type;

        update_cursor(resize_type);

        consume_event(root, c);
    };
    c->when_mouse_motion = c->when_mouse_enters_container;
    c->when_mouse_leaves_container = paint {
        consume_event(root, c);
        update_cursor((int) RESIZE_TYPE::NONE);
    };
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
                //unsetCursorImage();
                return false;
            }
            //notify("here");
            //setCursorImageUntilUnset("grabbing");
            return true;
        }
        //unsetCursorImage();
        return false; 
    };
    c->when_drag_start = paint {
        int type = *datum<int>(c, "resize_type");
        int cid = *datum<int>(c, "cid");
        
        resizing::begin(cid, type);
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

