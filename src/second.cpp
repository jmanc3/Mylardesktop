/**
 * 
 * Event pumper and distributor
 * 
 */

#include "second.h"

#include "container.h"
#include "hypriso.h"
#include "titlebar.h"
#include "events.h"
#include "icons.h"
#include "hotcorners.h"
#include "alt_tab.h"
#include "drag.h"
#include "resizing.h"
#include "client/test.h"

#ifdef TRACY_ENABLE
//#include "../tracy/public/client/TracyProfiler.hpp"
#include "tracy/Tracy.hpp"
#endif

#include <algorithm>
#include <linux/input-event-codes.h>
#include <thread>

std::unordered_map<std::string, Datas> datas;

std::vector<Container *> actual_monitors; // actually just root of all
Container *actual_root = new Container;

static void any_container_closed(Container *c) {
    remove_data(c->uuid); 
}

static bool on_mouse_move(int id, float x, float y) {
    second::layout_containers();
    auto mou = mouse();
    x = mou.x;
    y = mou.y;

    if (drag::dragging()) {
        drag::motion(drag::drag_window());
        return true;
    }
    if (resizing::resizing()) {
        resizing::motion(resizing::resizing_window());
        return true;
    }
    
    //notify(fz("{} {}", x, y));
    int active_mon = hypriso->monitor_from_cursor();
    {
        auto m = actual_root;
        auto cid = *datum<int>(m, "cid");
        auto bounds = bounds_monitor(cid);
        auto [rid, s, stage, active_id] = from_root(m);
        Event event(x - bounds.x, y - bounds.y);
        //notify(fz("{} {}                       ", event.x, event.y));
        
        move_event(m, event);
    }

    bool consumed = false;
    {
        auto root = actual_root;
        if (root->consumed_event) {
            consumed = true;
            root->consumed_event = false;
        }
    }

    if (!consumed) {
        hotcorners::motion(id, x, y);
    }

    return consumed;
}

static bool on_mouse_press(int id, int button, int state, float x, float y) {
    auto mou = mouse();
    x = mou.x;
    y = mou.y;

    bool consumed = false;
    if (drag::dragging() && !state) {
        drag::end(drag::drag_window());
    }
    if (resizing::resizing() && !state) {
        resizing::end(resizing::resizing_window());
    }
 
    if (alt_tab::showing()) {
        for (auto c : actual_root->children) {
            if (c->custom_type == (int)TYPE::ALT_TAB) {
                if (!bounds_contains(c->real_bounds, x, y)) {
                    alt_tab::close(true); 
                }
            }
        }
    }
 
    second::layout_containers();
    int active_mon = hypriso->monitor_from_cursor();
    {
        auto m = actual_root; 
        auto cid = *datum<int>(m, "cid");
        auto bounds = bounds_monitor(cid);
        auto [rid, s, stage, active_id] = from_root(m);
        Event event(x - bounds.x, y - bounds.y, button, state);
        mouse_event(m, event);
    }

    {
        auto root = actual_root;
        if (root->consumed_event) {
           consumed = true;
           root->consumed_event = false;
        } 
    }
    return consumed;
}

static bool on_scrolled(int id, int source, int axis, int direction, double delta, int discrete, bool from_mouse) {
    auto m = mouse();
    int active_mon = hypriso->monitor_from_cursor();
    //auto s = scale(active_mon);
    Event event;
    event.x = m.x;
    event.y = m.y;
    event.scroll = true;
    event.axis = axis;
    event.direction = direction;
    event.delta = delta;
    event.descrete = discrete;
    event.from_mouse = from_mouse;
    second::layout_containers();
    {
        auto m = actual_root;
        auto cid = *datum<int>(m, "cid");
        auto bounds = bounds_monitor(cid);
        auto [rid, s, stage, active_id] = from_root(m);
        event.x -= bounds.x;
        event.y -= bounds.y;
        //Event event(x - bounds.x, y - bounds.y, button, state);
        mouse_event(m, event);
    }

    bool consumed = false;
    {
        auto root = actual_root; 
        if (root->consumed_event) {
            consumed = true;
            root->consumed_event = false;
        }
    }

    return consumed;
}

