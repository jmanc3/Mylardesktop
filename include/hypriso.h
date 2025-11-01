#ifndef hypriso_h_INCLUDED
#define hypriso_h_INCLUDED

#include <hyprland/src/SharedDefs.hpp>

#include "container.h"
#include <ranges>
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <regex>

static int titlebar_h = 28;

enum struct STAGE : uint8_t {
    RENDER_PRE = eRenderStage::RENDER_PRE,        /* Before binding the gl context */
    RENDER_BEGIN = eRenderStage::RENDER_BEGIN,          /* Just when the rendering begins, nothing has been rendered yet. Damage, current render data in opengl valid. */
    RENDER_POST_WALLPAPER = eRenderStage::RENDER_POST_WALLPAPER, /* After background layer, but before bottom and overlay layers */
    RENDER_PRE_WINDOWS = eRenderStage::RENDER_PRE_WINDOWS,    /* Pre windows, post bottom and overlay layers */
    RENDER_POST_WINDOWS = eRenderStage::RENDER_POST_WINDOWS,   /* Post windows, pre top/overlay layers, etc */
    RENDER_LAST_MOMENT = eRenderStage::RENDER_LAST_MOMENT,    /* Last moment to render with the gl context */
    RENDER_POST = eRenderStage::RENDER_POST,           /* After rendering is finished, gl context not available anymore */
    RENDER_POST_MIRROR = eRenderStage::RENDER_POST_MIRROR,    /* After rendering a mirror */
    RENDER_PRE_WINDOW = eRenderStage::RENDER_PRE_WINDOW,     /* Before rendering a window (any pass) Note some windows (e.g. tiled) may have 2 passes (main & popup) */
    RENDER_POST_WINDOW = eRenderStage::RENDER_POST_WINDOW,    /* After rendering a window (any pass) */
};

enum class SnapPosition {
  NONE,
  MAX,
  LEFT,
  RIGHT,
  TOP_LEFT,
  TOP_RIGHT,
  BOTTOM_RIGHT,
  BOTTOM_LEFT
};

enum class RESIZE_TYPE {
  NONE,
  TOP,
  RIGHT,
  BOTTOM,
  LEFT,
  TOP_RIGHT,
  TOP_LEFT,
  BOTTOM_LEFT,
  BOTTOM_RIGHT,
};

static bool parse_hex(std::string hex, double *a, double *r, double *g, double *b) {
    while (hex[0] == '#') { // remove leading pound sign
        hex.erase(0, 1);
    }
    std::regex pattern("([0-9a-fA-F]{2})([0-9a-fA-F]{2})([0-9a-fA-F]{2})([0-9a-fA-F]{2})");
    
    std::smatch match;
    if (std::regex_match(hex, match, pattern)) {
        double t_a = std::stoul(match[4].str(), nullptr, 16);
        double t_r = std::stoul(match[1].str(), nullptr, 16);
        double t_g = std::stoul(match[2].str(), nullptr, 16);
        double t_b = std::stoul(match[3].str(), nullptr, 16);
        
        *a = t_a / 255;
        *r = t_r / 255;
        *g = t_g / 255;
        *b = t_b / 255;
        return true;
    }
    
    return false;
}

struct RGBA {
    double r, g, b, a;
    
    RGBA(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {
        
    }

    RGBA(std::string hex) {
        parse_hex(hex, &this->a, &this->r, &this->g, &this->b);
    }

    RGBA () {
        
    }

    bool operator==(const RGBA& other) const {
        constexpr double eps = 1e-9;
        return std::fabs(r - other.r) < eps &&
               std::fabs(g - other.g) < eps &&
               std::fabs(b - other.b) < eps &&
               std::fabs(a - other.a) < eps;
    }

    bool operator!=(const RGBA& other) const {
        return !(*this == other);
    }
};

struct Timer {
    wl_event_source *source = nullptr;
    std::function<void(Timer *)> func = nullptr;
    void *data = nullptr;
    bool keep_running = false;
    float delay = 0;
};

struct TextureInfo {
    int id = -1;
    int w = 0;
    int h = 0;

    std::string cached_text;
    RGBA cached_color;
    
    long last_reattempt_time = 0;
    int reattempts_count = 0;
    int cached_h = 0;
};

struct ThinClient {
    int id; // unique id which maps to a hyprland window

    int initial_x = 0; // before drag start
    int initial_y = 0; // before drag start

    bool snapped = false;
    SnapPosition snap_type = SnapPosition::NONE;
    Bounds pre_snap_bounds;
    float drag_initial_mouse_percentage = 0;

    bool resizing = false;
    int resize_type = 0;
    Bounds initial_win_box;

    std::string uuid;

    ThinClient(int _id) : id(_id) {}
};

struct ThinMonitor {
    int id; // unique id which maps to a hyprland monitor

    ThinMonitor(int _id) : id(_id) {}
};

struct HyprIso {
    bool no_render = false;

    bool dragging = false;
    int dragging_id = -1;
    long drag_stop_time = 0;
    Bounds drag_initial_mouse_pos;
    Bounds drag_initial_window_pos;
    
    bool resizing = false;
    int resizing_id = false;

    float get_varfloat(std::string target, float default_float = 1.0);
    RGBA get_varcolor(std::string target, RGBA default_color = {1.0, 0.0, 1.0, 1.0});

    void create_config_variables();
    
    // The main workhorse of the program which pumps events from hyprland to mylar
    void create_hooks();
    void create_callbacks();

    // So things can be cleaned
    void end(); 
    
    std::function<bool(int id, float x, float y)> on_mouse_move = nullptr;

