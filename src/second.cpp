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

#include <algorithm>
#include <thread>

std::unordered_map<std::string, Datas> datas;

std::vector<Container *> monitors;

static void any_container_closed(Container *c) {
    remove_data(c->uuid); 
}

static bool on_mouse_move(int id, float x, float y) {
    Event event(x, y);
    second::layout_containers();
    for (auto m : monitors) {
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
    Event event(x, y, button, state);
    second::layout_containers();
    for (auto root : monitors)
        mouse_event(root, event);
    
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
    auto mid = hypriso->monitor_from_cursor();
    auto s = scale(mid);
    Event event;
    event.x = m.x * s;
    event.y = m.y * s;
    event.scroll = true;
    event.axis = axis;
    event.direction = direction;
    event.delta = delta;
    event.descrete = discrete;
    event.from_mouse = from_mouse;
    second::layout_containers();
    for (auto root : monitors)
        mouse_event(root, event);

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
}

static void on_window_closed(int id) {
    titlebar::on_window_closed(id);
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

static void on_monitor_open(int id) {
    auto c = new Container();
    monitors.push_back(c);

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

}

static void on_draw_decos(std::string name, int monitor, int id, float a) {
    titlebar::on_draw_decos(name, monitor, id, a);
}

static void on_render(int id, int stage) {
    if (stage == (int) STAGE::RENDER_BEGIN) {
        second::layout_containers();
    }

    int current_monitor = current_rendering_monitor();
    int current_window = current_rendering_window();
    int active_id = current_window == -1 ? current_monitor : current_window;

    for (auto r : monitors) {
        *datum<int>(r, "stage") = stage;
        *datum<int>(r, "active_id") = active_id;
        //auto pulls = *datum<int>(r, "stage");
        //auto pullid = *datum<int>(r, "active_id");
        //system(fz("echo {} {} >> /tmp/mylarlog", stage, active_id).c_str());
        
        paint_root(r);
    }
}

static void on_drag_start_requested(int id) {

}

static void on_resize_start_requested(int id, RESIZE_TYPE type) {

}

static void on_config_reload() {

}

void second::begin() {
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
}

/*
struct RootData : UserData {
    int id = 0;
    int stage = 0;
    int active_id = 0;
    
    RootData(int id) : id(id) {
       ; 
    }
};

struct ClientData : UserData {
    int id = 0;
    int index = 0; // for reordering based on the stacking order
    float alpha = 1.0;
    std::vector<int> grouped_with;
    bool was_hidden = false;

    ClientData(int id) : id(id) {
       ; 
    }
};
*/


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
    std::vector<Container *> clients;
    for (auto r : monitors) {
        for (auto c: clients)
                clients.push_back(c);
    }
    for (auto c : clients) {
        auto cid = *datum<int>(c, "cid");
        int monitor = get_monitor(cid); 
        for (auto r : monitors) {
            auto rid = *datum<int>(r, "cid");
            if (monitor == -1) // if client not on any monitor, simply put it in first available monitor
                monitor = rid;
            if (rid == monitor) {
                r->children.push_back(c);
                break;
            }
        }
    }

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
        auto s = scale(rid);
        { // set the monitor bounds
            auto b = bounds_monitor(rid);
            b.scale(s);
            r->real_bounds = Bounds(b.x, b.y, b.w, b.h);
        }

        for (auto c : r->children) {
            auto cid = *datum<int>(c, "cid");
            {
                auto b = bounds_client(cid);            
                b.scale(s);
                auto fo = hypriso->floating_offset(cid);
                fo.scale(s);
                auto so = hypriso->workspace_offset(cid);
                so.scale(s);
                if (hypriso->has_decorations(cid))  {
                    c->real_bounds = Bounds(
                        b.x + fo.x + so.x, 
                        b.y - titlebar_h * s + fo.y + so.y, 
                        b.w, 
                        b.h + titlebar_h * s
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
    
    /*
    for (auto b: backup) {
        if (b->custom_type == (int) TYPE::CLIENT_RESIZE) {
            auto bdata = (ClientData *) b->user_data;
            for (auto r: monitors) {
                auto rdata = (RootData *) r->user_data;
                auto s = scale(rdata->id);
                for (int i = 0; i < r->children.size(); i++) {
                    auto c = r->children[i];
                    if (c->custom_type == (int) TYPE::CLIENT) {
                        auto cdata = (ClientData *) c->user_data;
                        if (cdata->id == bdata->id) {                            
                            b->real_bounds = c->real_bounds;
                            b->real_bounds.grow(resize_size * s);
                            r->children.insert(r->children.begin() + i, b);
                            break;
                        }
                    }
                }
            }
        } else if (b->custom_type == (int) TYPE::RESIZE_HANDLE) {            
        } else if (b->custom_type == (int) TYPE::WORKSPACE_SWITCHER) {
            for (auto r: roots) {
                auto rdata = (RootData *) r->user_data;
                auto s = scale(rdata->id);
                b->exists = showing_switcher;
                if (b->pre_layout)
                    b->pre_layout(r, b, r->real_bounds);
                r->children.insert(r->children.begin() + 0, b);
                break;
            } 
        } else if (b->custom_type == (int) TYPE::SNAP_HELPER) {
            for (auto r: roots) {
                auto rdata = (RootData *) r->user_data;
                auto s = scale(rdata->id);

                b->pre_layout(r, b, r->real_bounds);
                
                r->children.insert(r->children.begin() + 0, b);
                break;
            }
        } else if (b->custom_type == (int) TYPE::ALT_TAB) {
            alt_tab_menu->fix_index();
            //scroll->content = alt_tab_menu.root;
            b->exists = alt_tab_menu->is_showing();

            for (auto r: roots) {
                auto rdata = (RootData *) r->user_data;
                auto s = scale(rdata->id);

                alt_tab_menu->root->pre_layout(r, alt_tab_menu->root, r->real_bounds);
                //rect(r->real_bounds, {0, 0, 0, .3});
                //float interthumb_spacing = 20 * s;

                b->children.clear();
                b->children.push_back(alt_tab_menu->root);

                b->real_bounds = r->real_bounds;

                // b->real_bounds.w = alt_tab_menu.root->wanted_bounds.w;
                // b->real_bounds.h = alt_tab_menu.root->wanted_bounds.h;
                // b->real_bounds.x = r->real_bounds.x + r->real_bounds.w * .5 - b->real_bounds.w * .5;
                // b->real_bounds.y = r->real_bounds.y + r->real_bounds.h * .5 - b->real_bounds.h * .5;

                r->children.insert(r->children.begin() + 0, b);
                break;
            }
        }
    }
    */
}
