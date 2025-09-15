#include "startup.h"

#include "first.h"
#include "events.h"
#include "spring.h"
#include "container.h"
#include "hypriso.h"

#include <format>
#include <cassert>

static std::vector<Container *> roots; // monitors
static int titlebar_h = 29;
static int titlebar_icon_h = 12;
RGBA color_titlebar = {1.0, 1.0, 1.0, 1.0};
RGBA color_titlebar_hovered = {0.87, 0.87, 0.87, 1.0f};
RGBA color_titlebar_pressed = {0.69, 0.69, 0.69, 1.0f};
RGBA color_titlebar_hovered_closed = {0.9, 0.1, 0.1, 1.0f};
RGBA color_titlebar_pressed_closed = {0.7, 0.1, 0.1, 1.0f};
RGBA color_titlebar_icon = {0.0, 0.0, 0.0, 1.0};
RGBA color_titlebar_icon_close_pressed = {1.0, 1.0, 1.0, 1.0};
static float title_button_wratio = 1.4375f;
static float rounding = 10.0f;

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

    ClientData(int id) : id(id) {
       ; 
    }
};

CBox tobox(Container *c) {
   return {c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h}; 
}

void layout_every_single_root();

// returning true means consume the event
bool on_mouse_move(int id, float x, float y) {
    Event event(x, y);
    layout_every_single_root();
    for (auto root : roots)
        move_event(root, event);
    return false;
}

// returning true means consume the event
bool on_key_press(int id, int key, int state, bool update_mods) {
    return false;
}

// returning true means consume the event
bool on_scrolled(int id, int source, int axis, int direction, double delta, int discrete, bool mouse) {
    return false; 
}

ThinMonitor *m_from_id(int id) {
    for (auto m : hypriso->monitors)
        if (m->id == id)
            return m;
    return nullptr; 
}

ThinClient *c_from_id(int id) {
    for (auto w : hypriso->windows)
        if (w->id == id)
            return w;
    return nullptr; 
}

void layout_every_single_root() {
    // reorder based on stacking
    std::vector<int> order = get_window_stacking_order();
    for (auto r : roots) {
        // update the index based on the stacking order
        for (auto c : r->children) {
            auto cdata = (ClientData *) c->user_data;
            for (int i = 0; i < order.size(); i++)
                if (order[i] == cdata->id)
                    cdata->index = i;
        }
        // sort the children based on index
        std::sort(r->children.begin(), r->children.end(), [](Container *a, Container *b) {
            auto adata = (ClientData *) a->user_data; 
            auto bdata = (ClientData *) b->user_data; 
            return adata->index > bdata->index; 
        });
    }    
    
    // set bounds of containers 
    for (auto r : roots) {
        auto rdata = (RootData *) r->user_data;
        auto rid = rdata->id;
        auto s = scale(rid);
        if (auto m = m_from_id(rid)) {
            auto b = bounds(m);
            b.scale(s);
            r->real_bounds = Bounds(b.x, b.y, b.w, b.h);
        }

        for (auto c : r->children) {
            auto cdata = (ClientData *) c->user_data;
            auto cid = cdata->id;
            if (auto cm = c_from_id(cid)) {
                auto b = bounds(cm);            
                b.scale(s);
                c->real_bounds = Bounds(
                    b.x, 
                    b.y - titlebar_h * s, 
                    b.w, 
                    b.h + titlebar_h * s
                );
                ::layout(c, c, c->real_bounds);
            }
        } 
    }    
}


// i think this is being called once per monitor
void on_render(int id, int stage) {
    if (stage == (int) STAGE::RENDER_BEGIN) {
         layout_every_single_root();
    }
    
    int current_monitor = current_rendering_monitor();
    int current_window = current_rendering_window();
    int active_id = current_window == -1 ? current_monitor : current_window;
 
    for (auto root : roots) {
        auto rdata = (RootData *) root->user_data;
        rdata->stage = stage;
        rdata->active_id = active_id;
        paint_root(root);
    }
    
    if (stage == (int) STAGE::RENDER_LAST_MOMENT) {
        request_refresh();
    }
}

