#include "startup.h"

#include "components.h"
#include "first.h"
#include "events.h"
#include "icons.h"
#include "spring.h"
#include "container.h"
#include "hypriso.h"
#include "client/test.h"

#include <algorithm>
#include <thread>
#include <fstream>
#include <format>
#include <cassert>
#include <filesystem>
#include <linux/input-event-codes.h>
#include <locale>
#include <memory>

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

#define paint [](Container *root, Container *c)
#define fz std::format
#define nz notify

static bool force_damage_all = false;

struct Datas {
    std::unordered_map<std::string, std::any> datas;
};
std::unordered_map<std::string, Datas> datas;


template<typename T>
T *get_data(const std::string& uuid, const std::string& name) {
    // Locate uuid
    auto it_uuid = datas.find(uuid);
    if (it_uuid == datas.end())
        return nullptr;

    // Locate name
    auto it_name = it_uuid->second.datas.find(name);
    if (it_name == it_uuid->second.datas.end())
        return nullptr;

    // Attempt safe cast
    if (auto ptr = std::any_cast<T>(&it_name->second))
        return ptr;

    return nullptr; // type mismatch
}

template<typename T>
void set_data(const std::string& uuid, const std::string& name, T&& value) {
    datas[uuid].datas[name] = std::forward<T>(value);
}

template<typename T>
T *get_or_create(const std::string& uuid, const std::string& name) {
    T *data = get_data<T>(uuid, name);
    if (!data) {
        set_data<T>(uuid, name, T());
        data = get_data<T>(uuid, name);
    }
    return data;
}

void remove_data(const std::string& uuid) {
    datas.erase(uuid);
}

struct TitleData : UserData {
    long previous = 0;

    TextureInfo main_focused;
    TextureInfo main_unfocused;
    std::string cached_text;

    TextureInfo icon;
};

struct IconData : UserData {
    bool attempted = false;
    TextureInfo main;
    TextureInfo secondary;
};

bool any_fullscreen() {
    bool any = false;
    for (auto w : get_window_stacking_order())
        if (hypriso->is_fullscreen(w))
            any = true;
    return any; 
}

static std::vector<Container *> roots; // monitors
static int titlebar_text_h = 15;
static int titlebar_icon_button_h = 13;
static int titlebar_icon_h = 23;
static int titlebar_icon_pad = 8;
static int resize_size = 18;
static int tab_menu_font_h = 40;
static float interspace = 20.0f;
static bool META_PRESSED = false;

//static float thumb_to_position_time = 350;
//static float snap_fadein = 300;
static long thumb_to_position_forward = 0;

static float zoom_factor = 1.0;
static long zoom_nicely_ended_time = 0;

static bool screenshotting = false;
static bool showing_switcher = false;

static float sd = .65;                  // scale down
Bounds max_thumb = {510 * sd, 310 * sd, 510 * sd, 310 * sd}; // need to be multiplied by scale
static float sdd = .3;
Bounds max_space = {510 * sdd, 310 * sdd, 510 * sdd, 310 * sdd}; // need to be multiplied by scale


RGBA color_alt_tab = {1.0, 1.0, 1.0, 0.7};
RGBA color_snap_helper = {1.0, 1.0, 1.0, 0.35};
RGBA color_snap_helper_border = {0.5, 0.5, 0.5, 0.9};

RGBA color_workspace_switcher = {1.0, 1.0, 1.0, 0.55};
RGBA color_workspace_thumb = {0.59, 0.59, 0.59, 1.0f};

RGBA color_titlebar_focused() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_focused_color"); };
RGBA color_titlebar_unfocused() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_unfocused_color"); };

RGBA color_titlebar_hovered = {0.87, 0.87, 0.87, 1.0f};
RGBA color_titlebar_pressed = {0.69, 0.69, 0.69, 1.0f};
RGBA color_titlebar_hovered_closed = {0.9, 0.1, 0.1, 1.0f};
RGBA color_titlebar_pressed_closed = {0.7, 0.1, 0.1, 1.0f};

RGBA color_titlebar_text_focused() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_focused_text_color"); };
RGBA color_titlebar_text_unfocused() { return hypriso->get_varcolor("plugin:mylardesktop:titlebar_unfocused_text_color"); };
//RGBA color_titlebar_icon = {0.0, 0.0, 0.0, 1.0};
RGBA color_titlebar_icon_close_pressed = {1.0, 1.0, 1.0, 1.0};

static float title_button_wratio = 1.4375f;
static float rounding = 10.0f;
static float thumb_rounding = 10.0f;

ThinClient *c_from_id(int id);
ThinMonitor *m_from_id(int id);
void update_restore_info_for(int w);
void remove_snap_helpers();
void perform_snap(ThinClient *c, Bounds position, SnapPosition snap_position, bool create_helper = true, bool instant = true);
void clear_snap_groups(int id);
Bounds snap_position_to_bounds(int mon, SnapPosition pos);

float pull(std::vector<float>& fls, float scalar);

float pull(std::vector<float>& fls, float scalar) {
    if (fls.empty())
        return 0.0f; // or throw an exception

    // Clamp scalar between 0 and 1
    scalar = std::clamp(scalar, 0.0f, 1.0f);

    float fIndex = scalar * (fls.size() - 1); // exact position
    int   i0     = static_cast<int>(std::floor(fIndex));
    int   i1     = static_cast<int>(std::ceil(fIndex));

    if (i0 == i1 || i1 >= fls.size()) {
        return fls[i0];
    }

    float t = fIndex - i0; // fraction between the two indices
    return fls[i0] * (1.0f - t) + fls[i1] * t;
}


// {"anchors":[{"x":0,"y":1},{"x":0.275,"y":0.17500000000000002},{"x":1,"y":0}],"controls":[{"x":0.11310096545097162,"y":0.37717594570583773},{"x":0.3948317346817408,"y":0.03828705681694877}]}
std::vector<float> curve_to_position = { 0, 0.08799999999999997, 0.16900000000000004, 0.245, 0.31399999999999995, 0.378, 0.43700000000000006, 0.491, 0.5409999999999999, 0.587, 0.63, 0.6679999999999999, 0.704, 0.736, 0.765, 0.791, 0.8140000000000001, 0.834, 0.848, 0.86, 0.871, 0.88, 0.888, 0.895, 0.902, 0.908, 0.914, 0.919, 0.924, 0.929, 0.933, 0.937, 0.9410000000000001, 0.945, 0.948, 0.952, 0.955, 0.958, 0.961, 0.963, 0.966, 0.969, 0.971, 0.973, 0.975, 0.978, 0.98, 0.981, 0.983, 0.985, 0.987, 0.988, 0.99, 0.991, 0.993, 0.994, 0.995, 0.997, 0.998, 0.999 };

struct WindowRestoreLocation {
    Bounds box; // all values are 0-1 and supposed to be scaled to monitor

    Bounds actual_size_on_monitor(Bounds m) {
        Bounds b = {box.x * m.w, box.y * m.h, box.w * m.w, box.h * m.h};
        if (b.w < 5)
            b.w = 5;
        if (b.h < 5)
            b.h = 5;
        return b;
    }
};

std::unordered_map<std::string, WindowRestoreLocation> restore_infos;

enum struct TYPE : uint8_t {
    NONE = 0,
    RESIZE_HANDLE, // The handle that exists between two snapped winodws
    CLIENT_RESIZE, // The resize that exists around a window
    CLIENT, // Windows
    ALT_TAB,
    SNAP_HELPER,
    SNAP_THUMB,
    WORKSPACE_SWITCHER,
    WORKSPACE_THUMB,
};

struct SpaceSwitcher {
    bool expanded = false;  
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
    float alpha = 1.0;
    std::vector<int> grouped_with;
    bool was_hidden = false;

    ClientData(int id) : id(id) {
       ; 
    }
};

void reset_visible_window();
void update_visible_window();
void tab_next_window();

void screenshot_all() {
    screenshotting = true;
    hypriso->screenshot_all(); 
    screenshotting = false; 
}

void screenshot_deco(int id) {
    later(nullptr, 1, [id](Timer *t) {
        screenshotting = true;
        hypriso->screenshot_deco(id); 
        screenshotting = false;   
    });
}

// The reason we take screenshots like this is because if we try to do it in the render thread, we CRASH for some reason that I don't feel like investigating. PLUS, we need to take screenshots on a perdiodic basis anyways so this solves both problems.
Timer* start_producing_thumbnails() {
    //static float fps24 = 1000.0f / 24.0f;
    float fps = 1000.0f / 24.0f;
    Timer* timer = later(nullptr, fps, [](Timer* timer) { 
        screenshot_all();
    });
    timer->keep_running = true;
    return timer;
}

struct TabData : UserData {
    int wid = -1;  
    int index = 0;
};

struct AltRoot : UserData {
    Bounds b;  
};

class AltTabMenu {
  public:
    bool showing = false;

    int index = 0;

    Container *root = nullptr;
    Timer *timer = nullptr;

    long switch_time = 0;
    int previous_index = 0;
    long time_when_open = 0;
    long time_when_change = 0;

    AltTabMenu() {
        root = new Container(::absolute, FILL_SPACE, FILL_SPACE);
        root->custom_type = (int) TYPE::ALT_TAB;
        //root->when_paint = paint {
            //rect(c->real_bounds, {1, 0, 0, 1});
        //};
        root->automatically_paint_children = false;
        root->when_paint = paint {
            auto rdata = (RootData *) root->user_data;
            if (rdata->stage != (int) STAGE::RENDER_LAST_MOMENT)
                return;
            auto s = scale(rdata->id);
            auto altroot = (AltRoot *) c->user_data;
            rect(altroot->b, color_alt_tab, 0, thumb_rounding * s);
            for (auto ch : c->children) {
                paint_outline(root, ch);
            }
        };
        root->user_data = new AltRoot;
        root->pre_layout = [](Container* root, Container* self, const Bounds& bound) {
            // set position of all children
            auto rdata = (RootData *) root->user_data;
            auto s = scale(rdata->id);
            float m_max_w = bound.w * .9;
            float m_max_h = bound.h * .9;
            
            float max_w = max_thumb.w * s;
            float max_h = max_thumb.h * s;
            float interthumb_spacing = interspace * s;
            int pen_x = interthumb_spacing;
            int pen_y = interthumb_spacing;
            float hightest_seen_for_row = 0;
            float highest_w_seen_overall = 0;
            float highest_h_seen_overall = 0;
            for (int i = 0; i < self->children.size(); i++) {
                auto t = self->children[i];
                auto tdata = (TabData *) t->user_data;
                auto cdata = c_from_id(tdata->wid);
                auto cb = bounds(cdata);
                // todo needs to set max size and only scale douun to ratio
                float ratio_w = ((cb.w * s) / bound.w);
                float ratio_h = ((cb.h * s) / bound.h);
                if (ratio_h < ratio_w) {
                    float add = 1.0 - ratio_w;
                    ratio_w *= 1.0 + add;
                    ratio_h *= 1.0 + add;
                } else {
                    float add = 1.0 - ratio_h;
                    ratio_w *= 1.0 + add;
                    ratio_h *= 1.0 + add;
                }
                float t_w = max_w * ratio_w;
                float t_h = max_h * ratio_h;
                t_h += titlebar_h * s;
                if (t_h > hightest_seen_for_row)
                    hightest_seen_for_row = t_h;

                if (pen_x + t_w > m_max_w) {
                    pen_x = interthumb_spacing;
                    pen_y += hightest_seen_for_row + interthumb_spacing + titlebar_h * s;
                    hightest_seen_for_row = 0;
                }

                t->real_bounds = Bounds(pen_x, pen_y, t_w, t_h);
                if (pen_x + t_w > highest_w_seen_overall) {
                    highest_w_seen_overall = pen_x + t_w;
                }
                if (pen_y + t_h > highest_h_seen_overall) {
                    highest_h_seen_overall = pen_y + t_h;
                }
 
                ::layout(root, t, t->real_bounds);
               
                pen_x += t_w + interthumb_spacing;
            }

            self->real_bounds = Bounds(0, 0, bound.w, bound.h);
            highest_w_seen_overall += interthumb_spacing;
            highest_h_seen_overall += interthumb_spacing;
            modify_all(self, 
                bound.w * .5 - highest_w_seen_overall * .5, 
                bound.h * .5 - highest_h_seen_overall * .5);
            auto altroot = (AltRoot *) self->user_data;
            altroot->b = Bounds(bound.w * .5 - highest_w_seen_overall * .5, bound.h * .5 - highest_h_seen_overall * .5, highest_w_seen_overall, highest_h_seen_overall);
            altroot->b.grow(interthumb_spacing * .3);
        };
    }