    std::function<bool(int id, int button, int state, float x, float y)> on_mouse_press = nullptr;

    std::function<bool(int id, int source, int axis, int direction, double delta, int discrete, bool mouse)> on_scrolled = nullptr;

    std::function<bool(int id, int key, int state, bool update_mods)> on_key_press = nullptr;
    
    std::function<void(int id)> on_window_open = nullptr;
    
    std::function<void(int id)> on_window_closed = nullptr;

    std::function<void()> on_layer_change = nullptr;

    std::function<void(int id)> on_monitor_open = nullptr;
    
    std::function<void(int id)> on_monitor_closed = nullptr;
    
    std::function<void(int id)> on_activated = nullptr;
    
    std::function<void(std::string name, int monitor, int w, float a)> on_draw_decos = nullptr;
    
    std::function<void(int id, int stage)> on_render = nullptr;

    std::function<void(int id)> on_drag_start_requested = nullptr;
    std::function<void(int id, RESIZE_TYPE type)> on_resize_start_requested = nullptr;

    std::function<void()> on_config_reload = nullptr;
    void reload();

    //std::vector<ThinClient *> windows;
    //std::vector<ThinMonitor *> monitors;

    bool wants_titlebar(int id);
    void reserve_titlebar(int id, int size);

    float get_rounding(int id);

    std::string class_name(int id);
    std::string title_name(int id);
    
    void set_corner_rendering_mask_for_window(int id, int mask);
    
    void move(int id, int x, int y);
    void move_resize(int id, int x, int y, int w, int h, bool instant = true);
    void move_resize(int id, Bounds b, bool instant = true);
    int monitor_from_cursor();
    
    void send_key(uint32_t key);

    Bounds floating_offset(int id);
    Bounds workspace_offset(int id);

    Bounds min_size(int id);
    bool is_x11(int id);
    bool is_fullscreen(int id);
    bool has_decorations(int id);
    
    void bring_to_front(int id, bool focus = true);
    void set_hidden(int id, bool state);
    
    bool has_focus(int client);
    void all_lose_focus();
    void all_gain_focus();
    
    bool is_hidden(int id);
    
    void floatit(int id);

    void should_round(int id, bool state);

    void damage_entire(int monitor);
    void damage_box(Bounds b);

    void screenshot_all();
    void screenshot_deco(int id);
    void screenshot_space(int mon, int id);
    void screenshot_wallpaper(int mon);

    void draw_thumbnail(int id, Bounds b, int rounding = 0, float roundingPower = 2.0f, int cornermask = 0, float alpha = 1.0);
    void draw_deco_thumbnail(int id, Bounds b, int rounding = 0, float roundingPower = 2.0f, int cornermask = 0);
    void draw_raw_deco_thumbnail(int id, Bounds b, int rounding = 0, float roundingPower = 2.0f, int cornermask = 0);
    void draw_workspace(int mon, int id, Bounds b, int rounding = 0);
    void draw_wallpaper(int mon, Bounds b, int rounding = 0);
    

    void set_zoom_factor(float amount);
    int parent(int id);

    void set_reserved_edge(int side, int amount);

    void show_desktop();
    void hide_desktop();
    void move_to_workspace(int id, int workspace);
    void move_to_workspace(int workspace);

    std::vector<int> get_workspaces(int monitor);
    int get_active_workspace(int monitor);
    int get_workspace(int client);

    void add_float_rule();
    void overwrite_defaults();

    bool clip = false;
    Bounds clipbox;
};

extern HyprIso *hypriso;

void rect(Bounds box, RGBA color, int conrnermask = 0, float round = 0.0, float roundingPower = 2.0, bool blur = true, float blurA = 1.0);
void border(Bounds box, RGBA color, float size, int cornermask = 0, float round = 0.0, float roundingPower = 2.0, bool blur = true, float blurA = 1.0);
void shadow(Bounds box, RGBA color, float rounding, float roundingPower, float size);

static long get_current_time_in_ms() {
    using namespace std::chrono;
    milliseconds currentTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    return currentTime.count();
}

// ThinClient props
Bounds bounds(ThinClient *w);
Bounds real_bounds(ThinClient *w);
Bounds bounds_full(ThinClient *w);
std::string class_name(ThinClient *w);
std::string title_name(ThinClient *w);

// ThinMonitor props
Bounds bounds(ThinMonitor *m);
Bounds bounds_reserved(ThinMonitor *m);

Bounds bounds_monitor(int id);
Bounds bounds_reserved_monitor(int id);

Bounds bounds_client(int id);
Bounds real_bounds_client(int id);
Bounds bounds_full_client(int id);

int current_rendering_monitor();
int current_rendering_window();

float scale(int id);

std::vector<int> get_window_stacking_order();

void notify(std::string text);

void set_window_corner_mask(int id, int cornermask);

void free_text_texture(int id);
TextureInfo gen_text_texture(std::string font, std::string text, float h, RGBA color);
TextureInfo gen_texture(std::string path, float h);

void draw_texture(TextureInfo info, int x, int y, float a = 1.0, float clip_w = 0.0);

void setCursorImageUntilUnset(std::string cursor);
void unsetCursorImage();

int get_monitor(int client);

void close_window(int id);

Bounds mouse();

Timer* later(void* data, float time_ms, const std::function<void(Timer*)>& fn);

Timer* later(float time_ms, const std::function<void(Timer*)>& fn);

Timer* later_immediate(const std::function<void(Timer*)>& fn);

void request_refresh();
void request_refresh_only();

#endif // hypriso_h_INCLUDED