// returning true means consume the event
bool on_mouse_press(int id, int button, int state, float x, float y) {
    Event event(x, y, button, state);
    layout_every_single_root();
    for (auto root : roots)
        mouse_event(root, event);

    return false;
}

int monitor_overlapping(int id) {
    
    return -1;
}

ThinClient *client_by_id(int id) {
    for (auto c : hypriso->windows)
        if (c->id == id)
            return c;
    return nullptr;
}

ThinMonitor *monitor_by_id(int id) {
    for (auto c : hypriso->monitors)
        if (c->id == id)
            return c;
    return nullptr;
}

void on_window_open(int id) {
    // add a child to root which will rep the window titlebar
    auto tc = new ThinClient(id);
    hypriso->windows.push_back(tc);
    hypriso->reserve_titlebar(tc, titlebar_h);
    set_window_corner_mask(id, 3);

    int monitor = monitor_overlapping(id);
    if (monitor == -1) {
        for (auto r : roots) {
            auto data = (RootData *) r->user_data;
            monitor = data->id;
            break;
        }
    }

   //notify(std::to_string(monitor));

    // TODO: We should put these windows in a limbo vector until a monitor opens and then move them over
    //assert(monitor != -1 && "window opened and there were no monitors avaialable (Mylardesktop bug!)");

    for (auto r : roots) {
        auto data = (RootData *) r->user_data;
        if (data->id == monitor) {
            auto c = r->child(::vbox, FILL_SPACE, FILL_SPACE); // the sizes are set later by layout code
            c->user_data = new ClientData(id);            
            auto s = scale(((RootData *) r->user_data)->id);
            auto title = c->child(::hbox, FILL_SPACE, titlebar_h * s);
            auto content = c->child(::hbox, FILL_SPACE, FILL_SPACE);
            title->when_drag_start = [](Container *root, Container *c) {
                auto data = (ClientData *) c->parent->user_data;
                auto rdata = (RootData *) root->user_data;
                auto s = scale(rdata->id);
                auto b = bounds(c_from_id(data->id));
                auto client = client_by_id(data->id);
                client->initial_x = b.x * s;
                client->initial_y = b.y * s;
                setCursorImageUntilUnset("grabbing");
            };
            title->when_drag = [](Container *root, Container *c) {
                auto data = (ClientData *) c->parent->user_data;
                auto client = client_by_id(data->id);
                auto newx = client->initial_x + (root->mouse_current_x - root->mouse_initial_x);
                auto newy = client->initial_y + (root->mouse_current_y - root->mouse_initial_y);
                move(data->id, newx, newy);
            };
            title->when_drag_end = [](Container *root, Container *c) {
                unsetCursorImage();
            };
            title->when_paint = [](Container *root, Container *c) {
                auto data = (ClientData *) c->parent->user_data;
                auto rdata = (RootData *) root->user_data;
                if (data->id == rdata->active_id) {
                    rect(c->real_bounds, color_titlebar, 12, rounding * scale(rdata->id));
                }
            };
            title->alignment = ALIGN_RIGHT;
            struct TitleData {
                long previous = 0;
            };
            title->when_clicked = [](Container *root, Container *c) {
                auto data = (TitleData *) c->user_data;
                long current = get_current_time_in_ms();
                notify("title clicked");
                if (current - data->previous < 300) {
                    notify("toggle max");
                }
                data->previous = current;
            };
            title->user_data = new TitleData;

            struct IconData {
                bool attempted = false;
                TextureInfo main;
                TextureInfo secondary;
            };
            
            auto min = title->child(100, FILL_SPACE);
            min->user_data = new IconData;
            min->when_paint = [](Container *root, Container *c) {
                auto data = (ClientData *) c->parent->parent->user_data;
                auto rdata = (RootData *) root->user_data;
                auto cdata = (IconData *) c->user_data;
                auto s = scale(rdata->id);
                if (data->id == rdata->active_id) {
                    if (c->state.mouse_pressing) {
                        rect(c->real_bounds, color_titlebar_pressed);
                    } else if (c->state.mouse_hovering) {
                        rect(c->real_bounds, color_titlebar_hovered);
                    }

                    if (!cdata->attempted) {
                        cdata->attempted = true;
                        cdata->main = gen_text_texture("Segoe Fluent Icons", "\ue921",
                            titlebar_icon_h * s, color_titlebar_icon);
                    }
                    if (cdata->main.id != -1) {
                        draw_texture(cdata->main,
                            c->real_bounds.x + c->real_bounds.w * .5 - cdata->main.w * .5,
                            c->real_bounds.y + c->real_bounds.h * .5 - cdata->main.h * .5);
                    }
                }
            };
            min->pre_layout = [](Container *root, Container *c, const Bounds &b) {
                c->wanted_bounds.w = b.h * title_button_wratio;
            };
            min->when_clicked = [](Container *root, Container *C)  {
                notify("min");
            };
            auto max = title->child(100, FILL_SPACE);
            max->user_data = new IconData;
            max->when_paint = [](Container *root, Container *c) {
                auto data = (ClientData *) c->parent->parent->user_data;
                auto rdata = (RootData *) root->user_data;
                auto cdata = (IconData *) c->user_data;
                auto s = scale(rdata->id);
                auto client = client_by_id(data->id);
                if (data->id == rdata->active_id) {
                    if (c->state.mouse_pressing) {
                        rect(c->real_bounds, color_titlebar_pressed);
                    } else if (c->state.mouse_hovering) {
                        rect(c->real_bounds, color_titlebar_hovered);
                    }

                    if (!cdata->attempted) {
                        cdata->attempted = true;
                        cdata->main = gen_text_texture("Segoe Fluent Icons", "\ue922",
                            titlebar_icon_h * s, color_titlebar_icon);
                        // demax
                        cdata->secondary = gen_text_texture("Segoe Fluent Icons", "\ue923",
                            titlebar_icon_h * s, color_titlebar_icon);
                    }
                    if (cdata->main.id != -1) {
                        auto texid = cdata->main;
                        if (client->maximized)
                           texid = cdata->secondary; 
                        draw_texture(texid,
                            c->real_bounds.x + c->real_bounds.w * .5 - cdata->main.w * .5,
                            c->real_bounds.y + c->real_bounds.h * .5 - cdata->main.h * .5);
                    }
                }
            };
            max->pre_layout = [](Container *root, Container *c, const Bounds &b) {
                c->wanted_bounds.w = b.h * title_button_wratio;
            };
            max->when_clicked = [](Container *root, Container *c)  {
                auto cdata = (ClientData *) c->parent->parent->user_data;
                auto client = client_by_id(cdata->id);
                if (cdata) {
                    client->maximized = !client->maximized;
                }
                notify("toggle max");
            };
            auto close = title->child(100, FILL_SPACE);
            close->user_data = new IconData;
            close->when_paint = [](Container *root, Container *c) {
                auto data = (ClientData *) c->parent->parent->user_data;
                auto rdata = (RootData *) root->user_data;
                auto cdata = (IconData *) c->user_data;
                auto s = scale(rdata->id);
                if (data->id == rdata->active_id) {
                    if (c->state.mouse_pressing) {
                        rect(c->real_bounds, color_titlebar_pressed_closed, 13, 10 * scale(rdata->id));
                    } else if (c->state.mouse_hovering) {
                        rect(c->real_bounds, color_titlebar_hovered_closed, 13, 10 * scale(rdata->id));
                    }

                    if (!cdata->attempted) {
                        cdata->attempted = true;
                        cdata->main = gen_text_texture("Segoe Fluent Icons", "\ue8bb",
                            titlebar_icon_h * s, color_titlebar_icon);
                        cdata->secondary = gen_text_texture("Segoe Fluent Icons", "\ue8bb",
                            titlebar_icon_h * s, color_titlebar_icon_close_pressed);
                    }
                    if (cdata->main.id != -1) {
                        auto texid = cdata->main;
                        if (c->state.mouse_pressing || c->state.mouse_hovering) {
                            texid = cdata->secondary;
                        }
                        draw_texture(texid,
                            c->real_bounds.x + c->real_bounds.w * .5 - cdata->main.w * .5,
                            c->real_bounds.y + c->real_bounds.h * .5 - cdata->main.h * .5);
                    }
                }
            };
            close->pre_layout = [](Container *root, Container *c, const Bounds &b) {
                c->wanted_bounds.w = b.h * title_button_wratio;
            };
            close->when_clicked = [](Container *root, Container *C)  {
                notify("close");
            };
            

            break;
        }
    }
}