    void change_showing(bool state, bool change_index = true) {
        if (showing != state) {
            if (state) {
                // TODO: order needs to change on change showing based on actual stacking order 
                index = 0;
                time_when_open = get_current_time_in_ms();
                time_when_change = time_when_open - 80;

                auto wids = get_window_stacking_order();
                for (auto t: root->children) {
                   auto tdata = (TabData *) t->user_data;
                   for (int i = 0; i < wids.size(); i++) {
                       if (wids[i] == tdata->wid) {
                           tdata->index = i;
                       }
                   }
                }

                std::sort(root->children.begin(), root->children.end(), [](Container *a, Container *b) {
                    auto adata = (TabData *) a->user_data; 
                    auto bdata = (TabData *) b->user_data; 
                    return adata->index > bdata->index; 
                });
            } else {
                if (change_index) {
                    if (index < root->children.size()) {
                        if (auto c = root->children[index]) {
                            auto tab_data = (TabData *) c->user_data;
                            auto w = tab_data->wid;
                            auto client = c_from_id(w);
                            if (hypriso->is_hidden(w)) { // problem with preview making it not hidden when it is
                                hypriso->set_hidden(w, false);
                            }
                            hypriso->bring_to_front(w);
                        }
                    }
                }
            }

            if (state) {
                update_visible_window();
                screenshot_all();
                if (timer == nullptr)
                    timer = start_producing_thumbnails();
            } else {
                if (timer) {
                    timer->keep_running = false;
                    timer = nullptr;
                }

                reset_visible_window();
            }

            request_refresh();
        }
        showing = state;
    }

    bool is_showing() {
        return showing;
    }

    void fix_index() {
        if (root) {
            if (index >= root->children.size()) {
                index = root->children.size() - 1;
                if (index < 0) {
                    index = 0;
                }
            }
        }
    }

    ~AltTabMenu() {
        //printf("done\n");
    }
};

static std::shared_ptr<AltTabMenu> alt_tab_menu = std::make_shared<AltTabMenu>();

void reset_visible_window() {
    return;
}

void update_visible_window() {
    return;
}

void paint_thumbnail_raw(Container *root, Container *c, float alpha) {
    auto tdata = (TabData *) c->user_data;
    auto rdata = (RootData *) root->user_data;
    auto s = scale(rdata->id);
    hypriso->draw_thumbnail(tdata->wid, c->real_bounds, thumb_rounding * s, 2.0f, 3, alpha);
    if (alt_tab_menu->index <= alt_tab_menu->root->children.size()) {
        auto r = alt_tab_menu->root->children[alt_tab_menu->index];
        if (r == c->parent) {
            rect(c->real_bounds, {1, 1, 1, .2}, 3, thumb_rounding * s, 2.0f, false);
        }
    }
}

void paint_thumbnail(Container *root, Container *c) {
    paint_thumbnail_raw(root, c, 1.0); 
}

void paint_titlebar_raw(Container *root, Container *c, float a, bool subtract_close = true) {
    auto tdata = (TabData *) c->user_data;
    auto rdata = (RootData *) root->user_data;
    auto s = scale(rdata->id);
    
    auto ct = color_titlebar_focused();
    ct.a = a;
    rect(c->real_bounds, ct, 12, thumb_rounding * s, 2.0f, true, a);
    Container *cdata = nullptr;
    for (auto r : roots) {
        for (auto ch : r->children) {
           if (ch->custom_type == (int) TYPE::CLIENT) {
               auto chdata = (ClientData *) ch->user_data;
               if (chdata->id == tdata->wid) {
                   cdata = ch;
                   break;
               }
           }
        }
    }

    if (cdata) {
        if (auto title = container_by_name("title", cdata)) {
            auto td = (TitleData *) title->user_data;
            int xoff = titlebar_icon_pad * s;
            if (td->icon.id != -1) {
                draw_texture(td->icon, c->real_bounds.x + xoff, c->real_bounds.y + c->real_bounds.h * .5 - td->icon.h * .5, a);
                xoff += titlebar_icon_pad * s + td->icon.w;
            }
            if (td->main_focused.id != -1) {
                if (subtract_close) {
                    draw_texture(td->main_focused, c->real_bounds.x + xoff, c->real_bounds.y + c->real_bounds.h * .5 - td->main_focused.h * .5, a,
                             c->real_bounds.w - xoff - c->real_bounds.h * title_button_wratio);
                } else {
                    draw_texture(td->main_focused, c->real_bounds.x + xoff, c->real_bounds.y + c->real_bounds.h * .5 - td->main_focused.h * .5, a,
                             c->real_bounds.w - xoff - titlebar_icon_pad * s);
                }
            }
        }
    }
}

void paint_titlebar(Container *root, Container *c) {
    paint_titlebar_raw(root, c, 1.0);
}

void paint_titlebar_close_raw(Container *root, Container *c, float a) {
    auto tdata = (TabData *) c->user_data;
    auto rdata = (RootData *) root->user_data;
    auto s = scale(rdata->id);
    if (c->state.mouse_pressing) {
        auto color = color_titlebar_pressed_closed;
        color.a = a;
        rect(c->real_bounds, color, 13, thumb_rounding * s, 2.0, true, a);
    } else if (c->state.mouse_hovering) {
        auto color = color_titlebar_hovered_closed;
        color.a = a;
        rect(c->real_bounds, color, 13, thumb_rounding * s, 2.0, true, a);
    }

    Container *cdata = nullptr;
    for (auto r : roots) {
        for (auto ch : r->children) {
           if (ch->custom_type == (int) TYPE::CLIENT) {
               auto chdata = (ClientData *) ch->user_data;
               if (chdata->id == tdata->wid) {
                   cdata = ch;
                   break;
               }
           }
        }
    }

    if (cdata) {
        if (auto close = container_by_name("close", cdata)) {
            auto td = (IconData *) close->user_data;
            if (td->main.id != -1) {
                auto texid = td->main;
                if (c->state.mouse_pressing || c->state.mouse_hovering) {
                    texid = td->secondary;
                }
                draw_texture(texid,
                    c->real_bounds.x + c->real_bounds.w * .5 - td->main.w * .5,
                    c->real_bounds.y + c->real_bounds.h * .5 - td->main.h * .5, a);
            }
        }
    }
}

