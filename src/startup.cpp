#include "startup.h"

#include "first.h"
#include "events.h"
#include "icons.h"
#include "spring.h"
#include "container.h"
#include "hypriso.h"

#include <format>
#include <cassert>
#include <linux/input-event-codes.h>

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

static std::vector<Container *> roots; // monitors
static int titlebar_text_h = 15;
static int titlebar_icon_button_h = 14;
static int titlebar_icon_h = 24;
static int titlebar_icon_pad = 8;
static int resize_size = 18;
static int tab_menu_font_h = 40;
RGBA color_titlebar = {1.0, 1.0, 1.0, 1.0};
RGBA color_titlebar_hovered = {0.87, 0.87, 0.87, 1.0f};
RGBA color_titlebar_pressed = {0.69, 0.69, 0.69, 1.0f};
RGBA color_titlebar_hovered_closed = {0.9, 0.1, 0.1, 1.0f};
RGBA color_titlebar_pressed_closed = {0.7, 0.1, 0.1, 1.0f};
RGBA color_titlebar_icon = {0.0, 0.0, 0.0, 1.0};
RGBA color_titlebar_icon_close_pressed = {1.0, 1.0, 1.0, 1.0};
static float title_button_wratio = 1.4375f;
static float rounding = 10.0f;

ThinClient *c_from_id(int id);
ThinMonitor *m_from_id(int id);

enum struct TYPE : uint8_t {
    NONE = 0,
    RESIZE_HANDLE, // The handle that exists between two snapped winodws
    CLIENT_RESIZE, // The resize that exists around a window
    CLIENT, // Windows
    ALT_TAB,
};

static std::string to_lower(const std::string& str) {
    std::string result;
    result.reserve(str.size()); // avoid reallocations

    std::transform(str.begin(), str.end(), std::back_inserter(result), [](unsigned char c) { return std::tolower(c); });
    return result;
}

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

struct AltTabOption {
    std::string class_name;
    std::string title;
    int window;
};

void reset_visible_window();
void update_visible_window();
void tab_next_window();

// The reason we take screenshots like this is because if we try to do it in the render thread, we CRASH for some reason that I don't feel like investigating. PLUS, we need to take screenshots on a perdiodic basis anyways so this solves both problems.
Timer* start_producing_thumbnails() {
    //static float fps24 = 1000.0f / 24.0f;
    float fps = 1000.0f / 12.0f;
    Timer* timer = later(nullptr, fps, [](Timer* timer) { hypriso->screenshot_all(); });
    timer->keep_running = true;
    return timer;
}

class AltTabMenu {
  public:
    bool showing = false;

    Container* root  = nullptr;
    int index = 0;
    std::vector<AltTabOption> options;
    Timer *timer = nullptr;

    long switch_time = 0;
    int previous_index = 0;
    long time_when_open = 0;
    long time_when_change = 0;

    void change_showing(bool state) {
        auto m_windows = get_window_stacking_order();
        for (int i = options.size() - 1; i >= 0; i--) {
            auto& o = options[i];
            bool  found = false;
            for (auto w : m_windows)
                if (o.window == w)
                    found = true;
            if (!found) {
                options.erase(options.begin() + i);
            }
        }
        // todo get rid of those who exist in options but not in m_windows
        for (auto w : m_windows) {
            bool has_mylar_bar = false;
            for (auto r: roots) {
                for (auto c: r->children) {
                    if (c->custom_type == (int) TYPE::CLIENT) {
                        auto data = (ClientData *) c->user_data;
                        if (data->id == w) {
                            has_mylar_bar = hypriso->has_decorations(data->id);
                        }
                    }
                }
            }

            if (has_mylar_bar) {
                AltTabOption o;
                bool skip = false;
                for (auto option : options)
                    if (option.window == w)
                        skip = true;
                if (skip)
                    continue;
                auto c = c_from_id(w);
                o.class_name = class_name(c);
                o.title = title_name(c);
                o.window = w;
                options.insert(options.begin(), o);
            }
        }
        
        if (showing != state) {
            if (root && state) {
                delete root;
                root = nullptr;
            }
            if (state) {
                index = 0;
                time_when_open = get_current_time_in_ms();
                time_when_change = time_when_open - 80;
            } else {
                //activate option at index
                if (!options.empty() && index < options.size()) {
                    auto& o = options[index];
                    auto w = o.window;
                    auto client = c_from_id(w);
                    if (client->iconified) { // problem with preview making it not hidden when it is
                        hypriso->iconify(w, false);
                        //iconify(w, false);
                    }
                    hypriso->bring_to_front(w);
                }
                options.clear();
            }

            if (state) {
                update_visible_window();
                hypriso->screenshot_all();
                timer = start_producing_thumbnails();
                timer->keep_running = true;
            } else {
                // stop taking screenshots
                if (timer)
                    timer->keep_running = false;

                // go through windows and set iconify based on perwindow data
                reset_visible_window();
            }

            request_refresh();
        }
        showing = state;
    }