void on_window_closed(int id) {
    for (int i = 0; i < hypriso->windows.size(); i++) {
        auto w = hypriso->windows[i];
        if (w->id == id) {
            delete w;
            hypriso->windows.erase(hypriso->windows.begin() + i); 
        }
    }

    for (auto r : roots) {
        for (int i = 0; i < r->children.size(); i++) {
            auto data = (ClientData *) r->children[i]->user_data;
            if (data->id == id) {
                delete r->children[i];
                r->children.erase(r->children.begin() + i);
            }
        } 
    }
}

void on_monitor_open(int id) {
    auto tm = new ThinMonitor(id);
    hypriso->monitors.push_back(tm);

    auto c = new Container(layout_type::absolute, FILL_SPACE, FILL_SPACE);
    c->user_data = new RootData(id);
    c->when_paint = [](Container *root, Container *c) {
    };
    roots.push_back(c);
}

void on_monitor_closed(int id) {
    for (int i = 0; i < hypriso->monitors.size(); i++) {
        auto m = hypriso->monitors[i];
        if (m->id == id) {
            delete m;
            hypriso->monitors.erase(hypriso->monitors.begin() + i); 
        }
    } 
    for (int i = 0; i < roots.size(); i++) {
        auto data = (RootData *) roots[i]->user_data;
        if (data->id == id) {
            delete roots[i];
            roots.erase(roots.begin() + i);
        }
    } 
}