void paint_titlebar_close(Container *root, Container *c) {
    paint_titlebar_close_raw(root, c, 1.0);
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

ClientData *get_cdata(int id) {
    for (auto r: roots) {
        for (auto c : r->children) {
           if (c->custom_type == (int) TYPE::CLIENT) {
               auto cdata = (ClientData *) c->user_data;
               if (cdata->id == id) {
                   return cdata;
               }
           }
        }
    }
    return nullptr; 
}

void resize_start(int id, RESIZE_TYPE type) {
    hypriso->resizing = true;
    hypriso->resizing_id = id;
    auto client = c_from_id(hypriso->resizing_id);
    if (!client)
        return;
    client->resizing = true;
    auto m = mouse();
    client->initial_x = m.x;
    client->initial_y = m.y;
    client->initial_win_box = bounds(client);
    if (auto cc = get_cdata(id)) {
        for (auto g : cc->grouped_with) {
            auto gg = c_from_id(g);
            gg->initial_x = m.x;
            gg->initial_y = m.y;
            gg->initial_win_box = bounds(gg);
        }
    }
}

bool is_part_of_snap_group(int id) {
    for (auto r: roots) {
        for (auto c : r->children) {
           if (c->custom_type == (int) TYPE::CLIENT) {
               auto cdata = (ClientData *) c->user_data;
               if (cdata->id == id) {
                   return !cdata->grouped_with.empty();
               }
           } 
        }
    }
    return false;
}

void resize_client(ThinClient *client, int resize_type) {
    auto monitor = get_monitor(client->id);
    auto s = scale(monitor);
    auto m = mouse();
    Bounds diff = {m.x - client->initial_x, m.y - client->initial_y, 0, 0};
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
    auto mini = titlebar_h * s * 3 * title_button_wratio + titlebar_icon_button_h * s + titlebar_icon_pad * s * 2;
    if (min.w < mini) {
        min.w = mini;
    }
    min.x = 10;
    min.y = 10;
    min.w = 10;
    min.h = 10;
    
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

bool on_left(ThinClient *c) {
    if (c->snap_type == SnapPosition::LEFT) {
        return true;
    } else if (c->snap_type == SnapPosition::TOP_LEFT) {
        return true;
    } else if (c->snap_type == SnapPosition::BOTTOM_LEFT) {
        return true;
    }
    return false;
}

bool on_top(ThinClient *c) {
    if (c->snap_type == SnapPosition::TOP_LEFT) {
        return true;
    } else if (c->snap_type == SnapPosition::TOP_RIGHT) {
        return true;
    }
    return false;
}

bool on_bottom(ThinClient *c) {
    if (c->snap_type == SnapPosition::BOTTOM_LEFT) {
        return true;
    } else if (c->snap_type == SnapPosition::BOTTOM_RIGHT) {
        return true;
    }
    return false;
}

std::vector<ThinClient *> thin_groups(int id) {
    auto c_data = get_cdata(id);
    std::vector<ThinClient *> clients;
    for (auto g : c_data->grouped_with) {
        if (auto g_data = c_from_id(g)) {
            clients.push_back(g_data);
        }
    }
    return clients;
}

void three_type_dragging(int id, bool left_drag, bool middle_drag, bool right_drag, 
                         float left_perc, float middle_perc, float right_perc,
                         Bounds reserved, Bounds start, Bounds end) {
    float y_off = (end.y - start.y) / reserved.h;
    float x_off = (end.x - start.x) / reserved.w;
    if (x_off > .5)
        x_off = .5;
    if (x_off < -.5)
        x_off = -.5;

    if (left_drag)
        left_perc += y_off;
    if (middle_drag)
        middle_perc += x_off;
    if (right_drag)
        right_perc += y_off;

    auto r = reserved;

    Bounds lt = Bounds(r.x, r.y, r.w * middle_perc, r.h * left_perc);
    
    Bounds lb = Bounds(r.x, r.y + r.h * left_perc, r.w * middle_perc, r.h * (1 - left_perc));
    Bounds rt = Bounds(r.x + r.w * middle_perc, r.y, r.w * (1 - middle_perc), r.h * right_perc);
    Bounds rb = Bounds(r.x + r.w * middle_perc, r.y + r.h * right_perc, r.w * (1 - middle_perc), r.h * (1 - right_perc));
    
    auto gs = thin_groups(id);
    gs.push_back(c_from_id(id));
    for (auto g : gs) {
        if (g->snap_type == SnapPosition::LEFT) {
            hypriso->move_resize(g->id, reserved.x, reserved.y + titlebar_h, reserved.w * middle_perc, reserved.h - titlebar_h);
        } else if (g->snap_type == SnapPosition::RIGHT) {
            hypriso->move_resize(g->id, reserved.x + reserved.w * middle_perc, reserved.y + titlebar_h, reserved.w * (1 - middle_perc), reserved.h - titlebar_h);
        } else if (g->snap_type == SnapPosition::TOP_LEFT) {
            hypriso->move_resize(g->id, lt.x, lt.y + titlebar_h, lt.w, lt.h - titlebar_h);
        } else if (g->snap_type == SnapPosition::BOTTOM_LEFT) {
            hypriso->move_resize(g->id, lb.x, lb.y + titlebar_h, lb.w, lb.h - titlebar_h);
        } else if (g->snap_type == SnapPosition::TOP_RIGHT) {
            hypriso->move_resize(g->id, rt.x, rt.y + titlebar_h, rt.w, rt.h - titlebar_h);
        } else if (g->snap_type == SnapPosition::BOTTOM_RIGHT) {
            hypriso->move_resize(g->id, rb.x, rb.y + titlebar_h, rb.w, rb.h - titlebar_h);
        }
    }
}

void fill_snap_area(int a_id) {
    auto thin_a = c_from_id(a_id);
    auto a_bounds = bounds(thin_a);
    auto top = on_top(thin_a);
    auto left = on_left(thin_a);
    auto mon = get_monitor(a_id);
    auto s = scale(mon);
    auto reserved = bounds_reserved(m_from_id(mon));

    for (auto thin_g : thin_groups(a_id)) {
        auto b_bounds = bounds(thin_g);
        bool b_top = on_top(thin_g);
        bool b_left = on_left(thin_g);
        if (left) {
            if (!b_left) {
                a_bounds.w =  b_bounds.x - a_bounds.x;
            } 
        }
        if (left && top) { //tl
            if (!b_top) {
                a_bounds.h = (b_bounds.y - a_bounds.y);
            }
        } else if (left && !top) { //bl
            if (b_top) {
                //a_bounds.y = 0;
                //a_bounds.h = 0;
            }                      
        } else if (!left && top) { //tr
            
        } else if (!left && !top) { //br
            
        }        
    }
    auto b = a_bounds;
    hypriso->move_resize(a_id, b.x, b.y, b.w, b.h);
}

void resize_update() {
    auto client = c_from_id(hypriso->resizing_id);
    if (client) {
        resize_client(client, client->resize_type);

        bool grouped = is_part_of_snap_group(client->id);
        if (grouped) {
            bool left_drag = true;
            bool middle_drag = true;
            bool right_drag = true;
            float left_perc = .5;
            float middle_perc = .5;
            float right_perc = .5;
            Bounds start = Bounds(client->initial_x, client->initial_y, client->initial_x, client->initial_y);
            int mon = get_monitor(client->id);
            auto s = scale(mon);
            Bounds reserved = bounds_reserved(m_from_id(mon));
            auto gs = thin_groups(client->id);
            gs.push_back(client);
            for (auto g : gs) {
                if (g->snap_type == SnapPosition::TOP_LEFT) {
                    left_perc = (g->initial_win_box.h + titlebar_h) / reserved.h;
                } else if (g->snap_type == SnapPosition::TOP_RIGHT) {
                    right_perc = (g->initial_win_box.h + titlebar_h) / reserved.h;
                } else if (g->snap_type == SnapPosition::BOTTOM_LEFT) {
                    left_perc = (g->initial_win_box.y - titlebar_h) / reserved.h;
                } else if (g->snap_type == SnapPosition::BOTTOM_RIGHT) {
                    right_perc = (g->initial_win_box.y - titlebar_h) / reserved.h;
                } 
            }
            middle_perc = client->initial_win_box.w / reserved.w;
            if (!on_left(client)) {
               middle_perc = 1 - middle_perc;
            }

            three_type_dragging(client->id, left_drag, middle_drag, right_drag, left_perc, middle_perc, right_perc, reserved, start, mouse());
        }
    }
    request_refresh();
}

void resize_stop() {
    hypriso->resizing = false;
    update_restore_info_for(hypriso->resizing_id);
    auto client = c_from_id(hypriso->resizing_id);
    if (client) {
        client->resizing = false;
    }
}

static Timer *workspace_screenshot_timer = nullptr;

void stop_workspace_screenshotting() {
    if (workspace_screenshot_timer) {
        workspace_screenshot_timer->keep_running = false;
        workspace_screenshot_timer = nullptr;
    }
}

void start_workspace_screenshotting() {
    if (workspace_screenshot_timer) {
        workspace_screenshot_timer->keep_running = true;
        return;
    }
    float fps = 1000.0 / 24.0f;
    workspace_screenshot_timer = later(nullptr, fps, [](Timer *m) {
        for (auto r : roots) {
            auto rdata = (RootData *) r->user_data;
            auto spaces = hypriso->get_workspaces(rdata->id);
            auto before = screenshotting;
            for (auto s : spaces) {
                //hypriso->screenshot_space(rdata->id, s);
            }
            screenshotting = true;
            for (auto client : hypriso->windows) {
                hypriso->screenshot_deco(client->id);
            }
            screenshotting = true;
            hypriso->screenshot_wallpaper(rdata->id);
            screenshotting = before;
        }
    });
    workspace_screenshot_timer->keep_running = true;
}


void paper() {
    later(0, [](Timer *) {
        hypriso->screenshot_wallpaper(((RootData *) roots[0]->user_data)->id);
    });
}

void drag_start(int id) {
    paper();
    //start_workspace_screenshotting();
    hypriso->dragging_id = id;
    hypriso->dragging = true;
    showing_switcher = true;
    //screenshot_deco(id);
    //hypriso->set_hidden(id, true);
    clear_snap_groups(id);
    auto client = c_from_id(hypriso->dragging_id);
    auto b = bounds(client);
    auto MOUSECOORDS = mouse();
    auto mid = get_monitor(client->id);
    auto monitor = m_from_id(mid);
    auto mb = bounds(monitor);
    if (client->snapped) {
        client->snapped = false;
        client->snap_type = SnapPosition::NONE;
        hypriso->should_round(client->id, true);
        auto s = scale(mid);
        //client->drag_initial_mouse_percentage = perc;
        float perc = (MOUSECOORDS.x - b.x) / b.w;
        bool window_left_side = b.x < mb.x + b.w * .5;
        bool click_left_side = perc <= .5;
        float size_from_left = b.w * perc;
        float size_from_right = b.w - size_from_left;
        bool window_smaller_after = b.w > client->pre_snap_bounds.w;
        float x = MOUSECOORDS.x - (perc * (client->pre_snap_bounds.w)); // perc based relocation
        // keep window fully on screen
        if (!window_smaller_after) {
            if (click_left_side) {
                if (window_left_side) {
                    x = MOUSECOORDS.x - size_from_left;
                } else {
                    x = MOUSECOORDS.x - client->pre_snap_bounds.w + size_from_right;
                }
            } else {
                if (window_left_side) {
                    x = b.x;
                } else {
                    x = MOUSECOORDS.x - client->pre_snap_bounds.w + size_from_right;
                }
            }
        } else {
            // if offset larger than resulting window use percentage
        }

        hypriso->move_resize(client->id, 
            x, 
            b.y, 
            client->pre_snap_bounds.w, 
            client->pre_snap_bounds.h);
        b = bounds(client);
    } else {
        client->pre_snap_bounds = b;
        update_restore_info_for(id);
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
        return {x, y, w + 2, h};
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

struct SnapHelperData : UserData {
    SnapPosition pos;
    int id;
    long created = 0;
};

SnapHelperData *get_shd_data() {
    for (auto r : roots) {
        for (auto c : r->children) {
            if (c->custom_type == (int) TYPE::SNAP_HELPER) {
                return (SnapHelperData *) c->user_data;
            }
        }
    }

    return nullptr;
}

void create_snap_helper(ThinClient *c, SnapPosition window_snap_target) {
    int count = 0;
    for (auto w: hypriso->windows) {
        if (hypriso->has_decorations(w->id))
            count++;
    }
    if (count <= 1) // Don't create snap helper if there is only one window open
        return;
    
    if (alt_tab_menu->timer == nullptr)
        alt_tab_menu->timer = start_producing_thumbnails();
   // Create snap helper
    auto mon = get_monitor(c->id);
    for (auto r: roots) {
        auto rdata = (RootData * ) r->user_data;

        auto snap_helper = r->child(::absolute, FILL_SPACE, FILL_SPACE);
        snap_helper->receive_events_even_if_obstructed = true;
        snap_helper->name = "snap_helper";
        snap_helper->pre_layout = [](Container *root, Container *c, const Bounds &b) {
            auto shd = (SnapHelperData *) c->user_data;
            auto rdata = ((RootData *) root->user_data);
            auto mon = rdata->id;
            auto s = scale(rdata->id);
            c->real_bounds = snap_position_to_bounds(mon, shd->pos);
            c->real_bounds.scale(s);
            c->real_bounds.shrink(interspace * s);

            // Remove children who no longer exist
            for (int i = c->children.size() - 1; i >= 0; i--) {
                auto ch = c->children[i];
                auto chdata = (TabData *) ch->user_data;
                bool found = false;
                for (auto t: alt_tab_menu->root->children) {
                    auto tdata = (TabData *) t->user_data;
                    if (tdata->wid == chdata->wid) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    delete ch;
                    c->children.erase(c->children.begin() + i);
                }
            }

            // Add children who don't exist yet
           for (auto t: alt_tab_menu->root->children) {
                auto tdata = (TabData *) t->user_data;
                bool found = false;
                for (int i = c->children.size() - 1; i >= 0; i--) {
                    auto ch = c->children[i];
                    auto chdata = (TabData *) ch->user_data;
                    if (chdata->wid == tdata->wid) {
                        found = true;
                    }
                }
                if (!found) {
                    auto td = new TabData;
                    td->wid = tdata->wid;
                    
                    auto thumbnail_parent = c->child(::vbox, FILL_SPACE, FILL_SPACE);
                    thumbnail_parent->user_data = td;
                    thumbnail_parent->after_paint = paint {
                        auto rdata = (RootData *) root->user_data;
                        auto shd = get_shd_data();
                        if (!shd)
                            return;
                        // lerp between ch->real and b, based on
                        long current = get_current_time_in_ms();
                        float thumb_to_position_time = hypriso->get_varfloat("plugin:mylardesktop:thumb_to_position_time");
                        float scalar = ((float)(current - shd->created)) / thumb_to_position_time;
                        if (scalar > 1)
                            scalar = 1.0;

                        auto s = scale(((RootData *) root->user_data)->id);

                        float snap_fadein = hypriso->get_varfloat("plugin:mylardesktop:snap_helper_fade_in");                        
                        for (auto tchild : c->children) {
                            if (tchild->custom_type == (int) TYPE::SNAP_THUMB) {
                                float snfade = ((float )(get_current_time_in_ms() - shd->created)) / snap_fadein;
                                if (snfade > 1.0)
                                    snfade = 1.0;
                                snfade = pull(curve_to_position, snfade);

                                auto tdata = (TabData *) tchild->user_data;
                                auto color = color_titlebar_focused();
                                if (!hypriso->has_focus(tdata->wid))
                                    color = color_titlebar_unfocused();
                                color.a = snfade;
                                //rect(tchild->real_bounds, color, 3, thumb_rounding * s, 2.0, true, scalar);
                            }
                        }
 
                        //for (int i = 0; i < 2; i++) {
                            for (auto tchild : c->children) {
                                if (tchild->custom_type == (int) TYPE::SNAP_THUMB) {
                                    auto tt = (TabData *) tchild->user_data;

                                    auto b = tchild->real_bounds;
                                    for (auto r : roots) {
                                        auto rdata = (RootData *) r->user_data;
                                        auto s = scale(rdata->id);
                                        for (auto cc : r->children) {
                                            if (cc->custom_type == (int) TYPE::CLIENT) {
                                                auto chdata = (ClientData *) cc->user_data;
                                                if (chdata->id == tt->wid) {
                                                    auto cb = cc->real_bounds;
                                                    cb.y += titlebar_h * s;
                                                    cb.h -= titlebar_h * s;
                                                    tchild->real_bounds = cb;
                                                }
                                            }
                                        }
                                    }
                                    auto active_monitor = rdata->id;
                                    auto active_space = hypriso->get_active_workspace(active_monitor);
                                    auto actual_monitor = get_monitor(tt->wid);
                                    auto actual_space = hypriso->get_workspace(tt->wid);
                                    bool on_same_monitor_and_workspace = active_monitor == actual_monitor && active_space == actual_space;
                                    scalar = pull(curve_to_position, scalar);
                                    float alpha = 1.0;
                                    if (!on_same_monitor_and_workspace) {
                                        alpha = scalar;
                                        scalar = 1.0;
                                    } else {
                                        
                                    }


                                    tchild->real_bounds.x = tchild->real_bounds.x + ((b.x - tchild->real_bounds.x) * scalar);
                                    tchild->real_bounds.y = tchild->real_bounds.y + ((b.y - tchild->real_bounds.y) * scalar);
                                    tchild->real_bounds.w = tchild->real_bounds.w + ((b.w - tchild->real_bounds.w) * scalar);
                                    tchild->real_bounds.h = tchild->real_bounds.h + ((b.h - tchild->real_bounds.h) * scalar);

                                    paint_thumbnail_raw(root, tchild, alpha);
                                    if (alpha != 1.0) {
                                        //rect(c->real_bounds, 
                                            //{0, 0, 0, ((float) (1.0 - alpha) * .75f)},
                                            //0, thumb_rounding * s, 2.0f, false);
                                    }
                                    tchild->real_bounds = b;
                                }
                            }
                        //}
                    };

                    auto titlebar = thumbnail_parent->child(::hbox, FILL_SPACE, titlebar_h * s);
                    titlebar->alignment = ALIGN_RIGHT;
                    titlebar->skip_delete = true;
                    titlebar->when_paint = paint {
                        long *d = get_data<long>(c->uuid, "start");
                        if (!d) {
                            set_data<long>(c->uuid, "start", get_current_time_in_ms() - thumb_to_position_forward);
                            d = get_data<long>(c->uuid, "start");
                        }
                        long current = get_current_time_in_ms();
                        float thumb_to_position_time = hypriso->get_varfloat("plugin:mylardesktop:thumb_to_position_time");
                        float scalar = ((float)(current - *d)) / (thumb_to_position_time + 100);
                        if (scalar > 1)
                            scalar = 1.0;
                        paint_titlebar_raw(root, c, scalar, false);
                    }; 
                    titlebar->user_data = td;

                    /*
                    auto close = titlebar->child(::hbox, titlebar_h * s * title_button_wratio, FILL_SPACE);
                    close->skip_delete = true;
                    close->when_paint = paint {
                        float snap_fadein = hypriso->get_varfloat("plugin:mylardesktop:snap_helper_fade_in");
                        auto shd = (SnapHelperData * ) c->parent->parent->user_data;
                        float scalar = ((float )(get_current_time_in_ms() - shd->created)) / snap_fadein;
                        if (scalar > 1.0)
                            scalar = 1.0;
                        scalar = pull(curve_to_position, scalar);
                        paint_titlebar_close_raw(root, c, scalar); 
                    };
                    close->user_data = td;
                    close->when_clicked = paint {
                        auto td = (TabData *) c->user_data;
                        if (td->wid != -1) {
                            later(1, [td](Timer *t) { // crashes because results in this very container being deleted in while this function still running
                                close_window(td->wid);
                            });
                        }
                    };
                    */
                    
                    auto thumbnail_area = thumbnail_parent->child(::vbox, FILL_SPACE, FILL_SPACE);
                    thumbnail_area->custom_type = (int) TYPE::SNAP_THUMB;
                    thumbnail_area->when_paint = paint {
                        auto rdata = (RootData *) root->user_data;
                        auto shd = (SnapHelperData *) c->user_data;
                        auto tt = (TabData *) c->user_data;
                        auto b = c->real_bounds;
                        auto s = scale(rdata->id);
                        //rect(b, color_snap_helper_thumb_bg, 3, thumb_rounding * s);
                    };
                    thumbnail_area->when_clicked = paint {
                        auto tt = (TabData *) c->user_data;
                        auto shd = (SnapHelperData * ) c->parent->parent->user_data;
                        auto mon = ((RootData *) root->user_data)->id;
                        SnapPosition snop = shd->pos;
                        auto pos = snap_position_to_bounds(mon, snop);
                        // we instant move it so that it looks like it's comint out of the thumbnail
                        auto client = c_from_id(tt->wid);
                        auto pre_snap = bounds(client);
                        bool save = !client->snapped;
                        Bounds bb = Bounds(c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h);
                        auto s = scale(((RootData *) root->user_data)->id);
                        bb.scale(1.0 / s);
                        hypriso->move_resize(tt->wid, bb.x, bb.y, bb.w, bb.h);
                        perform_snap(c_from_id(tt->wid), pos, snop, true, false);
                        if (save)
                            client->pre_snap_bounds = pre_snap;
 
                        hypriso->bring_to_front(tt->wid);
                        later(1, [](Timer *t) {
                            remove_snap_helpers();
                            if (alt_tab_menu->timer) {
                                alt_tab_menu->timer->keep_running = false;
                                alt_tab_menu->timer = nullptr;
                            }
                        });
                        root->consumed_event = true;
                    };
                    titlebar->when_clicked = thumbnail_area->when_clicked;
                    thumbnail_area->user_data = td;
                    thumbnail_area->skip_delete = true;
                }
            }
            float max_w = max_thumb.w * s;
            float max_h = max_thumb.h * s;

            // Manually layout children bounds
            int pen_x = c->real_bounds.x + interspace * s;
            int pen_y = c->real_bounds.y + interspace * s;
            float highest_h = 0;
            for (auto ch : c->children) {
                auto tdata = (TabData *) ch->user_data;
                ch->exists = true; 
                if (tdata->wid == shd->id) {
                    // Don't include the creating window as an option
                    ch->exists = false; // So that it isn't painted
                    continue;
                } 
                auto cdata = c_from_id(tdata->wid);
                auto cb = bounds(cdata);
                // todo needs to set max size and only scale douun to ratio
                float ratio_w = ((cb.w * s) / root->real_bounds.w);
                float ratio_h = ((cb.h * s) / root->real_bounds.h);
                if (ratio_h < ratio_w) {
                    float add = 1.0 - ratio_w;
                    ratio_w *= 1.0 + add;
                    ratio_h *= 1.0 + add;
                } else {
                    float add = 1.0 - ratio_h;
                    ratio_w *= 1.0 + add;
                    ratio_h *= 1.0 + add;
                }
                float t_w = max_w * ratio_w;
                float t_h = max_h * ratio_h;
                
                float final_h = t_h + titlebar_h * s;
                if (pen_x + (t_w + interspace * s) > c->real_bounds.x + c->real_bounds.w) {
                    pen_x = c->real_bounds.x + interspace * s;
                    pen_y += (highest_h) + interspace * s;
                    highest_h = 0;
                }
                if (final_h > highest_h)
                    highest_h = final_h;
                ch->real_bounds = Bounds(pen_x, pen_y, t_w, final_h);
                ch->real_bounds.y += c->scroll_v_visual;
                ::layout(root, ch, ch->real_bounds);
                pen_x += t_w + interspace * s;
            }

            if (c->children.size() <= 1) { // Remove the snap helper if there are no windows to show
                later(1, [](Timer *t) {
                    remove_snap_helpers();
                    if (alt_tab_menu->timer) {
                        alt_tab_menu->timer->keep_running = false;
                        alt_tab_menu->timer = nullptr;
                    }
                });
            }
        };
        snap_helper->when_paint = paint {
            auto color = color_snap_helper;
            auto rdata = (RootData *) root->user_data;
            auto shd = (SnapHelperData *) c->user_data;
            c->automatically_paint_children = false;
            if (rdata->active_id != shd->id || 
                rdata->stage != (int) STAGE::RENDER_PRE_WINDOW) {
                return;
            }
            c->automatically_paint_children = true;
            
            auto s = scale(rdata->id);
            auto b = c->real_bounds;
            b.grow(interspace* s);
            //shadow(b, {0, 0, 0, 1}, thumb_rounding * s, 2.0, interspace* s);
            float snap_fadein = hypriso->get_varfloat("plugin:mylardesktop:snap_helper_fade_in");
            float scalar = ((float )(get_current_time_in_ms() - shd->created)) / snap_fadein;
            if (scalar > 1.0)
                scalar = 1.0;
            scalar = pull(curve_to_position, scalar);
            color.a *= scalar;
            rect(c->real_bounds, color, 0, thumb_rounding * s, 2.0, true, scalar);
            auto color2 = color_snap_helper_border;
            color2.a *= scalar;
            border(c->real_bounds, color2, 1 * s, 0, thumb_rounding * s);
        };
        snap_helper->after_paint = paint {
            c->automatically_paint_children = false;
        };
        snap_helper->when_mouse_motion = paint { root->consumed_event = true; };
        snap_helper->when_mouse_down = paint { root->consumed_event = true; };
        snap_helper->when_mouse_up = paint { root->consumed_event = true; };
        snap_helper->when_fine_scrolled = [](Container* root, Container* c, int scroll_x, int scroll_y, bool came_from_touchpad) {
            root->consumed_event = true;
            c->scroll_v_visual += scroll_y;
            c->scroll_v_real += scroll_y;
            //notify(std::to_string(c->scroll_v_real));
        };
        snap_helper->custom_type = (int) TYPE::SNAP_HELPER;
        SnapPosition op = opposite_snap_position(window_snap_target);
        auto shd = new SnapHelperData;
        shd->pos = op;
        shd->id = c->id;
        shd->created = get_current_time_in_ms();
        snap_helper->user_data = shd;
    } 

    // Save hidden state
    for (auto r : roots) {
        for (auto ch : r->children) {
            if (ch->custom_type == (int) TYPE::CLIENT) {
                auto cdata = (ClientData *) ch->user_data;
                cdata->was_hidden = hypriso->is_hidden(cdata->id);
                // skip self todo
                if (cdata->id != c->id) {
                    hypriso->set_hidden(cdata->id, true);
                }
            }
        }
    } 
    
}

bool groupable_types(SnapPosition a, SnapPosition b) {
    if (a == b)
        return false;
    if (a == SnapPosition::MAX || b == SnapPosition::MAX)
        return false;
    bool a_on_left = a == SnapPosition::LEFT || a == SnapPosition::TOP_LEFT || a == SnapPosition::BOTTOM_LEFT;
    bool b_on_left = b == SnapPosition::LEFT || b == SnapPosition::TOP_LEFT || b == SnapPosition::BOTTOM_LEFT;
    if (a_on_left != b_on_left) {
        return true;
    } else {
        bool a_on_top = a == SnapPosition::TOP_LEFT || a == SnapPosition::TOP_RIGHT;
        bool b_on_top = b == SnapPosition::TOP_LEFT || b == SnapPosition::TOP_RIGHT;
        if (a == SnapPosition::LEFT || b == SnapPosition::RIGHT)
            return false;
        if (a_on_top != b_on_top) {
            return true;
        }
    }

    return false;
}

bool groupable(SnapPosition position, const std::vector<int> ids) {
    std::vector<SnapPosition> positions;
    for (auto id : ids)
        if (auto client = c_from_id(id))
            positions.push_back(client->snap_type); 

    // have to check in with group represented by b and see if all are mergable friendships
    for (auto p : positions)
        if (!groupable_types(position, p)) 
            return false;

    return true; 
}

void add_to_snap_group(int id, int other, const std::vector<int> &grouped) {
    // go through all current groups of other, and add id to those as well
    for (auto r : roots) {
        for (auto c : r->children) {
            if (c->custom_type == (int) TYPE::CLIENT) {
                auto cdata = (ClientData *) c->user_data;
                auto client = c_from_id(cdata->id);
                bool part_of_group = false;
                for (auto g : grouped)
                    if (g == cdata->id)        
                        part_of_group = true;
                if (cdata->id == other || part_of_group) {
                    cdata->grouped_with.push_back(id);
                }
                if (cdata->id == id) {
                    cdata->grouped_with.push_back(other);
                    for (auto g : grouped) {
                        cdata->grouped_with.push_back(g);
                    }
                }
            }
        }
    } 
}


void perform_snap(ThinClient *c, Bounds position, SnapPosition snap_position, bool create_helper, bool instant) {
    bool already_snapped = c->snapped;
    c->snapped = true;
    c->snap_type = snap_position;
    hypriso->should_round(c->id, false);
    if (!already_snapped)
        c->pre_snap_bounds = bounds(c);
    position.y += titlebar_h;
    position.h -= titlebar_h;
    hypriso->move_resize(c->id, position.x, position.y, position.w, position.h, instant); 

    // if not snapped anymore, remove from all snap groups and clear self groups
    if (!c->snapped)
        clear_snap_groups(c->id);
    // attempt to group snapped window with other snapped windows
    if (c->snapped) {
        // find first top to bottom snapped client that is not self
        // if not mergeble, don't do anything special
        // if can be merged, then merge by adding to itself and other to each other groups
        for (auto r: roots) {
            for (auto ch : r->children) {
                if (ch->custom_type == (int) TYPE::CLIENT) {
                    auto other_cdata = (ClientData *) ch->user_data;
                    if (other_cdata->id == c->id)
                        continue; // skip self
                    auto other_client = c_from_id(other_cdata->id);
                    if (!other_client->snapped)
                        continue; // skip non snapped
                    std::vector<int> ids;
                    ids.push_back(other_client->id);
                    for (auto grouped_id : other_cdata->grouped_with)
                        ids.push_back(grouped_id);
                    bool mergable = groupable(c->snap_type, ids);
                    if (mergable) {
                        add_to_snap_group(c->id, other_cdata->id, other_cdata->grouped_with);
                        create_helper = false;
                    } else {
                        // if first merge attempt fails, we don't seek deeper layers
                        goto out;
                    }
                }
            }
            out: 
            break; // todo logic bug needs to be on actual root not first one
        }
    }

    if (create_helper) {
        if (!(snap_position == SnapPosition::MAX || snap_position == SnapPosition::NONE)) {
            create_snap_helper(c, snap_position); 
        }
    }
    for (auto root : roots) {
        auto rdata = (RootData *) root->user_data;
        hypriso->damage_entire(rdata->id);
    }
}

void open_overview() {
    // TODO this is a fake overview right now piggy-backing off alt tab menu
    alt_tab_menu->change_showing(true);
}

void drag_stop() {    
    //stop_workspace_screenshotting();
    showing_switcher = false;
    int window = hypriso->dragging_id;
    drag_update();
    hypriso->dragging_id = -1;
    hypriso->dragging = false;
    hypriso->set_hidden(window, false);
    hypriso->drag_stop_time = get_current_time_in_ms();
    unsetCursorImage();

    int mon = hypriso->monitor_from_cursor();
    auto m = mouse();
    auto snap_position = mouse_to_snap_position(mon, m.x, m.y);
    Bounds position = snap_position_to_bounds(mon, snap_position);

    auto c = c_from_id(window);
    
    auto init_drag_pos = hypriso->drag_initial_window_pos;
    if (c->snapped)
        init_drag_pos = c->pre_snap_bounds;
    
    if (snap_position == SnapPosition::NONE) {
        auto newx = hypriso->drag_initial_window_pos.x + (m.x - hypriso->drag_initial_mouse_pos.x);
        auto newy = hypriso->drag_initial_window_pos.y + (m.y - hypriso->drag_initial_mouse_pos.y);
        position.x = newx;
        position.y = newy;
        hypriso->move(window, position.x, position.y);
    } else {
        perform_snap(c, position, snap_position, true, false);

        auto client = c;
        {
            float left_perc = .5;
            float middle_perc = .5;
            float right_perc = .5;
            Bounds reserved = bounds_reserved(m_from_id(mon));
            auto gs = thin_groups(c->id);
            for (auto g : gs) {
                auto b = bounds(g);
                if (g->snap_type == SnapPosition::TOP_LEFT) {
                    left_perc = (b.h + titlebar_h) / reserved.h;
                    middle_perc = (b.w) / reserved.w;
                } else if (g->snap_type == SnapPosition::TOP_RIGHT) {
                    right_perc = (b.h + titlebar_h) / reserved.h;
                    middle_perc = 1.0 - ((b.w) / reserved.w);
                } else if (g->snap_type == SnapPosition::BOTTOM_LEFT) {
                    left_perc = (b.y - titlebar_h) / reserved.h;
                    middle_perc = (b.w) / reserved.w;
                } else if (g->snap_type == SnapPosition::BOTTOM_RIGHT) {
                    right_perc = (b.y - titlebar_h) / reserved.h;
                    middle_perc = 1.0 - ((b.w) / reserved.w);
                } else if (g->snap_type == SnapPosition::LEFT) {
                    middle_perc = (b.w) / reserved.w;
                } else if (g->snap_type == SnapPosition::RIGHT) {
                    middle_perc = 1.0 - ((b.w) / reserved.w);
                }
            }
            if (left_perc == .5 && right_perc != .5) {
                left_perc = right_perc; 
            }
            if (right_perc == .5 && left_perc != .5) {
                right_perc = left_perc; 
            }
            auto r = reserved;
            Bounds lt = Bounds(r.x, r.y, r.w * middle_perc, r.h * left_perc);
            Bounds lb = Bounds(r.x, r.y + r.h * left_perc, r.w * middle_perc, r.h * (1 - left_perc));
            Bounds rt = Bounds(r.x + r.w * middle_perc, r.y, r.w * (1 - middle_perc), r.h * right_perc);
            Bounds rb = Bounds(r.x + r.w * middle_perc, r.y + r.h * right_perc, r.w * (1 - middle_perc), r.h * (1 - right_perc));
            
            if (c->snap_type == SnapPosition::LEFT) {
                hypriso->move_resize(c->id, reserved.x, reserved.y + titlebar_h, reserved.w * middle_perc, reserved.h - titlebar_h, false);
            } else if (c->snap_type == SnapPosition::RIGHT) {
                hypriso->move_resize(c->id, reserved.x + reserved.w * middle_perc, reserved.y + titlebar_h, reserved.w * (1 - middle_perc), reserved.h - titlebar_h, false);
            } else if (c->snap_type == SnapPosition::TOP_LEFT) {
                hypriso->move_resize(c->id, lt.x, lt.y + titlebar_h, lt.w, lt.h - titlebar_h, false);
            } else if (c->snap_type == SnapPosition::BOTTOM_LEFT) {
                hypriso->move_resize(c->id, lb.x, lb.y + titlebar_h, lb.w, lb.h - titlebar_h, false);
            } else if (c->snap_type == SnapPosition::TOP_RIGHT) {
                hypriso->move_resize(c->id, rt.x, rt.y + titlebar_h, rt.w, rt.h - titlebar_h, false);
            } else if (c->snap_type == SnapPosition::BOTTOM_RIGHT) {
                hypriso->move_resize(c->id, rb.x, rb.y + titlebar_h, rb.w, rb.h - titlebar_h, false);
            }
        } 
    }

    if (!c->snapped) {
        for (auto r : roots) {
            for (auto c : r->children) {
                if (c->custom_type == (int) TYPE::WORKSPACE_SWITCHER) {
                    auto space_data = get_or_create<SpaceSwitcher>(c->uuid, "space_data");
                    space_data->expanded = false;
                }
            }
        }
        for (auto r : roots) {
            auto rdata = (RootData *) r->user_data;

            // todo not really correct should be based on window i think
            if (mon == rdata->id) {
                auto s = scale(rdata->id);
                std::vector<Container*> pierced = pierced_containers(r, m.x * s, m.y * s);
                for (auto p : pierced) {
                    if (p->custom_type == (int) TYPE::WORKSPACE_THUMB) {
                        auto tdata = get_or_create<TabData>(p->uuid, "tdata");
                        hypriso->move_to_workspace(c->id, tdata->wid);
                        hypriso->move_resize(c->id, init_drag_pos.x, init_drag_pos.y, init_drag_pos.w, init_drag_pos.h, false);
                        break;
                    }
                }
            }
        }
    }
    if (!c->snapped) {
        clear_snap_groups(window);
    }
}

void toggle_maximize(int id) {
    auto c = c_from_id(id);
    int mon = hypriso->monitor_from_cursor();
    if (c->snapped) {
        auto m = m_from_id(mon);
        auto b = bounds(m);
        hypriso->move_resize(id, 
            b.x + b.w * .5 - c->pre_snap_bounds.w * .5, 
            b.y + b.h * .5 - c->pre_snap_bounds.h * .5, 
            c->pre_snap_bounds.w, c->pre_snap_bounds.h, false);
    } else {
        Bounds position = snap_position_to_bounds(mon, SnapPosition::MAX);
        c->pre_snap_bounds = bounds(c);
        update_restore_info_for(c->id);
        hypriso->move_resize(id, position.x, position.y + titlebar_h, position.w, position.h - titlebar_h, false);
    }
    c->snapped = !c->snapped;
    hypriso->should_round(c->id, !c->snapped);
    for (auto root : roots) {
        auto rdata = (RootData *) root->user_data;
        hypriso->damage_entire(rdata->id);
    } 
}

// returning true means consume the event
bool on_mouse_move(int id, float x, float y) {
    if (hypriso->monitors.empty())
        return false;
 
    if (hypriso->dragging)
        drag_update();
    if (hypriso->resizing)
        resize_update();

    Event event(x, y);
    layout_every_single_root();
    for (auto root : roots)
        move_event(root, event);

    if (hypriso->dragging)
        return true;
    if (hypriso->resizing)
        return true;

    auto current = get_current_time_in_ms();
    auto time_since = (current - zoom_nicely_ended_time);
    if (zoom_factor == 1.0 && time_since > 1000) {
        static bool has_done_window_switch = false;
        bool no_fullscreens = !any_fullscreen();
        int m = hypriso->monitor_from_cursor();
        auto s = scale(m);
        auto current_coords = mouse();
        current_coords.scale(s);
        bool enough_time = true;
        if ((get_current_time_in_ms() - hypriso->drag_stop_time) < 500) {
            enough_time = false;
        }
        
        for (auto r : roots) {
            auto rdata = (RootData *) r->user_data;
            if (rdata->id != m)
                continue;
            
            if (current_coords.y <= r->real_bounds.y + 1) {
                if (current_coords.x >= r->real_bounds.x + r->real_bounds.w * .4) {
                    if (current_coords.x <= r->real_bounds.x + r->real_bounds.w * .6 && no_fullscreens) {
                        showing_switcher = true;
                        request_refresh();
                    }
                }
            }

            if (current_coords.x <= r->real_bounds.x + 1) {
                if (!has_done_window_switch && no_fullscreens && enough_time) {
                    has_done_window_switch = true;
                    
                    if (current_coords.y < r->real_bounds.h * .2) {
                        open_overview();
                    } else if (current_coords.y < r->real_bounds.h * .4) {
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
                            system("nohup bash -c '/home/jmanc3/Scripts/./spotifytoggle.sh &'");
                        } else {
                            later(nullptr, 100, [](Timer* data) {
                                hypriso->send_key(KEY_SPACE);
                                later(nullptr, 100, [](Timer* data) {
                                    alt_tab_menu->change_showing(true);
                                    tab_next_window();
                                    alt_tab_menu->change_showing(false);
                                });
                            });
                        }
                    } else {
                        alt_tab_menu->change_showing(true);
                        tab_next_window();
                        alt_tab_menu->change_showing(false);
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
    alt_tab_menu->previous_index = alt_tab_menu->index;
    alt_tab_menu->index++;
    if (alt_tab_menu->index >= alt_tab_menu->root->children.size()) {
        alt_tab_menu->index = 0;
    }
    update_visible_window();
    alt_tab_menu->time_when_change = get_current_time_in_ms();
    /*
    auto w = alt_tab_menu.stored[alt_tab_menu.index]; 
    switchToWindow(w.lock(), true);
    g_pCompositor->changeWindowZOrder(w.lock(), true);
    */
}

void tab_previous_window() {
    alt_tab_menu->index--;
    if (alt_tab_menu->index < 0) {
        alt_tab_menu->index = alt_tab_menu->root->children.size() - 1;
    }
    update_visible_window();
    alt_tab_menu->time_when_change = get_current_time_in_ms();

    /*
    auto w = alt_tab_menu.stored[alt_tab_menu.index]; 
    switchToWindow(w.lock(), true);
    g_pCompositor->changeWindowZOrder(w.lock(), true);
    */
}

void remove_snap_helpers() {
    bool update_hidden = false;
    for (auto r: roots) {
        for (int i = r->children.size() - 1; i >= 0; i--) {
            if (r->children[i]->custom_type == (int) TYPE::SNAP_HELPER) {
                delete r->children[i];
                r->children.erase(r->children.begin() + i);
                update_hidden = true;
            }
        }
    } 

    if (update_hidden) {
        for (auto r : roots) {
            for (auto ch : r->children) {
                if (ch->custom_type == (int) TYPE::CLIENT) {
                    auto cdata = (ClientData *) ch->user_data;
                    hypriso->set_hidden(cdata->id, false);
                }
            }
        } 
        request_refresh();
    }
}


// returning true means consume the event
bool on_key_press(int id, int key, int state, bool update_mods) {
    if (hypriso->monitors.empty())
        return false;
 
    static bool alt_down   = false;
    static bool shift_down = false;
    
    if (key == KEY_ESC && state == 0) {
        if (hypriso->dragging) {
            drag_stop();
        }
        if (hypriso->resizing) {
            resize_stop();
        }
        META_PRESSED = false;
        zoom_factor = 1.0;
        
        alt_tab_menu->change_showing(false);
        // remove snap helper
        //remove_snap_helpers();
    }
    remove_snap_helpers();
    
    if (key == KEY_LEFTALT || key == KEY_RIGHTALT) {
        alt_down = state;
        if (state == 0) {
            alt_tab_menu->change_showing(false);
        }
    }
    
    if (key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT) {
        shift_down = state;
    }

    if (key == KEY_LEFTMETA || key == KEY_RIGHTMETA) {
        META_PRESSED = state;
    }

    if (key == KEY_TAB) { // on tab release
        if (alt_down) {
            alt_tab_menu->change_showing(true);
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
bool on_scrolled(int id, int source, int axis, int direction, double delta, int discrete, bool from_mouse) {
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

    auto current = get_current_time_in_ms();
    auto time_since = (current - zoom_nicely_ended_time);
    if (META_PRESSED && time_since > 1000) {
        zoom_factor -= delta * .05; 
        if (zoom_factor < 1.0)
            zoom_factor = 1.0;
        if (zoom_factor > 10.0)
            zoom_factor = 10.0;
        if (delta > 0 && zoom_factor < 1.3 && zoom_factor != 1.0) { // Recognize likely attempted to end zoom and do it cleanly for user
           zoom_factor = 1.0; 
           zoom_nicely_ended_time = get_current_time_in_ms();
        }
        hypriso->set_zoom_factor(zoom_factor);
        return true;
    }
    if (time_since < 750) // consume scrolls which are likely referring to the zoom effect and not to the window focused
        return true;
    
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
    if (roots.empty())
        return;
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

                            /*

CBox assignedBoxGlobal(MylarBar *bar) {
    if (!validMapped(bar->window))
        return {};

    const auto PWORKSPACE = bar->window->m_workspace;
    const auto WORKSPACEOFFSET = PWORKSPACE && !bar->window->m_pinned ? PWORKSPACE->m_renderOffset->value() : Vector2D();
    auto w = bar->window.get();

    CBox title_raw = {
        w->m_realPosition->value().x + w->m_floatingOffset.x + WORKSPACEOFFSET.x, 
        w->m_realPosition->value().y + w->m_floatingOffset.y + WORKSPACEOFFSET.y - ((float) our_state->titlebar_size), 
        w->m_realSize->value().x, 
        (float) our_state->titlebar_size
    };

    return title_raw.round();
}
                             * 
                             */ 

        for (auto c : r->children) {
            auto cdata = (ClientData *) c->user_data;
            auto cid = cdata->id;
            if (auto cm = c_from_id(cid)) {
                auto b = bounds(cm);            
                b.scale(s);
                auto fo = hypriso->floating_offset(cid);
                fo.scale(s);
                auto so = hypriso->workspace_offset(cid);
                so.scale(s);
                c->real_bounds = Bounds(
                    b.x + fo.x + so.x, 
                    b.y - titlebar_h * s + fo.y + so.y, 
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
}


// i think this is being called once per monitor
void on_render(int id, int stage) {
    if (screenshotting) // todo maybe remove
        return;
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
        if (stage == (int) STAGE::RENDER_LAST_MOMENT) {
            if (alt_tab_menu->is_showing()) {
                rect(root->real_bounds, {0, 0, 0, .4}, 0, 0, 2.0f, false);
            }
        }

        paint_root(root);
    }
    
    if (stage == (int) STAGE::RENDER_LAST_MOMENT) {
        bool max_request = false;
        for (auto root : roots) {
            auto rdata = (RootData *) root->user_data;
            // TODO: how costly is this exactly?
            bool should_damage = false;
            if (alt_tab_menu->is_showing())
                should_damage = true;
            for (auto c: root->children)
                if (c->custom_type == (int)TYPE::SNAP_HELPER)
                    should_damage = true;

            if (should_damage || force_damage_all) {
                max_request = true;
            }
        }
        if (showing_switcher)
            max_request = true;
        if (max_request) {
            request_refresh();
        }
        force_damage_all = false;
        //request_refresh();
    }
    if (stage == (int) STAGE::RENDER_LAST_MOMENT) {
        bool shp = false;
        for (auto r : roots) {
           for (auto c : r->children) {
               if (c->custom_type == (int) TYPE::SNAP_HELPER) {
                   shp = true;
               }
           }
        }
        if (shp) {
            for (auto r : roots) {
               for (auto c : r->children) {
                   if (c->custom_type == (int) TYPE::CLIENT) {
                       auto rdata = (RootData *) r->user_data;
                       auto cdata = (ClientData *) c->user_data;
                       auto b = c->real_bounds;
                       auto s = scale(rdata->id);
                       b.y += titlebar_h * s;
                       b.h -= titlebar_h * s;
                       //hypriso->draw_thumbnail(cdata->id, b);
                   }
               }
            }
        }
    }
}

// returning true means consume the event
bool on_mouse_press(int id, int button, int state, float x, float y) {
    if (hypriso->monitors.empty())
        return false;
    
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

    // remove snap helper if not hit
    if (state == 1) {
        bool snap_helper_hit = false;
        for (auto r : roots) {
           for (auto c : r->children) {
                if (c->custom_type == (int) TYPE::SNAP_HELPER) {
                    if (bounds_contains(c->real_bounds, event.x, event.y))
                        snap_helper_hit = true;
                }   
           }
        }
        if (!snap_helper_hit)
            remove_snap_helpers();

        if (alt_tab_menu->is_showing()) {
            for (auto r: roots) {
                for (int i = r->children.size() - 1; i >= 0; i--) {
                    auto c = r->children[i];
                    if (c->custom_type == (int) TYPE::ALT_TAB) {
                        auto altroot = (AltRoot *) c->children[0]->user_data;
                        if (!bounds_contains(altroot->b, event.x, event.y)) {
                            alt_tab_menu->change_showing(false, false);
                        }
                    }
                }
            } 
        }
    }

    return consumed;
}

int monitor_overlapping(int id) {
    return get_monitor(id);
}

void save_restore_infos() {
    // Resolve $HOME
    const char* home = std::getenv("HOME");
    if (!home) {
        throw std::runtime_error("HOME environment variable not set");
    }

    // Target path
    std::filesystem::path filepath = std::filesystem::path(home) / ".config/mylar/restore.txt";

    // Ensure parent directories exist
    std::filesystem::create_directories(filepath.parent_path());

    // Write file (overwrite mode)
    std::ofstream out(filepath, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Failed to write file: " + filepath.string());
    }
    for (auto [class_name, info] : restore_infos) {
        // class_name std::string
        // info.box.x info.box.y info.box.w info.box.h
        out << class_name << " " << info.box.x << " " << info.box.y << " " << info.box.w << " " << info.box.h << "\n";
    }
    //out << contents;
    if (!out.good()) {
        throw std::runtime_error("Error occurred while writing: " + filepath.string());
    }
}

void load_restore_infos() {
    restore_infos.clear();

    // Resolve $HOME
    const char* home = std::getenv("HOME");
    if (!home) {
        throw std::runtime_error("HOME environment variable not set");
    }

    // Target path
    std::filesystem::path filepath = std::filesystem::path(home) / ".config/mylar/restore.txt";

    std::ifstream in(filepath);
    if (!in) {
        // No file — silently return
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string class_name;
        Bounds info;

        // Parse strictly: skip if the line is malformed
        if (!(iss >> class_name >> info.x >> info.y >> info.w >> info.h)) {
            continue; // bad line — skip
        }

        WindowRestoreLocation restore;
        restore.box = info;
        restore_infos[class_name] = restore;
    }
}

void update_restore_info_for(int w) {
    WindowRestoreLocation info;
    ThinClient *c = c_from_id(w);
    int mid = monitor_overlapping(w);
    ThinMonitor *m = m_from_id(mid);
    if (m && c) {
        Bounds cb = bounds(c);
        Bounds cm = bounds(m);
        auto s = scale(mid);
        info.box = {
            cb.x / cm.w,
            cb.y / cm.h,
            cb.w / cm.w,
            (cb.h + titlebar_h) / cm.h,
        };
        restore_infos[class_name(c_from_id(w))] = info;
        save_restore_infos(); // I believe it's okay to call this here because it only happens on resize end, and drag end
    }
}

struct PreviousData {
    std::vector<float> old;
};

bool any_change(std::string uuid, std::string id, std::vector<float> newer) {
    bool change = false;
    PreviousData *data = get_data<PreviousData>(uuid, id);
    if (!data) {
       set_data<PreviousData>(uuid, id, PreviousData()); 
       data = get_data<PreviousData>(uuid, id);
    }
    if (data) {
        PreviousData *pd = data;
        if (pd->old.size() != newer.size()) {
            change = true;
        } else {
            for (int i = 0; i < pd->old.size(); i++) {
                if (pd->old[i] != newer[i]) {
                    change = true;
                }
            }
        }
        pd->old.clear();
        for (auto f : newer)
            pd->old.push_back(f);
    }
    //if (change)
        //nz(fz("{}", change));
    return change; 
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
    
    if (hypriso->has_decorations(id)) {
        auto s = scale(monitor);
        
        auto td = new TabData;
        td->wid = id;
        
        auto thumbnail_parent = alt_tab_menu->root->child(::vbox, FILL_SPACE, FILL_SPACE);
        thumbnail_parent->user_data = td;

        auto titlebar = thumbnail_parent->child(::hbox, FILL_SPACE, titlebar_h * s);
        titlebar->alignment = ALIGN_RIGHT;
        titlebar->skip_delete = true;
        titlebar->when_paint = paint_titlebar; 
        titlebar->user_data = td;
        
        auto close = titlebar->child(::hbox, titlebar_h * s * title_button_wratio, FILL_SPACE);
        close->skip_delete = true;
        close->when_paint = paint_titlebar_close; 
        close->user_data = td;
        close->when_clicked = paint {
            auto td = (TabData *) c->user_data;
            if (td->wid != -1) {
                later(1, [td](Timer *t) { // crashes because results in this very container being deleted in while this function still running
                    close_window(td->wid);
                });
            }
        };

        auto thumb_area = thumbnail_parent->child(::hbox, FILL_SPACE, FILL_SPACE);
        thumb_area->skip_delete = true;
        thumb_area->when_paint = paint {
            auto rdata = (RootData *) root->user_data;
            paint_thumbnail(root, c);
        };
        thumb_area->user_data = td;
        thumb_area->when_clicked = paint {
            auto td = (TabData *) c->user_data;
            for (int i = 0; i < alt_tab_menu->root->children.size(); i++) {
                auto o = alt_tab_menu->root->children[i];
                auto data = (TabData *) o->user_data;
                if (data->wid == td->wid) {
                    alt_tab_menu->index = i;
                    alt_tab_menu->change_showing(false);
                    break;
                }
            }
        };
        titlebar->when_clicked = thumb_area->when_clicked;
    }
    
    auto cname = class_name(tc);
    for (auto [class_n, info] : restore_infos) {
        if (cname == class_n) {
            // Skip restore info if class name is same as parent class name (dialogs)
            int parent = hypriso->parent(id);
            if (parent != -1) {
                auto pname = class_name(c_from_id(parent));
                if (pname == cname) {
                    continue;
                }
            }
            
            auto b = real_bounds(tc);
            auto m = m_from_id(monitor);
            auto s = scale(m->id);
            auto b2 = bounds_reserved(m);
            b.w = b2.w * info.box.w;
            b.h = b2.h * info.box.h;
            if (b.w >= b2.w - 60 * s) {
                b.w = b2.w - 60 * s;
            }
            bool fix = false;
            if (b.h >= b2.h - 60 * s) {
                b.h = b2.h - 60 * s;
                fix = true;
            }
            b.x = b2.x + b2.w * .5 - b.w * .5;
            b.y = b2.y + b2.h * .5 - b.h * .5;
            if (fix)
                b.y += (titlebar_h * s) * .5;

            hypriso->move_resize(tc->id, b.x, b.y, b.w, b.h);
        }
    }
    


    // TODO: We should put these windows in a limbo vector until a monitor opens and then move them over
    //assert(monitor != -1 && "window opened and there were no monitors avaialable (Mylardesktop bug!)");

    for (auto r : roots) {
        auto data = (RootData *) r->user_data;
        if (data->id == monitor) {
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

                auto rtype = client->resize_type;
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
                resize_update();
            };
            resize->when_drag_start = [](Container *root, Container *c) {
                //auto cdata = (ClientData *) c->user_data;
                //auto client = c_from_id(cdata->id);
                //client->resizing = true;
                //auto m = mouse();
                //client->initial_x = m.x;
                //client->initial_y = m.y;
                //client->initial_win_box = bounds(client);
                //c->when_drag(root, c);
                auto cdata = (ClientData *) c->user_data;
                auto client = c_from_id(cdata->id);
                //client->resizing = true;
                //auto m = mouse();
                //client->initial_x = m.x;
                //client->initial_y = m.y;
                //client->initial_win_box = bounds(client);
                //c->when_drag(root, c);
                resize_start(cdata->id, (RESIZE_TYPE) client->resize_type);
                resize_update();
            };
            resize->when_drag_end = [](Container *root, Container *c) {
                resize_stop();
                //auto cdata = (ClientData *) c->user_data;
                //auto client = c_from_id(cdata->id);
                //client->resizing = false;
                //c->when_drag(root, c);
                //update_restore_info_for(client->id);
            };
            resize->handles_pierced = [](Container* container, int mouse_x, int mouse_y) {
                auto cdata = (ClientData *) container->user_data;
                auto client = c_from_id(cdata->id);                
                if (client->snapped) {
                    // TODO resize edge should be enabled
                    //return false; 
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
            c->automatically_paint_children = false;
            c->when_paint = paint {
                auto data = (ClientData *) c->user_data;
                auto rdata = (RootData *) root->user_data;
                auto s = scale(rdata->id);
                if (rdata->active_id == data->id && rdata->stage == (int) STAGE::RENDER_POST_WINDOW) {
                    auto b = c->real_bounds;
                    auto size = std::floor(1.3 * s);
                    b.shrink(size);
                    float round = hypriso->get_rounding(data->id);
                    //border(b, {1, 1, 1, .2}, size, 0, round * s);
                }
                if (hypriso->dragging && data->id == hypriso->dragging_id) {
                    if (rdata->stage == (int) STAGE::RENDER_LAST_MOMENT) {
                        //rect(c->real_bounds, {1, 1, 0, 1});
                        auto b = c->real_bounds;
                        auto client = c_from_id(data->id);
                        auto full_b = bounds_full(client);
                        auto xb = bounds(client);
                        auto s = scale(rdata->id);
                        xb.x -= (xb.x - full_b.x);
                        xb.y -= (xb.y - full_b.y);
                        xb.scale(s);
                        full_b.scale(s);
                        //hypriso->draw_deco_thumbnail(data->id, full_b);
                    }
                }
            };
            resize->user_data = c->user_data;
            
            auto thinc = c_from_id(id);
            thinc->uuid = c->uuid;
            auto s = scale(((RootData *) r->user_data)->id);
            auto title = c->child(::hbox, FILL_SPACE, titlebar_h * s);
            title->name = "title";
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
                    auto color = color_titlebar_focused();
                    if (!hypriso->has_focus(data->id))
                        color = color_titlebar_unfocused();
                    color.a = data->alpha;
                    if (client->snapped) {
                        rect(c->real_bounds, color, 0, 0);
                    } else {
                        rect(c->real_bounds, color, 12, rounding * scale(rdata->id));
                    }
                    auto text = title_name(client);
                    if (titledata->cached_text != text) {
                        if (titledata->main_focused.id != -1) {
                            titledata->main_focused.id = -1;
                            free_text_texture(titledata->main_focused.id);
                            free_text_texture(titledata->main_unfocused.id);
                        }

                        titledata->main_focused = gen_text_texture("Segoe UI Variable", text, titlebar_text_h * s, color_titlebar_text_focused());
                        titledata->main_unfocused = gen_text_texture("Segoe UI Variable", text, titlebar_text_h * s, color_titlebar_text_unfocused());
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
                            c->real_bounds.y + (c->real_bounds.h - titledata->icon.h) * .5, data->alpha);
                    }

                    double xoff = c->real_bounds.x + (titledata->icon.id == -1 ? titlebar_icon_pad * s : titlebar_icon_pad * s * 2 + titledata->icon.w);
                    double clip_w = c->real_bounds.w - (xoff - c->real_bounds.x) - (c->real_bounds.h * 3 * title_button_wratio);
                    if (clip_w <= 0.0)
                        clip_w = 1.0;
                    auto tex = titledata->main_focused;
                    if (!hypriso->has_focus(data->id))
                       tex = titledata->main_unfocused;
                    draw_texture(tex, xoff, c->real_bounds.y + (c->real_bounds.h - tex.h) * .5, data->alpha, clip_w);
                }
                c->real_bounds = backup;
            };
            title->alignment = ALIGN_RIGHT;
            title->when_clicked = [](Container *root, Container *c) {
                auto cdata = (ClientData *) c->parent->user_data;
                auto rdata = (RootData *) root->user_data;
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
            min->when_mouse_motion = paint {
                auto rdata = (RootData *) root->user_data;
                auto s = scale(rdata->id);
                bool needs_damage = any_change(c->uuid, "min", {(float) c->state.mouse_hovering, (float) c->state.mouse_pressing});               
                if (needs_damage) {
                    force_damage_all = true;
                    //hypriso->damage_entire(rdata->id);
                    //auto b = c->real_bounds;
                    //b.scale(1.0 / s);
                    //b.grow(2 * s);
                    //hypriso->damage_box(b);
                    request_refresh();
                }
            };
            min->when_paint = [](Container *root, Container *c) {
                auto data = (ClientData *) c->parent->parent->user_data;
                auto rdata = (RootData *) root->user_data;
                auto cdata = (IconData *) c->user_data;
                auto s = scale(rdata->id);
                if (data->id == rdata->active_id) { 
                    auto color = color_titlebar_pressed;
                    if (c->state.mouse_hovering)
                        color = color_titlebar_hovered;
                    color.a = data->alpha;
                    if (c->state.mouse_hovering || c->state.mouse_pressing)
                        rect(c->real_bounds, color);

                    if (!cdata->attempted) {
                        cdata->attempted = true;
                        auto color_titlebar_icon = color_titlebar_text_focused();
                        if (!hypriso->has_focus(data->id)) {
                            color_titlebar_icon = color_titlebar_text_unfocused();
                        }
                        cdata->main = gen_text_texture("Segoe Fluent Icons", "\ue921",
                            titlebar_icon_button_h * s, color_titlebar_icon);
                    }
                    if (cdata->main.id != -1) {
                        draw_texture(cdata->main,
                            c->real_bounds.x + c->real_bounds.w * .5 - cdata->main.w * .5,
                            c->real_bounds.y + c->real_bounds.h * .5 - cdata->main.h * .5, data->alpha);
                    }
                }
            };
            min->pre_layout = [](Container *root, Container *c, const Bounds &b) {
                c->wanted_bounds.w = b.h * title_button_wratio;
            };
            min->when_clicked = paint  {
                auto data = (ClientData *) c->parent->parent->user_data;
                auto rdata = (RootData *) root->user_data;
                auto cdata = (IconData *) c->user_data;
                auto cc = c_from_id(data->id); 
                if (cc->snapped) {
                   toggle_maximize(cc->id); 
                } else {
                    //hypriso->move_to_workspace(data->id, 2);
                    hypriso->set_hidden(data->id, true);
                    alt_tab_menu->change_showing(true);
                    tab_next_window();
                    alt_tab_menu->change_showing(false);
                }
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
                    bool needs_damage = any_change(c->uuid, "max", {(float) c->state.mouse_hovering, (float) c->state.mouse_pressing});               
                    if (needs_damage) {
                        request_refresh();
                        force_damage_all = true;
                        //hypriso->damage_entire(rdata->id);
                        /*auto b = c->real_bounds;
                        b.scale(1.0 / s);
                        b.grow(2 * s);
                        hypriso->damage_box(b);*/
                    }
                    auto color = color_titlebar_pressed;
                    if (c->state.mouse_hovering)
                        color = color_titlebar_hovered;
                    color.a = data->alpha;
                    if (c->state.mouse_hovering || c->state.mouse_pressing)
                        rect(c->real_bounds, color);

                    if (!cdata->attempted) {
                        cdata->attempted = true;
                        auto color_titlebar_icon = color_titlebar_text_focused();
                        if (!hypriso->has_focus(data->id)) {
                            color_titlebar_icon = color_titlebar_text_unfocused();
                        }
                        
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
                            c->real_bounds.y + c->real_bounds.h * .5 - cdata->main.h * .5, data->alpha);
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
            close->name = "close";
            close->when_mouse_down = title->when_mouse_down;
            close->when_paint = [](Container *root, Container *c) {                
                auto data = (ClientData *) c->parent->parent->user_data;
                auto client = c_from_id(data->id);
                auto rdata = (RootData *) root->user_data;
                auto cdata = (IconData *) c->user_data;
                auto s = scale(rdata->id);
                if (data->id == rdata->active_id) {
                    if (c->state.mouse_pressing || c->state.mouse_hovering) {
                        c->real_bounds.w += 1;
                        c->real_bounds.y -= 1;
                        c->real_bounds.h += 1;
                    }
                    bool needs_damage = any_change(c->uuid, "close", {(float) c->state.mouse_hovering, (float) c->state.mouse_pressing});               
                    if (needs_damage) {
                        request_refresh();
                        force_damage_all = true;
                        //hypriso->damage_entire(rdata->id);
                        /*auto b = c->real_bounds;
                        b.scale(1.0 / s);
                        b.grow(2 * s);
                        hypriso->damage_box(b);*/
                    }
                    
                    int mask = 13;
                    float round = 10 * scale(rdata->id);
                    if (client->snapped) {
                       mask = 0; 
                       round = 0.0;
                    }
                    auto color = color_titlebar_pressed_closed;
                    if (c->state.mouse_hovering)
                        color = color_titlebar_hovered_closed;
                    color.a = data->alpha;
                    if (c->state.mouse_hovering || c->state.mouse_pressing)
                        rect(c->real_bounds, color, mask, round);

                    if (!cdata->attempted) {
                        cdata->attempted = true;
                        auto color_titlebar_icon = color_titlebar_text_focused();
                        if (!hypriso->has_focus(data->id)) {
                            color_titlebar_icon = color_titlebar_text_unfocused();
                        }
                        
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
                            c->real_bounds.y + c->real_bounds.h * .5 - cdata->main.h * .5, data->alpha);
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

    later(0, [id](Timer *) {
        hypriso->floatit(id);
    });
}

void clear_snap_groups(int id) {
    for (auto r: roots) {
        for (auto c : r->children) {
           if (c->custom_type == (int) TYPE::CLIENT) {
               auto cdata = (ClientData *) c->user_data;
               if (cdata->id == id) {
                   cdata->grouped_with.clear();
               }
               for (int i = cdata->grouped_with.size() - 1; i >= 0; i--) {
                   if (cdata->grouped_with[i] == id) {
                       cdata->grouped_with.erase(cdata->grouped_with.begin() + i);
                   } 
               }
           }
        }
    }
}

void on_window_closed(int id) {
    auto client = c_from_id(id);

    for (int i = 0; i < alt_tab_menu->root->children.size(); i++) {
        auto t = alt_tab_menu->root->children[i];
        auto tab_data = (TabData *) t->user_data;
        if (tab_data->wid == id) {
            delete t;
            alt_tab_menu->root->children.erase(alt_tab_menu->root->children.begin() + i);
        }
    }
  
    for (auto r : roots) {
        for (int i = 0; i < r->children.size(); i++) {
            auto child = r->children[i];
            if (child->custom_type == (int) TYPE::SNAP_HELPER) {
                // Close snap helper if the window that created it is closed
                auto shd = (SnapHelperData *) child->user_data;
                if (shd->id == id) {
                    later(1, [](Timer *t) {
                        remove_snap_helpers();
                        if (alt_tab_menu->timer) {
                            alt_tab_menu->timer->keep_running = false;
                            alt_tab_menu->timer = nullptr;
                        }
                    });
                }
            } else if (child->uuid == client->uuid) {
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
    c->receive_events_even_if_obstructed = true;
    c->handles_pierced = [](Container *c, int mouse_x, int mouse_y) {
        auto b = c->real_bounds;
        b.x += b.w - 3;
        b.y += b.h - 3;
        b.w = 3;
        b.h = 3;
        if (bounds_contains(b, mouse_x, mouse_y)) {
            return true;
        }
        return false;
    };
    c->when_clicked = paint {
        auto rdata = (RootData *) root->user_data;
        auto s = scale(rdata->id);
        auto m = mouse();
        m.scale(s);
        auto b = c->real_bounds;
        b.x += b.w - 2;
        b.y += b.h - 2;
        b.w = 2;
        b.h = 2;
        if (bounds_contains(b, m.x, m.y)) {
            static bool showing = true;
            if (showing) {
                hypriso->hide_desktop();
            } else {
                hypriso->show_desktop();
            }
            showing = !showing;
        }
    };
    c->when_paint = paint {
        auto rdata = (RootData *) root->user_data;
        if (rdata->stage != (int) STAGE::RENDER_LAST_MOMENT)
            return;
        Bounds box = {100, 100, 400, 400};
        auto b = box;
        b.grow(40);
        //shadow(b, {0, 0, 0, 1}, 0, 2.0, 40);
        //rect(box, {1, 1, 0, 1});
    };
    roots.push_back(c);

    auto workspace = c->child(::hbox, FILL_SPACE, FILL_SPACE); 
    workspace->custom_type = (int) TYPE::WORKSPACE_SWITCHER;
    workspace->receive_events_even_if_obstructed = true;
    workspace->pre_layout = [](Container* root, Container* c, const Bounds& bound) {
        auto rdata = (RootData *) root->user_data;
        auto s = scale(rdata->id);
        auto space_data = get_or_create<SpaceSwitcher>(c->uuid, "space_data");

        auto spaces = hypriso->get_workspaces(rdata->id);
        // we add a fake space which is one greater to allow creating a new workspace 
        int highest = 1;
        if (!spaces.empty())
            highest = spaces[0];                   
        for (auto s : spaces)
            if (highest < s)
                highest = s;
        if (highest <= 0) // this is a fix if all spaces are named (and therefore negative)?
            highest = 0;
        highest++;
        spaces.push_back(highest);

        // if child not represented in spaces, remove it
        for (int i = c->children.size() - 1; i >= 0; i--) {
            auto ch = c->children[i];
            auto tdata = get_or_create<TabData>(ch->uuid, "tdata");
            bool found = false;
            for (auto space : spaces)
                if (space == tdata->wid)
                    found = true;
            if (!found) {
                // remove it
                delete ch;
                c->children.erase(c->children.begin() + i);
            }
        }
        // if space not represented in children, create it
        for (auto space : spaces) {
            bool found = false;
            for (auto ch : c->children) {
                auto tdata = get_or_create<TabData>(ch->uuid, "tdata");
                if (tdata->wid == space) 
                    found = true;
            }
            if (!found) {
                auto ch = c->child(max_space.w * s, max_space.h * s);
                ch->custom_type = (int) TYPE::WORKSPACE_THUMB;
                auto tdata = get_or_create<TabData>(ch->uuid, "tdata");
                tdata->wid = space;
                ch->when_clicked = paint {
                    auto tdata = get_or_create<TabData>(c->uuid, "tdata");
                    hypriso->move_to_workspace(tdata->wid);
                };
                ch->when_paint = paint {
                    auto tdata = get_or_create<TabData>(c->uuid, "tdata");
                    
                    auto b = c->real_bounds;
                    //b.shrink(10);
                    auto s = scale(((RootData *) root->user_data)->id);
                    auto color = color_workspace_thumb;
                    color.a = .4;
                    rect(b, color, 0, interspace * s);
                    auto rdata = (RootData *) root->user_data;
                    //nz(fz("{} {} {}", rdata->id, tdata->wid, "attempt"));
                    bool actual = false;
                    for (auto s : hypriso->get_workspaces(rdata->id)){
                        if (s == tdata->wid) {
                            actual = true;
                        }
                    }
                     for (auto hclient : hypriso->windows) {
                         auto actual_space = hypriso->get_workspace(hclient->id);
                         if (actual_space == tdata->wid) {
                             //notify("ch=" + std::to_string(tdata->wid) + ", " + std::to_string(actual_space));
                         }
                     }

                    hypriso->draw_wallpaper(rdata->id, c->real_bounds, interspace * s);
                    //if (actual) {
                        // TODO: draw deco windows to screen
                        // for (auto hclient : hypriso->windows) {
                        //     //auto mon = get_monitor(hclient->id);
                        //     //if (mon == rdata->id) {
                        //         // if workspace
                        //         auto actual_space = hypriso->get_workspace(hclient->id);
                        //         if (actual_space == tdata->wid) {
                        //             auto thin = c_from_id(hclient->id);
                        //             auto bou = bounds_full(thin);
                        //             bou.scale(s);
                        //             bou.x = bou.x / root->real_bounds.x;
                        //             bou.y = bou.y / root->real_bounds.y;
                        //             bou.w = bou.w / root->real_bounds.w;
                        //             bou.h = bou.h / root->real_bounds.h;
                        //             bou.x = bou.x * c->real_bounds.x;
                        //             bou.y = bou.y * c->real_bounds.y;
                        //             bou.w = bou.w * c->real_bounds.w;
                        //             bou.h = bou.h * c->real_bounds.h;
                        //             hypriso->draw_deco_thumbnail(hclient->id, bou);
                        //         }
                        //     //}
                        // }
                         
                        //hypriso->draw_workspace(rdata->id, tdata->wid, c->real_bounds, interspace * s);
                    //}
                };
            }
        }
        // sort children based on order 

        c->spacing = interspace * s;
        c->wanted_pad = Bounds(c->spacing, c->spacing, 0, 0);
        c->real_bounds = root->real_bounds;
        c->real_bounds.y += interspace * s; 
        c->real_bounds.w = (max_space.w * s + c->spacing) * c->children.size() + c->spacing;
        c->real_bounds.x += root->real_bounds.w * .5 - c->real_bounds.w * .5;
        c->real_bounds.h = max_space.h * s + c->spacing * 2;

        if (!space_data->expanded) {
           c->real_bounds.y = bound.y - (interspace * s * 2); 
           c->real_bounds.h = interspace * s + (interspace * s * 2);
        }
        for (auto ch : c->children) {
            ch->exists = space_data->expanded;
        }

        // determine if expanded
        auto b = c->real_bounds;
        if (space_data->expanded) {
            b.grow(interspace * s * 1.5);
        }
        auto m = mouse();
        space_data->expanded = bounds_contains(b, m.x * s, m.y * s);
        if (hypriso->dragging) {
            showing_switcher = true;
        } else if (showing_switcher) {
            showing_switcher = space_data->expanded;
        }
        if (space_data->expanded && showing_switcher) {
            start_workspace_screenshotting();
        }
        if (!showing_switcher) {
            stop_workspace_screenshotting(); 
        }

        layout(root, c, c->real_bounds);
    };
    workspace->when_paint = paint {
        auto s = scale(((RootData *) root->user_data)->id);
        auto space_data = get_or_create<SpaceSwitcher>(c->uuid, "space_data");
        int mask = 3;
        if (space_data->expanded)
            mask = 0;
        rect(c->real_bounds, color_workspace_switcher, mask, interspace * s);

        auto rdata = (RootData *) root->user_data;
        if (rdata->stage != (int) STAGE::RENDER_LAST_MOMENT)
            return;

        auto spaces = hypriso->get_workspaces(rdata->id);
        int x = 0;
        for (auto space :spaces) {
            //hypriso->draw_wallpaper(rdata->id, Bounds(x, 0, 200, 200), interspace * s);
            //notify(std::to_string(space));
            //hypriso->draw_workspace(rdata->id, space, Bounds(x, 0, 200, 200), interspace * s);
            x += 200;
        }
        //notify("done");
    };
    workspace->when_clicked = paint {
        root->consumed_event = true;  
    };

    auto tab_menu = c->child(::vbox, FILL_SPACE, FILL_SPACE); 
    tab_menu->custom_type = (int) TYPE::ALT_TAB;
    tab_menu->receive_events_even_if_obstructed = true;
    tab_menu->when_mouse_motion = [](Container *root, Container *c) {
        root->consumed_event = true;
    };
    tab_menu->when_mouse_down = tab_menu->when_mouse_motion;
    tab_menu->when_mouse_up = tab_menu->when_mouse_motion;
    tab_menu->when_fine_scrolled = [](Container* root, Container* self, int scroll_x, int scroll_y, bool came_from_touchpad) {
        root->consumed_event = true;
    };
    tab_menu->when_paint = paint {
        //rect(c->real_bounds, {.3, .2, 1.0, 1.0});
    };
}

void on_monitor_closed(int id) {
    for (auto r : roots) {
        for (int i = 0; i < r->children.size(); i++) {
            if (r->children[i]->custom_type == (int) TYPE::ALT_TAB) {
                r->children.erase(r->children.begin() + i); 
            } 
        }
    }
    
    for (int i = 0; i < roots.size(); i++) {
        auto data = (RootData *) roots[i]->user_data;
        if (data->id == id) {
            delete roots[i];
            roots.erase(roots.begin() + i);
        }
    }
    for (int i = 0; i < hypriso->monitors.size(); i++) {
        auto m = hypriso->monitors[i];
        if (m->id == id) {
            delete m;
            hypriso->monitors.erase(hypriso->monitors.begin() + i); 
        }
    }  
}

void on_drag_start_requested(int id) {
    drag_start(id);
}

void on_activated(int id) {
    for (auto g : thin_groups(id)) {
        hypriso->bring_to_front(g->id, false);
    }
    hypriso->bring_to_front(id, false);
}

void on_resize_start_requested(int id, RESIZE_TYPE type) { 
    resize_start(id, type);
}

void on_config_reload() {
    hypriso->set_zoom_factor(zoom_factor);
    hypriso->add_float_rule();
}

void any_container_closed(Container *c) {
    // delete all related datas
    remove_data(c->uuid); 
}

void on_draw_decos(std::string name, int m, int w, float a) {
    if (name != "MylarBar")
        return;

    for (auto r : roots) {
        for (auto c : r->children) {
            if (c->custom_type == (int) TYPE::CLIENT) {
               auto chdata = (ClientData *) c->user_data;
               if (chdata->id == w) {
                   // todo @speed
                   layout_every_single_root();
                   c->automatically_paint_children = true;
                   //chdata->alpha = 1.0;
                   chdata->alpha = a;
                   paint_outline(r, c);
                   chdata->alpha = 1.0;
                   c->automatically_paint_children = false;
                   return;
               }
            }
        }
    }
};

std::string rounding_shader_data = R"round(
// smoothing constant for the edge: more = blurrier, but smoother
#define M_PI 3.1415926535897932384626433832795
#define SMOOTHING_CONSTANT (M_PI / 5.34665792551)

uniform float radius;
uniform float roundingPower;
uniform vec2 topLeft;
uniform vec2 fullSize;
uniform int cornerDisableMask; // new

vec4 rounding(vec4 color) {
    vec2 pixCoord = vec2(gl_FragCoord);
    vec2 preMirror = pixCoord - (topLeft + fullSize * 0.5); // new

    pixCoord -= topLeft + fullSize * 0.5;
    pixCoord *= vec2(lessThan(pixCoord, vec2(0.0))) * -2.0 + 1.0;
    pixCoord -= fullSize * 0.5 - radius;
    pixCoord += vec2(1.0, 1.0) / fullSize;

    // new: skip rounding if bit for this corner is set
    int cornerBit = (preMirror.y < 0.0 ? (preMirror.x < 0.0 ? 1 : 2)
                                       : (preMirror.x < 0.0 ? 4 : 8));
    if ((cornerDisableMask & cornerBit) != 0)
        return color;

    if (pixCoord.x + pixCoord.y > radius) {
        float dist = pow(pow(pixCoord.x, roundingPower) + pow(pixCoord.y, roundingPower), 1.0/roundingPower);

        if (dist > radius + SMOOTHING_CONSTANT)
            discard;

        float normalized = 1.0 - smoothstep(0.0, 1.0, (dist - radius + SMOOTHING_CONSTANT) / (SMOOTHING_CONSTANT * 2.0));

        color *= normalized;
    }

    return color;
}
)round";

void create_rounding_shader() {
    const char* home = std::getenv("HOME");
    if (!home) {
        throw std::runtime_error("HOME environment variable not set");
    }

    bool reload_shaders = false;

    // Target path
    std::filesystem::path filepath = std::filesystem::path(home) / ".config/hypr/shaders/rounding.glsl";
    if (!std::filesystem::exists(filepath)) {
        reload_shaders = true;
    }

    // Ensure parent directories exist
    std::filesystem::create_directories(filepath.parent_path());

    // Write file (overwrite mode)
    {
        std::ofstream out(filepath, std::ios::trunc);
        out << rounding_shader_data << std::endl;
    }

    if (reload_shaders)
        hypriso->reload();
}

void startup::begin() {
    hypriso->create_config_variables();
    
    hypriso->add_float_rule();
    
    on_any_container_close = any_container_closed;
    
    hypriso->on_mouse_press = on_mouse_press;
    hypriso->on_mouse_move = on_mouse_move;
    hypriso->on_key_press = on_key_press;
    hypriso->on_scrolled = on_scrolled;
    hypriso->on_draw_decos = on_draw_decos;
    hypriso->on_render = on_render;
    hypriso->on_window_open = on_window_open;
    hypriso->on_window_closed = on_window_closed;
    hypriso->on_monitor_open = on_monitor_open;
    hypriso->on_monitor_closed = on_monitor_closed;
    hypriso->on_drag_start_requested = on_drag_start_requested;
    hypriso->on_resize_start_requested = on_resize_start_requested;
    hypriso->on_config_reload = on_config_reload;
    hypriso->on_activated = on_activated;

    load_restore_infos();
	// The two most important callbacks we hook are mouse move and mouse events
	// On every mouse move we update the current state of the ThinClients to be in the right positions
	// so that hen we receive a mouse down, we know if we have to consume it (snap resizing, title bar interactions, alt tab menu, overview dragging, overview drop down, desktop folders, desktop folder selection, so on)
 
    // hooks need to be created last because otherwise we miss initial loading of all windows with on_window_open
	hypriso->create_hooks_and_callbacks(); 
    if (icon_cache_needs_update()) {
        icon_cache_generate();
        //notify("generated");
    }
    
    {
        //notify("icon load");
        icon_cache_load();
    }

    create_rounding_shader();

    std::thread th([]() {
        start_test();        
    });
    th.detach();
}

void startup::end() {
    hypriso->end();
}