static bool on_key_press(int id, int key, int state, bool update_mods) {
    static bool alt_held = false;
    if (key == KEY_LEFTALT || key == KEY_RIGHTALT) {
        alt_held = state;
    }
    static bool shift_held = false;
    if (key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT) {
        shift_held = state;
    }
    if (alt_held) {
        if (key == KEY_TAB) {
            if (state) {
                alt_tab::show();
                if (shift_held) {
                    alt_tab::move(-1);
                } else {
                    alt_tab::move(1);
                }
            }
        }
    }
    bool alt_showing = alt_tab::showing();
    if (!alt_held && alt_showing) {
       alt_tab::close(true); 
    }
    
    if (key == KEY_TAB && state == 0) {
        //hypriso->no_render = !hypriso->no_render;
        //nz(fz(
          //"rendering {}", !hypriso->no_render  
        //));
    }
    
    return false;
}

SnapPosition mouse_to_snap_position(int mon, int x, int y) {
    Bounds pos = bounds_reserved_monitor(mon);

    const float edgeThresh = 20.0;
    const float sideThreshX = pos.w * 0.05f;
    const float sideThreshY = pos.h * 0.05f;
    const float rightEdge = pos.x + pos.w;
    const float bottomEdge = pos.y + pos.h;
    bool on_top_edge = y < pos.y + edgeThresh;
    bool on_bottom_edge = y > bottomEdge - edgeThresh;
    bool on_left_edge = x < pos.x + edgeThresh;
    bool on_right_edge = x > rightEdge - edgeThresh;
    bool on_left_side = x < pos.x + sideThreshX;
    bool on_right_side = x > rightEdge - sideThreshX;
    bool on_top_side = y < pos.y + sideThreshY;
    bool on_bottom_side = y > bottomEdge - sideThreshY;

    if ((on_top_edge && on_left_side) || (on_left_edge && on_top_side)) {
        return SnapPosition::TOP_LEFT;
    } else if ((on_top_edge && on_right_side) || (on_right_edge && on_top_side)) {
        return SnapPosition::TOP_RIGHT;
    } else if ((on_bottom_edge && on_left_side) || (on_left_edge && on_bottom_side)) {
        return SnapPosition::BOTTOM_LEFT;
    } else if ((on_bottom_edge && on_right_side) || (on_right_edge && on_bottom_side)) {
        return SnapPosition::BOTTOM_RIGHT;
    } else if (on_top_edge) {
        return SnapPosition::MAX;
    } else if (on_left_edge) {
        return SnapPosition::LEFT;
    } else if (on_right_edge) {
        return SnapPosition::RIGHT;
    } else if (on_bottom_edge) {
        return SnapPosition::MAX;
    } else {
        return SnapPosition::NONE;
    }

    return SnapPosition::NONE;
}

Bounds snap_position_to_bounds(int mon, SnapPosition pos) {
    Bounds screen = bounds_reserved_monitor(mon);

    float x = screen.x;
    float y = screen.y;
    float w = screen.w;
    float h = screen.h;

    Bounds out = {x, y, w, h};

    if (pos == SnapPosition::MAX) {
        return {x, y, w, h};
        //return {x, y, w + 2, h};
    } else if (pos == SnapPosition::LEFT) {
        return {x, y, w * .5, h};
    } else if (pos == SnapPosition::RIGHT) {
        return {x + w * .5, y, w * .5, h};
    } else if (pos == SnapPosition::TOP_LEFT) {
        return {x, y, w * .5, h * .5};
    } else if (pos == SnapPosition::TOP_RIGHT) {
        return {x + w * .5, y, w * .5, h * .5};
    } else if (pos == SnapPosition::BOTTOM_LEFT) {
        return {x, y + h * .5, w * .5, h * .5};
    } else if (pos == SnapPosition::BOTTOM_RIGHT) {
        return {x + w * .5, y + h * .5, w * .5, h * .5};
    }

    return out;
}

SnapPosition opposite_snap_position(SnapPosition pos) {
    if (pos == SnapPosition::NONE) {
        return SnapPosition::MAX;
    } else if (pos == SnapPosition::MAX) {
        return SnapPosition::NONE;
    } else if (pos == SnapPosition::LEFT) {
        return SnapPosition::RIGHT;
    } else if (pos == SnapPosition::RIGHT) {
        return SnapPosition::LEFT;
    } else if (pos == SnapPosition::TOP_LEFT) {
        return SnapPosition::BOTTOM_LEFT;
    } else if (pos == SnapPosition::TOP_RIGHT) {
        return SnapPosition::BOTTOM_RIGHT;
    } else if (pos == SnapPosition::BOTTOM_LEFT) {
        return SnapPosition::TOP_LEFT;
    } else if (pos == SnapPosition::BOTTOM_RIGHT) {
        return SnapPosition::TOP_RIGHT;
    }
    return pos;
}