void startup::begin() {
    hypriso->on_mouse_press = on_mouse_press;
    hypriso->on_mouse_move = on_mouse_move;
    hypriso->on_key_press = on_key_press;
    hypriso->on_scrolled = on_scrolled;
    hypriso->on_render = on_render;
    hypriso->on_window_open = on_window_open;
    hypriso->on_window_closed = on_window_closed;
    hypriso->on_monitor_open = on_monitor_open;
    hypriso->on_monitor_closed = on_monitor_closed;

	// The two most important callbacks we hook are mouse move and mouse events
	// On every mouse move we update the current state of the ThinClients to be in the right positions
	// so that hen we receive a mouse down, we know if we have to consume it (snap resizing, title bar interactions, alt tab menu, overview dragging, overview drop down, desktop folders, desktop folder selection, so on)
 
    // hooks need to be created last because otherwise we miss initial loading of all windows with on_window_open
	hypriso->create_hooks_and_callbacks(); 

	/*auto b = root->child(::vbox, 200, 200);
	b->when_paint = [](Container *root, Container *c) {
    	if (c->state.mouse_pressing) {
            rect(tobox(c), {1, 1, 0, 1});	
    	} else {
            //rect(tobox(c), {1, 1, 1, 1});	
            border(tobox(c), {1, 1, 1, 1}, 10.0f);
    	}
	};
	*/
}

void startup::end() {
    hypriso->end();
}