    bool is_showing() {
        return showing;
    }
};

static AltTabMenu alt_tab_menu;

void reset_visible_window() {
    return;
    for (auto& w : get_window_stacking_order()) {
        auto client = c_from_id(w);
        hypriso->iconify(client->id, client->iconified) ;
    }
}

void update_visible_window() {
    return;
    for (auto& w : get_window_stacking_order()) {
        auto client = c_from_id(w);
        auto& o = alt_tab_menu.options[alt_tab_menu.index];
        bool is_hidden = client->iconified;
        if (w == o.window && is_hidden) {
            hypriso->iconify(w, false);
        } else if (!is_hidden) {
            hypriso->iconify(w, true);
        }
        if (w == o.window) {
            hypriso->iconify(w, false);
        }
    }
}

void layout_every_single_root();

void drag_update() {
    auto client = c_from_id(hypriso->dragging_id);
    auto m = mouse();
    auto newx = hypriso->drag_initial_window_pos.x + (m.x - hypriso->drag_initial_mouse_pos.x);
    auto newy = hypriso->drag_initial_window_pos.y + (m.y - hypriso->drag_initial_mouse_pos.y);
    hypriso->move(hypriso->dragging_id, newx, newy);
    hypriso->bring_to_front(hypriso->dragging_id);
}

void resize_start(int id, RESIZE_TYPE type) {
    
}

void resize_update() {
    
}

void resize_stop() {
    
}

void drag_start(int id) {
    hypriso->dragging_id = id;
    hypriso->dragging = true; 
    auto client = c_from_id(hypriso->dragging_id);
    auto b = bounds(client);
    auto MOUSECOORDS = mouse();
    auto mid = get_monitor(client->id);
    auto monitor = m_from_id(mid);
    auto mb = bounds(monitor);
    if (client->snapped) {
        client->snapped = false;
        hypriso->should_round(client->id, true);
        auto s = scale(mid);
        float perc = (MOUSECOORDS.x * s - b.x) / b.w;
        client->drag_initial_mouse_percentage = perc;
        hypriso->move_resize(client->id, MOUSECOORDS.x - (perc * (client->pre_snap_bounds.w * (1.0 / s))), b.y, client->pre_snap_bounds.w, client->pre_snap_bounds.h);
        b = bounds(client);
    } else {
        client->pre_snap_bounds = b;
    }
    hypriso->drag_initial_window_pos = b;
    hypriso->drag_initial_mouse_pos = mouse();
    setCursorImageUntilUnset("grabbing");

    drag_update();
}