void paint_snap_preview(Container *actual_root, Container *c) {
    auto root = get_rendering_root();
    if (!root)
        return;
    auto [rid, s, stage, active_id] = roots_info(actual_root, root);
    auto cid = *datum<int>(c, "cid");

    if (active_id == cid && stage == (int)STAGE::RENDER_POST_WINDOW) {
        if (*datum<bool>(c, "snapped") && (*datum<int>(c, "snap_type") != (int)SnapPosition::MAX)) {
            renderfix auto b = c->real_bounds;
            b.shrink(1);
            border(b, {.5, .5, .5, .8}, 1);
        }
    }

    if (!(active_id == cid && stage == (int)STAGE::RENDER_PRE_WINDOW))
        return;
    if (!(drag::dragging() && cid == drag::drag_window()))
        return;
    auto cursor_mon = hypriso->monitor_from_cursor();
    if (rid != cursor_mon)
        return;

    renderfix

    auto m = mouse();
    SnapPosition pos = mouse_to_snap_position(cursor_mon, m.x, m.y);
    if (pos != SnapPosition::NONE) {
        Bounds b = snap_position_to_bounds(rid, pos);
        b.shrink(10);
        b.x -= root->real_bounds.x;
        b.y -= root->real_bounds.y;
        b.scale(s);
        rect(b, {1, 1, 1, .3}, 0, 0, 2.0f, false, 0.0);
    }
}

static void on_window_open(int id) {    
    // We make the client on the first monitor we fine, because we move the container later based on actual monitor location
    {
        auto m = actual_root; 
        auto c = m->child(FILL_SPACE, FILL_SPACE);
        c->custom_type = (int) TYPE::CLIENT;
        c->when_paint = paint_snap_preview;
        
        *datum<int>(c, "cid") = id; 
        *datum<bool>(c, "snapped") = false; 
    }
    
    hypriso->set_corner_rendering_mask_for_window(id, 3);
    
    titlebar::on_window_open(id);
    alt_tab::on_window_open(id);
    resizing::on_window_open(id);
    second::layout_containers();
}

static void on_window_closed(int id) {
    titlebar::on_window_closed(id);
    resizing::on_window_closed(id);
    alt_tab::on_window_closed(id);
    //notify("close: " + std::to_string(id));
    //notify("monitors: " + std::to_string(monitors.size()));

    {
        auto m = actual_root; 
        //notify("ch: " + std::to_string(m->children.size()));
        
        for (int i = m->children.size() - 1; i >= 0; i--) {
            auto cid = *datum<int>(m->children[i], "cid");
            //notify("check: " + std::to_string(cid));
            if (cid == id) {
                //notify("here");
                delete m->children[i];
                m->children.erase(m->children.begin() + i);
            }
        } 
    }
    second::layout_containers();
}

static void on_layer_open(int id) {    
    return;
    auto m = actual_root; 
    auto c = m->child(FILL_SPACE, FILL_SPACE);
    c->custom_type = (int) TYPE::LAYER;
    *datum<int>(c, "cid") = id;
    log(fz("open layer {}", id));
}

static void on_layer_closed(int id) {    
    auto m = actual_root; 

    for (int i = m->children.size() - 1; i >= 0; i--) {
        auto cid = *datum<int>(m->children[i], "cid");
        if (cid == id) {
            delete m->children[i];
            m->children.erase(m->children.begin() + i);
        }
    } 

    second::layout_containers();
}

static void on_layer_change() {
    // move snapped windows
    later_immediate([](Timer *) {
        for (auto c : actual_root->children) {
            if (c->custom_type == (int) TYPE::CLIENT) {
                auto snapped = *datum<bool>(c, "snapped");
                auto snap_type = *datum<int>(c, "snap_type");
                if (snapped) {
                    int cid = *datum<int>(c, "cid");
                    auto p = snap_position_to_bounds(get_monitor(cid), (SnapPosition) snap_type);
                    float scalar = hypriso->has_decorations(cid); // if it has a titlebar
                    hypriso->move_resize(cid, p.x, p.y + titlebar_h * scalar, p.w, p.h - titlebar_h * scalar, false);
                    hypriso->should_round(cid, false);
                }
            }
        }        
    });

}

