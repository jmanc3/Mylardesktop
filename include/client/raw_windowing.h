#ifndef windowing_h_INCLUDED
#define windowing_h_INCLUDED

#include <functional>

struct RawApp {
    int id = -1;
};

struct RawWindow {
    int id = -1;

    RawApp *creator = nullptr;
    
    RawWindow *parent = nullptr;
    std::vector<RawWindow *> children;

    std::function<bool(RawWindow *, float x, float y)> on_mouse_move = nullptr;

    std::function<bool(RawWindow *, int button, int state, float x, float y)> on_mouse_press = nullptr;

    std::function<bool(RawWindow *, int source, int axis, int direction, double delta, int discrete, bool mouse)> on_scrolled = nullptr;

    std::function<bool(RawWindow *, int key, int state, bool update_mods)> on_key_press = nullptr;
    
    std::function<bool(RawWindow *, float x, float y)> on_mouse_enters = nullptr;
    
    std::function<bool(RawWindow *, float x, float y)> on_mouse_leaves = nullptr;

    std::function<bool(RawWindow *, float x, float y)> on_keyboard_focus = nullptr;
    
    std::function<bool(RawWindow *, float x, float y)> on_keyboard_focus_lost = nullptr;

    std::function<void(RawWindow *, int stage)> on_render = nullptr;

    std::function<void(RawWindow *, int stage)> on_resize = nullptr;
};

enum struct WindowType {
    NONE,
    NORMAL,
    DOCK,
};

struct PositioningInfo {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    
    int side = 0; // for docks
};

namespace windowing {
    RawApp *open_app();
    void close_app(RawApp *app);

    void add_fd(RawApp *app, int fd);
    void main_loop(RawApp *app);
    RawWindow *open_window(RawApp *app, WindowType type, PositioningInfo pos);
    void close_window(RawWindow *window);
};

#endif // windowing_h_INCLUDED
