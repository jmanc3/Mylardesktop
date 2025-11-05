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

#ifdef TRACY_ENABLE
//#include "../tracy/public/client/TracyProfiler.hpp"
#include "tracy/Tracy.hpp"
#endif

#include <algorithm>
#include <linux/input-event-codes.h>
#include <thread>

std::unordered_map<std::string, Datas> datas;

std::vector<Container *> monitors;

static void any_container_closed(Container *c) {
    remove_data(c->uuid); 
}

static bool on_mouse_move(int id, float x, float y) {
    second::layout_containers();
    auto mou = mouse();
    x = mou.x;
    y = mou.y;

    //if (drag::dragging()) {
        //drag::motion(drag::drag_window());
        //return true;
    //}
    //notify(fz("{} {}", x, y));
    int active_mon = hypriso->monitor_from_cursor();
    for (auto m : monitors) {
        auto cid = *datum<int>(m, "cid");
        if (cid != active_mon)
            continue;
        auto bounds = bounds_monitor(cid);
        auto [rid, s, stage, active_id] = from_root(m);
        Event event(x - bounds.x, y - bounds.y);
        //notify(fz("{} {}                       ", event.x, event.y));
        
        move_event(m, event);
    }

    bool consumed = false;
    for (auto root : monitors) {
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

    //if (drag::dragging() && !state) {
        //drag::end(drag::drag_window());
        //return true;
    //}
    second::layout_containers();
    int active_mon = hypriso->monitor_from_cursor();
    for (auto m: monitors) {
        auto cid = *datum<int>(m, "cid");
        if (cid != active_mon)
            continue;
        auto bounds = bounds_monitor(cid);
        auto [rid, s, stage, active_id] = from_root(m);
        Event event(x - bounds.x * s, y - bounds.y * s, button, state);
        mouse_event(m, event);
    }
    
    bool consumed = false;
    for (auto root : monitors) {
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
    for (auto m : monitors) {
        auto cid = *datum<int>(m, "cid");
        if (cid != active_mon)
            continue;
        auto bounds = bounds_monitor(cid);
        auto [rid, s, stage, active_id] = from_root(m);
        event.x -= bounds.x * s;
        event.y -= bounds.y * s;
        //Event event(x - bounds.x * s, y - bounds.y * s, button, state);
        mouse_event(m, event);
    }

    bool consumed = false;
    for (auto root : monitors) {
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
    if (alt_held && key == KEY_TAB) {
       if (state)  {
           //alt_tab::show();
       }
    } else {
        alt_tab::close();
    }
    if (key == KEY_TAB && state == 0) {
        //hypriso->no_render = !hypriso->no_render;
        //nz(fz(
          //"rendering {}", !hypriso->no_render  
        //));
    }
    
    return false;
}

static void on_window_open(int id) {    
    // We make the client on the first monitor we fine, because we move the container later based on actual monitor location
    for (auto m : monitors) {
        auto c = m->child(FILL_SPACE, FILL_SPACE);
        c->custom_type = (int) TYPE::CLIENT;
        c->when_paint = paint {
            auto [rid, s, stage, active_id] = from_root(root);
            auto cid = *datum<int>(c, "cid");
            //nz(fz("{} {} {} {} {}", rid, s, stage, active_id, cid));
            if (cid == active_id && stage == (int) STAGE::RENDER_PRE_WINDOW) {
                //border(c->real_bounds, {1, 0, 1, 1}, 5);
            }
        };
        
        *datum<int>(c, "cid") = id; 
        *datum<bool>(c, "snapped") = false; 

        break;
    }
    
    hypriso->set_corner_rendering_mask_for_window(id, 3);
    
    titlebar::on_window_open(id);
    alt_tab::on_window_open(id);
}

static void on_window_closed(int id) {
    titlebar::on_window_closed(id);
    alt_tab::on_window_closed(id);
    //notify("close: " + std::to_string(id));
    //notify("monitors: " + std::to_string(monitors.size()));

    for (auto m : monitors) {
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
}

static void on_layer_change() {
    // move snapped windows
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
    monitors.push_back(c);

    static bool skip = false;
    if (skip) {
        skip = false;
    } else {
        //test_container(c);
        //nz("make");
    }

    auto cid = datum<int>(c, "cid");
    *cid = id;
}

static void on_monitor_closed(int id) {
    for (int i = monitors.size() - 1; i >= 0; i--) {
        auto cid = *datum<int>(monitors[i], "cid");
        if (cid == id) {
            monitors.erase(monitors.begin() + i);
        }
    }
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

    Bounds mbounds;
    for (auto r : monitors) {
        auto cid = *datum<int>(r, "cid");
        hypriso->damage_entire(cid);
        if (cid == current_monitor) {
            mbounds = r->real_bounds;
            *datum<int>(r, "stage") = stage;
            *datum<int>(r, "active_id") = active_id;
            paint_outline(r, r);
        }
    }
    auto mo = mouse();
    if (stage == (int) STAGE::RENDER_LAST_MOMENT) {
        Bounds bb;
        for (auto r : monitors) {
            for (auto c : r->children) {
                auto cid = *datum<int>(c, "cid");
                if (hypriso->has_focus(cid)) {
                    bb = bounds_client(cid);
                }
            }
        }
        draw_text(fz("{:.2f} {:.2f}\n{} {} {} {}\n{} {} {} {}", 
            mo.x, mo.y, mbounds.x, mbounds.y, mbounds.w, mbounds.h,
            bb.x, bb.y, bb.w, bb.h), 
            0, 50);

        auto rid = current_rendering_monitor();
        auto cbounds = bounds_monitor(rid);
        auto mou = mouse();
        auto s = scale(rid);
        //rect(Bounds(mou.x * s - cbounds.x * s, mou.y * s - cbounds.y * s, 100, 100), {1, 1, 1, 1});
        
        
    }
}

static void on_drag_start_requested(int id) {
    //drag::begin(id);
}

static void on_resize_start_requested(int id, RESIZE_TYPE type) {

}

static void on_config_reload() {

}

void second::begin() {
//#ifdef TRACY_ENABLE
    //tracy::StartupProfiler();
//#endif
    
    on_any_container_close = any_container_closed;
    
    hypriso->create_config_variables();
        
    hypriso->on_mouse_press = on_mouse_press;
    hypriso->on_mouse_move = on_mouse_move;
    hypriso->on_key_press = on_key_press;
    hypriso->on_scrolled = on_scrolled;
    hypriso->on_draw_decos = on_draw_decos;
    hypriso->on_render = on_render;
    hypriso->on_window_open = on_window_open;
    hypriso->on_window_closed = on_window_closed;
    hypriso->on_layer_change = on_layer_change;
    hypriso->on_monitor_open = on_monitor_open;
    hypriso->on_monitor_closed = on_monitor_closed;
    hypriso->on_drag_start_requested = on_drag_start_requested;
    hypriso->on_resize_start_requested = on_resize_start_requested;
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
}

void second::end() {
    hypriso->end();    

//#ifdef TRACY_ENABLE
    //tracy::ShutdownProfiler();
//#endif
}

void second::layout_containers() {
    if (monitors.empty())
        return;
    std::vector<Container *> backup;
    for (auto r : monitors) {
        for (int i = r->children.size() - 1; i >= 0; i--) {
            auto c = r->children[i];
            if (c->custom_type != (int) TYPE::CLIENT) {
                backup.push_back(c);
                r->children.erase(r->children.begin() + i);
            }
        }
    }

    // put client on the correct monitor
    /*std::vector<Container *> clients;
    for (auto r : monitors) {
        for (auto c: clients)
            clients.push_back(c);
        r->children.clear();
    }
    for (auto c : clients) {
        auto cid = *datum<int>(c, "cid");
        int monitor = get_monitor(cid); 
        for (auto r : monitors) {
            auto rid = *datum<int>(r, "cid");
            notify(fz("{} {}", monitor, rid));
            c->parent = r;
            r->children.push_back(c);
        }
    }*/

    // reorder based on stacking
    std::vector<int> order = get_window_stacking_order();
    for (auto r : monitors) {
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
    
    for (auto r : monitors) {
        auto rid = *datum<int>(r, "cid");
        //auto s = scale(rid);
        { // set the monitor bounds
            auto b = bounds_monitor(rid);
            r->real_bounds = Bounds(b.x, b.y, b.w, b.h);
        }

        for (auto c : r->children) {
            auto cid = *datum<int>(c, "cid");
            {
                auto b = bounds_client(cid);            
                //b.scale(s);
                auto fo = hypriso->floating_offset(cid);
               //fo.scale(s);
                auto so = hypriso->workspace_offset(cid);
                //so.scale(s);
                if (hypriso->has_decorations(cid))  {
                    c->real_bounds = Bounds(
                        //b.x + fo.x + so.x, 
                        b.x, 
                        //b.y - titlebar_h + fo.y + so.y, 
                        b.y - titlebar_h, 
                        b.w, 
                        b.h + titlebar_h
                    );
                } else {
                    c->real_bounds = Bounds(
                        b.x + fo.x + so.x, 
                        b.y + fo.y + so.y, 
                        b.w, 
                        b.h
                    );
                }
                ::layout(r, c, c->real_bounds);
            }
        } 
    }

    for (auto c : backup) {
        *datum<bool>(c, "touched") = false;
    }

    for (auto c : backup) {
        if (c->custom_type == (int) TYPE::ALT_TAB) {
            c->parent->children.insert(c->parent->children.begin(), c);
            if (c->pre_layout) {
                c->pre_layout(c->parent, c, c->parent->real_bounds);
                *datum<bool>(c, "touched") = true;
            }
        }
        if (c->custom_type == (int) TYPE::TEST) {
            c->parent->children.insert(c->parent->children.begin(), c);
            if (c->pre_layout) {
                c->pre_layout(c->parent, c, c->parent->real_bounds);
            }
            *datum<bool>(c, "touched") = true;
        }
    }
    for (auto c : backup) {
        if (!(*datum<bool>(c, "touched"))) {
            notify("hey you forgot to layout one of the containers in layout_containers probably leading to it not getting drawn");
        }
    }

    
}