static void test_container(Container *m) {
    auto c = m->child(100, 100);
    c->custom_type = (int) TYPE::TEST;
    c->when_fine_scrolled = [](Container* root, Container* c, int scroll_x, int scroll_y, bool came_from_touchpad) {
        c->scroll_v_real += scroll_y; 
    };
    c->when_paint = [](Container *root, Container *c) {
        auto b = c->real_bounds;
        c->real_bounds.y += c->scroll_v_real;
        if (c->state.mouse_pressing) {
            rect(c->real_bounds, {1, 1, 1, 1});
        } else if (c->state.mouse_hovering) {
            rect(c->real_bounds, {1, 0, 0, 1});
        } else {
            rect(c->real_bounds, {1, 0, 1, 1});
        }
        auto info = gen_text_texture("Segoe UI", fz("{} {}", c->real_bounds.x, c->real_bounds.y), 20, {1, 1, 1, 1});
        draw_texture(info, c->real_bounds.x, c->real_bounds.y);
        free_text_texture(info.id);
        c->real_bounds = b;
    };
    c->pre_layout = [](Container *root, Container *c, const Bounds &bounds) {
        auto [rid, s, stage, active_id] = from_root(root);
        //nz(fz("{} {}", root->real_bounds.x * s, root->real_bounds.y));
        c->real_bounds = Bounds(20, 100, 100, 100);
        //hypriso->damage_entire(rid);
    };
    c->when_mouse_motion = request_damage;
    c->when_mouse_down = paint {
        consume_event(root, c);
        //request_damage(root, c);
    };
    c->when_mouse_up = paint {
        consume_event(root, c);
        //request_damage(root, c);
    };
    c->when_clicked = request_damage;
    c->when_mouse_leaves_container = request_damage;
    c->when_mouse_enters_container = request_damage;
}

static void on_monitor_open(int id) {
    auto c = new Container();
    //c->when_paint = paint_debug;
    actual_monitors.push_back(c);
    auto cid = datum<int>(c, "cid");
    *cid = id;
    second::layout_containers();
}

static void on_monitor_closed(int id) {
    for (int i = actual_monitors.size() - 1; i >= 0; i--) {
        auto cid = *datum<int>(actual_monitors[i], "cid");
        if (cid == id) {
            actual_monitors.erase(actual_monitors.begin() + i);
        }
    }
    second::layout_containers();
}

static void on_activated(int id) {
    titlebar::on_activated(id);
    alt_tab::on_activated(id);
}

static void on_draw_decos(std::string name, int monitor, int id, float a) {
    titlebar::on_draw_decos(name, monitor, id, a);
}

static void draw_text(std::string text, int x, int y) {
    return;
    TextureInfo first_info;
    {
        first_info = gen_text_texture("Monospace", text, 40, {0, 0, 0, 1});
        rect(Bounds(x, y, (double) first_info.w, (double) first_info.h), {1, 0, 1, 1});
        draw_texture(first_info, x + 3, y + 4);
        free_text_texture(first_info.id);
    }
    {
        auto info = gen_text_texture("Monospace", text, 40, {1, 1, 1, 1});
        draw_texture(info, x, y);
        free_text_texture(info.id);
    }
    
}

static void on_render(int id, int stage) {
    if (stage == (int) STAGE::RENDER_BEGIN) {
        second::layout_containers();
    }

    int current_monitor = current_rendering_monitor();
    int current_window = current_rendering_window();
    int active_id = current_window == -1 ? current_monitor : current_window;

    for (auto r : actual_monitors) {
        auto cid = *datum<int>(r, "cid");
        //hypriso->damage_entire(cid);
        if (cid == current_monitor) {
            *datum<int>(actual_root, "stage") = stage;
            *datum<int>(actual_root, "active_id") = active_id;
            paint_outline(actual_root, actual_root);
        }
    }
    if (stage == (int) STAGE::RENDER_LAST_MOMENT) {
        /*
        auto root = get_rendering_root();
        hypriso->damage_entire(*datum<int>(root, "cid"));
        if (!root) return;
        auto [rid, s, stage, active_id] = roots_info(actual_root, root);
 
        auto m = mouse();
        auto box = Bounds(m.x - root->real_bounds.x, m.y - root->real_bounds.y, 100, 100);
        auto b2 = box;
        b2.shrink(30);
        clip(b2, s);
        box.scale(s);
        rect(box, {1, 0, 1, 1});
        */
    }
}

