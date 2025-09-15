#ifndef hypriso_h_INCLUDED
#define hypriso_h_INCLUDED

#include <hyprland/src/SharedDefs.hpp>

#include "container.h"
#include <string>
#include <vector>
#include <chrono>
#include <functional>

enum struct STAGE : uint8_t {
    RENDER_PRE = eRenderStage::RENDER_PRE,        /* Before binding the gl context */
    RENDER_BEGIN = eRenderStage::RENDER_BEGIN,          /* Just when the rendering begins, nothing has been rendered yet. Damage, current render data in opengl valid. */
    RENDER_POST_WALLPAPER = eRenderStage::RENDER_POST_WALLPAPER, /* After background layer, but before bottom and overlay layers */
    RENDER_PRE_WINDOWS = eRenderStage::RENDER_POST_WALLPAPER,    /* Pre windows, post bottom and overlay layers */
    RENDER_POST_WINDOWS = eRenderStage::RENDER_PRE_WINDOWS,   /* Post windows, pre top/overlay layers, etc */
    RENDER_LAST_MOMENT = eRenderStage::RENDER_POST_WINDOWS,    /* Last moment to render with the gl context */
    RENDER_POST = eRenderStage::RENDER_LAST_MOMENT,           /* After rendering is finished, gl context not available anymore */
    RENDER_POST_MIRROR = eRenderStage::RENDER_POST,    /* After rendering a mirror */
    RENDER_PRE_WINDOW = eRenderStage::RENDER_POST_MIRROR,     /* Before rendering a window (any pass) Note some windows (e.g. tiled) may have 2 passes (main & popup) */
    RENDER_POST_WINDOW = eRenderStage::RENDER_PRE_WINDOW,    /* After rendering a window (any pass) */
};

struct RGBA {
    float r, g, b, a;
    
    RGBA(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {
        
    }
};

struct TextureInfo {
    int id = -1;
    int w = 0;
    int h = 0;
};

struct ThinClient {
    int id; // unique id which maps to a hyprland window

    int initial_x = 0; // before drag start
    int initial_y = 0; // before drag start

    bool maximized = false;

    bool snaped = false;
    Bounds pre_snap_bounds;


    ThinClient(int _id) : id(_id) {}
};

struct ThinMonitor {
    int id; // unique id which maps to a hyprland monitor

    ThinMonitor(int _id) : id(_id) {}
};

struct HyprIso {

    // The main workhorse of the program which pumps events from hyprland to mylar
    void create_hooks_and_callbacks();

    // So things can be cleaned
    void end(); 
    
    std::function<bool(int id, float x, float y)> on_mouse_move = nullptr;

    std::function<bool(int id, int button, int state, float x, float y)> on_mouse_press = nullptr;

    std::function<bool(int id, int source, int axis, int direction, double delta, int discrete, bool mouse)> on_scrolled = nullptr;

    std::function<bool(int id, int key, int state, bool update_mods)> on_key_press = nullptr;
    
    std::function<void(int id)> on_window_open = nullptr;
    
    std::function<void(int id)> on_window_closed = nullptr;
    
    std::function<void(int id)> on_monitor_open = nullptr;
    
    std::function<void(int id)> on_monitor_closed = nullptr;
    
    std::function<void(int id, int stage)> on_render = nullptr;

    std::vector<ThinClient *> windows;
    std::vector<ThinMonitor *> monitors;

    void reserve_titlebar(ThinClient *c, int size);
    
};

extern HyprIso *hypriso;

void rect(Bounds box, RGBA color, int conrnermask = 0, float round = 0.0, float roundingPower = 2.0, bool blur = true, float blurA = 1.0);
void border(Bounds box, RGBA color, float size, int cornermask = 0, float round = 0.0, float roundingPower = 2.0, bool blur = true, float blurA = 1.0);

static long get_current_time_in_ms() {
    using namespace std::chrono;
    milliseconds currentTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    return currentTime.count();
}

void request_refresh();

// ThinClient props
Bounds bounds(ThinClient *w);
Bounds bounds_full(ThinClient *w);
std::string class_name(ThinClient *w);
std::string title_name(ThinClient *w);

// ThinMonitor props
Bounds bounds(ThinMonitor *m);

int current_rendering_monitor();
int current_rendering_window();

float scale(int id);

std::vector<int> get_window_stacking_order();

void move(int id, int x, int y);

void notify(std::string text);

void set_window_corner_mask(int id, int cornermask);

void free_text_texture(int id);
TextureInfo gen_text_texture(std::string font, std::string text, float h, RGBA color);

void draw_texture(TextureInfo info, int x, int y, float a = 1.0);

void setCursorImageUntilUnset(std::string cursor);
void unsetCursorImage();


#endif // hypriso_h_INCLUDED