SnapPosition mouse_to_snap_position(int mon, int x, int y) {
    Bounds pos = bounds(m_from_id(mon));

    const float edgeThresh     = 20.0;
    const float sideThreshX    = pos.w * 0.05f;
    const float sideThreshY    = pos.h * 0.05f;
    const float rightEdge      = pos.x + pos.w;
    const float bottomEdge     = pos.y + pos.h;
    bool        on_top_edge    = y < pos.y + edgeThresh;
    bool        on_bottom_edge = y > bottomEdge - edgeThresh;
    bool        on_left_edge   = x < pos.x + edgeThresh;
    bool        on_right_edge  = x > rightEdge - edgeThresh;
    bool        on_left_side   = x < pos.x + sideThreshX;
    bool        on_right_side  = x > rightEdge - sideThreshX;
    bool        on_top_side    = y < pos.y + sideThreshY;
    bool        on_bottom_side = y > bottomEdge - sideThreshY;

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
    Bounds screen = bounds_reserved(m_from_id(mon));
    
    float x = screen.x;
    float y = screen.y;
    float w = screen.w;
    float h = screen.h;

    Bounds out = {x, y, w, h};

    if (pos == SnapPosition::MAX) {
        return {x, y, w, h};
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

void drag_stop() {
    int window = hypriso->dragging_id;
    drag_update();
    hypriso->dragging_id = -1;
    hypriso->dragging = false;
    unsetCursorImage();

    int mon = hypriso->monitor_from_cursor();
    auto m = mouse();
    auto snap_position = mouse_to_snap_position(mon, m.x, m.y);
    Bounds position = snap_position_to_bounds(mon, snap_position);
    auto c = c_from_id(window);
    if (snap_position == SnapPosition::NONE) {
        auto newx = hypriso->drag_initial_window_pos.x + (m.x - hypriso->drag_initial_mouse_pos.x);
        auto newy = hypriso->drag_initial_window_pos.y + (m.y - hypriso->drag_initial_mouse_pos.y);
        position.x = newx;
        position.y = newy;
        hypriso->move(window, position.x, position.y);
    } else {
        c->snapped = true;
        hypriso->should_round(c->id, false);
        c->pre_snap_bounds = bounds(c);
        position.y += titlebar_h;
        position.h -= titlebar_h;
        hypriso->move_resize(window, position.x, position.y, position.w, position.h);
    }
}

void toggle_maximize(int id) {
    auto c = c_from_id(id);
    int mon = hypriso->monitor_from_cursor();
    if (c->snapped) {
        hypriso->move_resize(id, c->pre_snap_bounds.x, c->pre_snap_bounds.y, c->pre_snap_bounds.w, c->pre_snap_bounds.h);
    } else {
        Bounds position = snap_position_to_bounds(mon, SnapPosition::MAX);
        c->pre_snap_bounds = bounds(c);
        hypriso->move_resize(id, position.x, position.y + titlebar_h, position.w, position.h - titlebar_h);
    }
    c->snapped = !c->snapped;
}

// returning true means consume the event
bool on_mouse_move(int id, float x, float y) {
    if (hypriso->dragging)
        drag_update();
    
    Event event(x, y);
    layout_every_single_root();
    for (auto root : roots)
        move_event(root, event);

    if (hypriso->dragging)
        return true;
    if (hypriso->resizing)
        return true;

    {
        static bool has_done_window_switch = false;
        bool no_fullscreens = true;
        for (auto w : get_window_stacking_order())
            if (hypriso->is_fullscreen(w))
                no_fullscreens = false;
        int m = hypriso->monitor_from_cursor();
        auto s = scale(m);
        auto current_coords = mouse();
        current_coords.scale(s);
        for (auto r : roots) {
            auto rdata = (RootData *) r->user_data;
            if (rdata->id != m)
                continue;
            if (current_coords.x <= r->real_bounds.x + 1) {
                if (!has_done_window_switch && no_fullscreens) {
                    has_done_window_switch = true;

                    if (current_coords.y < r->real_bounds.h * .4) {
                        // focus spotify
                        bool found = false;
                        for (auto w : get_window_stacking_order()) {
                            auto client = c_from_id(w);
                            if (class_name(client) == "Spotify") {
                                found = true;
                                hypriso->bring_to_front(w);
                            }
                        }
                        if (!found) {
                            system("nohup bash -c 'spotify &'");
                        } else {
                            later(nullptr, 100, [](Timer* data) {
                                hypriso->send_key(KEY_SPACE);
                                later(nullptr, 100, [](Timer* data) {
                                    alt_tab_menu.change_showing(true);
                                    tab_next_window();
                                    alt_tab_menu.change_showing(false);
                                });
                            });
                        }
                    } else {
                        alt_tab_menu.change_showing(true);
                        tab_next_window();
                        alt_tab_menu.change_showing(false);
                    }
                }
            } else {
                has_done_window_switch = false;
            }
        }
    }

    bool consumed = false;
    for (auto root : roots) {
       if (root->consumed_event) {
           consumed = true;
           root->consumed_event = false;
       } 
    }

    return consumed;
}

void tab_next_window() {
    alt_tab_menu.previous_index = alt_tab_menu.index;
    alt_tab_menu.index++;
    if (alt_tab_menu.index >= alt_tab_menu.options.size()) {
        alt_tab_menu.index = 0;
    }
    update_visible_window();
    alt_tab_menu.time_when_change = get_current_time_in_ms();
    /*
    auto w = alt_tab_menu.stored[alt_tab_menu.index]; 
    switchToWindow(w.lock(), true);
    g_pCompositor->changeWindowZOrder(w.lock(), true);
    */
}

void tab_previous_window() {
    alt_tab_menu.index--;
    if (alt_tab_menu.index < 0) {
        alt_tab_menu.index = alt_tab_menu.options.size() - 1;
    }
    update_visible_window();
    alt_tab_menu.time_when_change = get_current_time_in_ms();

    /*
    auto w = alt_tab_menu.stored[alt_tab_menu.index]; 
    switchToWindow(w.lock(), true);
    g_pCompositor->changeWindowZOrder(w.lock(), true);
    */
}


// returning true means consume the event
bool on_key_press(int id, int key, int state, bool update_mods) {
    static bool alt_down   = false;
    static bool shift_down = false;
    
    if (key == KEY_ESC && state == 0) {
        if (hypriso->dragging) {
            drag_stop();
        }
        if (hypriso->resizing) {
            resize_stop();
        }
    }
    
    if (key == KEY_LEFTALT || key == KEY_RIGHTALT) {
        alt_down = state;
        if (state == 0) {
            alt_tab_menu.change_showing(false);
        }
    }

    if (key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT) {
        shift_down = state;
    }

    if (key == KEY_TAB) { // on tab release
        if (alt_down) {
            alt_tab_menu.change_showing(true);
            if (shift_down) {
                if (state == 1)
                    tab_previous_window();
            } else {
                if (state == 1)
                    tab_next_window();
            }
        }
    }

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
    std::vector<Container *> backup;
    for (auto r : roots) {
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
    for (auto b: backup) {
        if (b->custom_type == (int) TYPE::CLIENT_RESIZE) {
            auto bdata = (ClientData *) b->user_data;
            for (auto r: roots) {
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
        } else if (b->custom_type == (int) TYPE::ALT_TAB) {
            for (auto r: roots) {
                auto rdata = (RootData *) r->user_data;
                b->exists = alt_tab_menu.is_showing();
                auto count = alt_tab_menu.options.size();
                if (count == 0)
                    count++;
                float w = 500;
                float h = tab_menu_font_h * count * scale(rdata->id);
                b->real_bounds = Bounds(
                    r->real_bounds.x + r->real_bounds.w * .5 - w * .5, 
                    r->real_bounds.y + r->real_bounds.h * .5 - h * .5, 
                    w, h);
                r->children.insert(r->children.begin() + 0, b);
                // TODO maybe show alt tab menu on active monitor?
                break;
            }
        } 
    }
    /*for (auto r : roots) {
        for (int i = r->children.size() - 1; i >= 0; i--) {
            auto c = r->children[i];
            
        }
    }*/
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
        for (auto root : roots) {
            auto rdata = (RootData *) root->user_data;
            // TODO: how costly is this exactly?
            hypriso->damage_entire(rdata->id);
        }
 
        //request_refresh();
    }
}

// returning true means consume the event
bool on_mouse_press(int id, int button, int state, float x, float y) {
    Event event(x, y, button, state);
    layout_every_single_root();
    for (auto root : roots)
        mouse_event(root, event);

    bool consumed = false;
    for (auto root : roots) {
       if (root->consumed_event) {
           consumed = true;
           root->consumed_event = false;
       } 
    }

    if (state == 0) {
        if (hypriso->dragging) {
           drag_stop(); 
        } else if (hypriso->resizing) {
           resize_stop(); 
        }
    }

    return consumed;
}

int monitor_overlapping(int id) {
    
    return -1;
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
            struct IconData : UserData {
                bool attempted = false;
                TextureInfo main;
                TextureInfo secondary;
            };

            struct TitleData : UserData {
                long previous = 0;

                TextureInfo main;
                std::string cached_text;

                TextureInfo icon;
            };

            auto resize = r->child(::vbox, FILL_SPACE, FILL_SPACE); // the sizes are set later by layout code
            resize->custom_type = (int) TYPE::CLIENT_RESIZE;
            resize->when_mouse_enters_container = [](Container *root, Container *c) {
                root->consumed_event = true;
                auto cdata = (ClientData *) c->user_data;
                auto client = c_from_id(cdata->id);
                auto rdata = (RootData *) root->user_data;
                if (client->resizing) {
                    return;
                }
                auto box = c->real_bounds;
                auto s = scale(rdata->id);
                auto m = mouse();
                m.scale(s);
                box.shrink(resize_size * s);
                int corner = 20 * s;
                client->resize_type = 0;
                bool left = false;
                bool right = false;
                bool top = false;
                bool bottom = false;
                if (m.x < box.x + corner)
                    left = true;
                if (m.x > box.x + box.w - corner)
                    right = true;
                if (m.y < box.y + corner)
                    top = true;
                if (m.y > box.y + box.h - corner)
                    bottom = true;
                if (top && left) {
                    client->resize_type = (int) RESIZE_TYPE::TOP_LEFT;
                } else if (top && right) {
                    client->resize_type = (int) RESIZE_TYPE::TOP_RIGHT;
                } else if (bottom && left) {
                    client->resize_type = (int) RESIZE_TYPE::BOTTOM_LEFT;
                } else if (bottom && right) {
                    client->resize_type = (int) RESIZE_TYPE::BOTTOM_RIGHT;
                } else if (top) {
                    client->resize_type = (int) RESIZE_TYPE::TOP;
                } else if (right) {
                    client->resize_type = (int) RESIZE_TYPE::RIGHT;
                } else if (bottom) {
                    client->resize_type = (int) RESIZE_TYPE::BOTTOM;
                } else if (left) {
                    client->resize_type = (int) RESIZE_TYPE::LEFT;
                }

                update_cursor(client->resize_type);
            };
            resize->when_mouse_leaves_container = [](Container *root, Container *c) {
                auto cdata = (ClientData *) c->user_data;
                auto client = c_from_id(cdata->id);
                client->resize_type = 0;
                update_cursor(client->resize_type);
            };
            resize->when_mouse_motion = [](Container *root, Container *c) {
                root->consumed_event = true;
                c->when_mouse_enters_container(root, c);
            };
            resize->when_mouse_down = [](Container *root, Container *c) {
                root->consumed_event = true;
            };
            resize->when_drag = [](Container *root, Container *c) {
                auto cdata = (ClientData *) c->user_data;
                auto rdata = (RootData *) c->user_data;
                auto s = scale(rdata->id); 
                auto client = c_from_id(cdata->id); 
                if (client->resizing) {
                    auto m = mouse();
                    Bounds diff = {m.x - client->initial_x, m.y - client->initial_y, 0, 0};
                    int change_x = 0;
                    int change_y = 0;
                    int change_w = 0;
                    int change_h = 0;

                    if (client->resize_type == (int) RESIZE_TYPE::NONE) {
                    } else if (client->resize_type == (int) RESIZE_TYPE::BOTTOM) {
                        change_h += diff.y;
                    } else if (client->resize_type == (int) RESIZE_TYPE::BOTTOM_LEFT) {
                        change_w -= diff.x;
                        change_x += diff.x;
                        change_h += diff.y;
                    } else if (client->resize_type == (int) RESIZE_TYPE::BOTTOM_RIGHT) {
                        change_w += diff.x;
                        change_h += diff.y;
                    } else if (client->resize_type == (int) RESIZE_TYPE::TOP) {
                        change_y += diff.y;
                        change_h -= diff.y;
                    } else if (client->resize_type == (int) RESIZE_TYPE::TOP_LEFT) {
                        change_y += diff.y;
                        change_h -= diff.y;
                        change_w -= diff.x;
                        change_x += diff.x;
                    } else if (client->resize_type == (int) RESIZE_TYPE::TOP_RIGHT) {
                        change_y += diff.y;
                        change_h -= diff.y;
                        change_w += diff.x;
                    } else if (client->resize_type == (int) RESIZE_TYPE::LEFT) {
                        change_w -= diff.x;
                        change_x += diff.x;
                    } else if (client->resize_type == (int) RESIZE_TYPE::RIGHT) {
                        change_w += diff.x;
                    }

                    // change in x and y shouldn't happen if size is going to be clipped
                    auto size = client->initial_win_box;
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
                    auto min = hypriso->min_size(client->id);
                    if (hypriso->is_x11(client->id)) {
                        min.x /= s;
                        min.y /= s;
                    }
                    if (size.w < min.w) {
                        size.w    = min.w;
                        x_clipped = true;
                    }
                    if (size.h < min.h) {
                        size.h    = min.h;
                        y_clipped = true;
                    }

                    auto pos = client->initial_win_box;
                    auto real = bounds(client);
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
                    hypriso->move_resize(client->id, fb.x, fb.y, fb.w, fb.h);
                }
            };
            resize->when_drag_start = [](Container *root, Container *c) {
                auto cdata = (ClientData *) c->user_data;
                auto client = c_from_id(cdata->id);
                client->resizing = true;
                auto m = mouse();
                client->initial_x = m.x;
                client->initial_y = m.y;
                client->initial_win_box = bounds(client);
                c->when_drag(root, c);
            };
            resize->when_drag_end = [](Container *root, Container *c) {
                auto cdata = (ClientData *) c->user_data;
                auto client = c_from_id(cdata->id);
                client->resizing = false;
                c->when_drag(root, c);
            };
            resize->handles_pierced = [](Container* container, int mouse_x, int mouse_y) {
                auto cdata = (ClientData *) container->user_data;
                auto client = c_from_id(cdata->id);                
                if (client->snapped) {
                    return false; 
                }
                auto b = container->real_bounds;
                auto s = 1.0f;
                for (auto r : roots) {
                    for (auto c : r->children) {
                        if (c == container) {
                            auto rdata = (RootData *) r->user_data;
                            s = scale(rdata->id);
                            break; 
                        }
                    }
                }
                if (bounds_contains(b, mouse_x, mouse_y)) {
                    b.shrink(resize_size * s);
                    if (bounds_contains(b, mouse_x, mouse_y)) {
                        return false;
                    }
                    return true;
                }
                return false; 
            };
            
            resize->skip_delete = true;

            auto c = r->child(::vbox, FILL_SPACE, FILL_SPACE); // the sizes are set later by layout code
            c->custom_type = (int) TYPE::CLIENT;
            c->user_data = new ClientData(id);            
            resize->user_data = c->user_data;
            
            auto thinc = c_from_id(id);
            thinc->uuid = c->uuid;
            auto s = scale(((RootData *) r->user_data)->id);
            auto title = c->child(::hbox, FILL_SPACE, titlebar_h * s);
            title->when_mouse_down = [](Container *root, Container *c) {
                root->consumed_event = true; 
                auto cdata = (ClientData *) c->parent->user_data;
                hypriso->bring_to_front(cdata->id);
                //auto client = c_from_id(hypriso->dragging_id);
            };
            auto content = c->child(::hbox, FILL_SPACE, FILL_SPACE);
            title->when_drag_start = [](Container *root, Container *c) {
                auto data = (ClientData *) c->parent->user_data;
                drag_start(data->id);
            };
            title->when_drag = [](Container *root, Container *c) {
                drag_update();
            };
            title->when_drag_end = [](Container *root, Container *c) {
                drag_stop();
            };
            title->when_paint = [](Container *root, Container *c) {
                auto backup = c->real_bounds;
                //c->real_bounds.h += 1;
                auto data = (ClientData *) c->parent->user_data;
                auto rdata = (RootData *) root->user_data;
                auto s = scale(rdata->id);
                auto client = c_from_id(data->id);
                auto titledata = (TitleData *) c->user_data;
                if (data->id == rdata->active_id) {
                    rect(c->real_bounds, color_titlebar, 12, rounding * scale(rdata->id));
                    auto text = title_name(client);
                    if (titledata->cached_text != text) {
                        if (titledata->main.id != -1) {
                            titledata->main.id = -1;
                            free_text_texture(titledata->main.id);
                        }
                        titledata->main = gen_text_texture("Segoe UI Variable", text, titlebar_text_h * s, color_titlebar_icon);
                        titledata->cached_text = text;
                    }

                    if (titledata->icon.id == -1) {
                        if (icons_loaded) {
                            auto name = class_name(client);
                            auto path = one_shot_icon(titlebar_icon_h * s, {
                                name, c3ic_fix_wm_class(name), to_lower(name), to_lower(c3ic_fix_wm_class(name))
                            });
                            titledata->icon = gen_texture(path, titlebar_icon_h * s);
                        }
                    } else {
                        draw_texture(titledata->icon,
                            c->real_bounds.x + titlebar_icon_pad * s,
                            c->real_bounds.y + (c->real_bounds.h - titledata->icon.h) * .5);
                    }

                    draw_texture(titledata->main,
                        c->real_bounds.x + (titledata->icon.id == -1 ? titlebar_icon_pad * s : titlebar_icon_pad * s * 2 + titledata->icon.w),
                        c->real_bounds.y + (c->real_bounds.h - titledata->main.h) * .5);
                }
                c->real_bounds = backup;
            };
            title->alignment = ALIGN_RIGHT;
            title->when_clicked = [](Container *root, Container *c) {
                auto cdata = (ClientData *) c->parent->user_data;
                auto data = (TitleData *) c->user_data;
                long current = get_current_time_in_ms();
                if (current - data->previous < 300) {
                    toggle_maximize(cdata->id);
                }
                data->previous = current;
            };
            title->user_data = new TitleData;

            auto min = title->child(100, FILL_SPACE);
            min->user_data = new IconData;
            min->when_mouse_down = title->when_mouse_down;
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
                            titlebar_icon_button_h * s, color_titlebar_icon);
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
            max->when_mouse_down = title->when_mouse_down;
            max->when_paint = [](Container *root, Container *c) {
                auto data = (ClientData *) c->parent->parent->user_data;
                auto rdata = (RootData *) root->user_data;
                auto cdata = (IconData *) c->user_data;
                auto s = scale(rdata->id);
                auto client = c_from_id(data->id);
                if (data->id == rdata->active_id) {
                    if (c->state.mouse_pressing) {
                        rect(c->real_bounds, color_titlebar_pressed);
                    } else if (c->state.mouse_hovering) {
                        rect(c->real_bounds, color_titlebar_hovered);
                    }

                    if (!cdata->attempted) {
                        cdata->attempted = true;
                        cdata->main = gen_text_texture("Segoe Fluent Icons", "\ue922",
                            titlebar_icon_button_h * s, color_titlebar_icon);
                        // demax
                        cdata->secondary = gen_text_texture("Segoe Fluent Icons", "\ue923",
                            titlebar_icon_button_h * s, color_titlebar_icon);
                    }
                    if (cdata->main.id != -1) {
                        auto texid = cdata->main;
                        if (client->snapped)
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
                if (cdata) {
                    toggle_maximize(cdata->id);
                }
            };
            auto close = title->child(100, FILL_SPACE);
            close->user_data = new IconData;
            close->when_mouse_down = title->when_mouse_down;
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
                            titlebar_icon_button_h * s, color_titlebar_icon);
                        cdata->secondary = gen_text_texture("Segoe Fluent Icons", "\ue8bb",
                            titlebar_icon_button_h * s, color_titlebar_icon_close_pressed);
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
            close->when_clicked = [](Container *root, Container *c)  {
                auto data = (ClientData *) c->parent->parent->user_data;
                close_window(data->id);
            };
            

            break;
        }
    }
}

void on_window_closed(int id) {
    auto client = c_from_id(id);
    
    for (auto r : roots) {
        for (int i = 0; i < r->children.size(); i++) {
            auto child = r->children[i];
            if (child->uuid == client->uuid) {
                delete r->children[i];
                r->children.erase(r->children.begin() + i);
            }
        } 
    }

    for (int i = 0; i < hypriso->windows.size(); i++) {
        auto w = hypriso->windows[i];
        if (w->id == id) {
            set_window_corner_mask(w->id, 0);
            delete w;
            hypriso->windows.erase(hypriso->windows.begin() + i); 
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

   auto tab_menu = c->child(::vbox, FILL_SPACE, FILL_SPACE); 
   tab_menu->custom_type = (int) TYPE::ALT_TAB;
   tab_menu->when_paint = [](Container *root, Container *c) {
        auto rdata = (RootData *) root->user_data;
        auto s = scale(rdata->id);
        if (RENDER_LAST_MOMENT == rdata->stage) {
            rect(c->real_bounds, {1, 1, 1, 1}); 
            int off = 0;
            for (int i = 0; i < alt_tab_menu.options.size(); i++) {
                auto o = alt_tab_menu.options[i];
                // TODO: slow (should be cached)
                RGBA color = {0, 0, 0, 1};
                float h = tab_menu_font_h * s;
                if (alt_tab_menu.index == i) {
                    color = {.1, .1, .8, 1};
                }
                auto info = gen_text_texture("Segoe UI Variable", o.title, h - 5, color);
                draw_texture(info, c->real_bounds.x + c->real_bounds.h, c->real_bounds.y + off); 
                free_text_texture(info.id);
                auto b = c->real_bounds;
                b.y += off;
                b.w = h;
                b.h = h;
                hypriso->draw_thumbnail(o.window, b);
                off += h;
            }
        }
   };
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

void on_drag_start_requested(int id) {
    drag_start(id);
}

void on_resize_start_requested(int id, RESIZE_TYPE type) { 
    resize_start(id, type);
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
    hypriso->on_drag_start_requested = on_drag_start_requested;
    hypriso->on_resize_start_requested = on_resize_start_requested;

	// The two most important callbacks we hook are mouse move and mouse events
	// On every mouse move we update the current state of the ThinClients to be in the right positions
	// so that hen we receive a mouse down, we know if we have to consume it (snap resizing, title bar interactions, alt tab menu, overview dragging, overview drop down, desktop folders, desktop folder selection, so on)
 
    // hooks need to be created last because otherwise we miss initial loading of all windows with on_window_open
	hypriso->create_hooks_and_callbacks(); 
    if (icon_cache_needs_update()) {
        icon_cache_generate();
        notify("generated");
    }
    {
        notify("icon load");
        icon_cache_load();
    }
}

void startup::end() {
    hypriso->end();
}