static void on_drag_start_requested(int id) {
    drag::begin(id);
}

static void on_resize_start_requested(int id, RESIZE_TYPE type) {
    resizing::begin(id, (int) type);
}

static void on_drag_or_resize_cancel_requested() {
    if (drag::dragging()) {
        drag::end(drag::drag_window());
    }
    if (resizing::resizing()) {
        resizing::end(resizing::resizing_window());
    }
}


static void on_config_reload() {

}

static void create_actual_root() {
    *datum<long>(actual_root, "drag_end_time") = 0;
}

void second::begin() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    on_any_container_close = any_container_closed;
    create_actual_root();
    
    hypriso->create_config_variables();
        
    hypriso->on_mouse_press = on_mouse_press;
    hypriso->on_mouse_move = on_mouse_move;
    hypriso->on_key_press = on_key_press;
    hypriso->on_scrolled = on_scrolled;
    hypriso->on_draw_decos = on_draw_decos;
    hypriso->on_render = on_render;
    hypriso->on_window_open = on_window_open;
    hypriso->on_window_closed = on_window_closed;
    hypriso->on_layer_open = on_layer_open;
    hypriso->on_layer_closed = on_layer_closed;
    hypriso->on_layer_change = on_layer_change;
    hypriso->on_monitor_open = on_monitor_open;
    hypriso->on_monitor_closed = on_monitor_closed;
    hypriso->on_drag_start_requested = on_drag_start_requested;
    hypriso->on_resize_start_requested = on_resize_start_requested;
    hypriso->on_drag_or_resize_cancel_requested = on_drag_or_resize_cancel_requested;
    hypriso->on_config_reload = on_config_reload;
    hypriso->on_activated = on_activated;

	hypriso->create_callbacks();
	hypriso->create_hooks();
	
    hypriso->add_float_rule();

    if (icon_cache_needs_update()) {
        std::thread th([] {
            icon_cache_generate();
            icon_cache_load();
        });
        th.detach();
    } else {
        icon_cache_load();
    }

    std::thread t([] {
        start_dock();
    });
    t.detach();
}

void second::end() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    hypriso->end();    
    stop_dock();

//#ifdef TRACY_ENABLE
    //tracy::ShutdownProfiler();
//#endif
}

void second::layout_containers() {
    if (actual_monitors.empty())
        return;
    std::vector<Container *> backup;
    {
        auto r = actual_root;
        for (int i = r->children.size() - 1; i >= 0; i--) {
            auto c = r->children[i];
            if (c->custom_type != (int) TYPE::CLIENT) {
                backup.push_back(c);
                r->children.erase(r->children.begin() + i);
            }
        }
    }

    // reorder based on stacking
    std::vector<int> order = get_window_stacking_order();
    {
        auto r = actual_root;
        // update the index based on the stacking order
        for (auto c : r->children) {
            auto sort_index = datum<int>(c, "sort_index");
            auto cid = *datum<int>(c, "cid");
            for (int i = 0; i < order.size(); i++)
               if (order[i] == cid)
                    *sort_index = i;
        }
        // sort the children based on index
        std::sort(r->children.begin(), r->children.end(), [](Container *a, Container *b) {
            auto adata_index = *datum<int>(a, "sort_index"); 
            auto bdata_index = *datum<int>(b, "sort_index"); 
            return adata_index > bdata_index; 
        });
    }
    
    for (auto r : actual_monitors) {
        auto rid = *datum<int>(r, "cid");
        r->real_bounds = bounds_monitor(rid); 
    }

    for (auto c : actual_root->children) {
        auto cid = *datum<int>(c, "cid");
        c->exists = hypriso->is_mapped(cid) && !hypriso->is_hidden(cid);
        if (c->exists) {
            auto b = bounds_client(cid);
            auto fo = hypriso->floating_offset(cid);
            auto so = hypriso->workspace_offset(cid);
            if (hypriso->has_decorations(cid)) {
                c->real_bounds = Bounds(b.x + fo.x + so.x,
                                        //b.x,
                                        b.y - titlebar_h + fo.y + so.y,
                                        //b.y - titlebar_h,
                                        b.w, b.h + titlebar_h);
            } else {
                c->real_bounds = Bounds(b.x + fo.x + so.x, b.y + fo.y + so.y, b.w, b.h);
            }
            ::layout(actual_root, c, c->real_bounds);
        }
    }

    for (auto c : backup) {
        *datum<bool>(c, "touched") = false;
    }

    for (auto c : backup) {
        if (c->custom_type == (int) TYPE::ALT_TAB) {
            c->parent->children.insert(c->parent->children.begin(), c);
            if (c->pre_layout) {
                c->pre_layout(actual_root, c, c->parent->real_bounds);
                *datum<bool>(c, "touched") = true;
            }
        }
        if (c->custom_type == (int) TYPE::CLIENT_RESIZE) {
            auto id = *datum<int>(c, "cid");
            c->exists = hypriso->is_mapped(id) && !hypriso->is_hidden(id);
            for (int i = actual_root->children.size() - 1; i >= 0; i--) {
                auto child = actual_root->children[i];
                auto cid = *datum<int>(child, "cid");
                if (child->custom_type == (int) TYPE::CLIENT && cid == id) {
                    c->real_bounds = child->real_bounds;
                    //actual_root->children.insert(actual_root->children.begin() + i + 1, c);
                    actual_root->children.insert(actual_root->children.begin() + i, c);
                    *datum<bool>(c, "touched") = true;
                    if (c->pre_layout) {
                        c->pre_layout(actual_root, c, actual_root->real_bounds);
                    }
                }
            }
        }
        if (c->custom_type == (int) TYPE::TEST) {
            c->parent->children.insert(c->parent->children.begin(), c);
            if (c->pre_layout) {
                c->pre_layout(actual_root, c, c->parent->real_bounds);
            }
            *datum<bool>(c, "touched") = true;
        }

        if (c->custom_type == (int) TYPE::LAYER) {
            log("TODO: layer needs to be positioned based on above or below, and level in that stack");
            c->parent->children.insert(c->parent->children.begin(), c);
            auto id = *datum<int>(c, "cid");
            log(fz("layout layer: {}", id));
            c->real_bounds = bounds_layer(id);
            
            log(fz("{} {} {} {}", c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h));
            
            *datum<bool>(c, "touched") = true;
        }
    }
    for (auto c : backup) {
        if (!(*datum<bool>(c, "touched"))) {
            notify("hey you forgot to layout one of the containers in layout_containers probably leading to it not getting drawn");
        }
    }
}

bool double_clicked(Container *c, std::string needle) {
    auto n = needle + "_double_click_check";
    long *data = get_data<long>(c->uuid, n);
    if (!data) {
        data = datum<long>(c, n);
        *data = 0;
    }
    long *activation = get_data<long>(c->uuid, n + "_activation");
    if (!activation) {
        activation = datum<long>(c, n + "_activation");
        *activation = 0;
    }
    
    long current = get_current_time_in_ms();
    long last_time = *data;
    long last_activation = *activation;
    if (current - last_time < 500 && current - last_activation > 600) {
        data = datum<long>(c, n);
        *activation = current;
        return true; 
    }
    *data = current;
    return false;
}

#include <fstream>
#include <string>
#include <mutex>

void log(const std::string& msg) {
    //return;
    static bool firstCall = true;
    static std::ofstream ofs;
    static std::mutex writeMutex;
    static long num = 0; 

    std::lock_guard<std::mutex> lock(writeMutex);

    if (firstCall) {
        ofs.open("/tmp/log", std::ios::out | std::ios::trunc);
        firstCall = false;

        // Replace "program" with something that displays a live-updating file.
        // Example choices:
        //   - `xterm -e "tail -f /tmp/log"`
        //   - `gedit /tmp/log`
        //   - `glow /tmp/log`
        //std::thread t([]() {
            //system("alacritty -e tail -f /tmp/log");
        //});
        //t.detach();
    } else if (!ofs.is_open()) {
        // If log is called after close, recover
        ofs.open("/tmp/log", std::ios::out | std::ios::app);
    }

    std::string result = std::format("{:>10}", num++);
    ofs << result << ' ' << msg << '\n';
    ofs.flush(); // force write so GUI viewer always shows latest content
}

Container *get_rendering_root() {
    auto rendering_monitor = current_rendering_monitor();
    for (auto m : actual_monitors) {
        auto rid = *datum<int>(m, "cid");
        m->real_bounds = bounds_monitor(rid);
        if (rid == rendering_monitor)
            return m;
    }

    for (auto m : actual_monitors)
        return m;

    return nullptr;
}

void consume_event(Container *actual_root, Container *c) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    actual_root->consumed_event = true;
    request_damage(actual_root, c);
}
