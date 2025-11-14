#include "client/windowing.h"

bool on_mouse_move(RawWindow *rw, float x, float y) {
    return false;
}

bool on_mouse_press(RawWindow *rw, int button, int state, float x, float y) {
    return false;
}

bool on_scrolled(RawWindow *rw, int source, int axis, int direction, double delta, int discrete, bool mouse) {
    return false;
}

bool on_key_press(RawWindow *rw, int key, int state, bool update_mods) {
    return false;
}
    
bool on_mouse_enters(RawWindow *rw, float x, float y) {
    return false;
}
    
bool on_mouse_leaves(RawWindow *rw, float x, float y) {
    return false;
}

bool on_keyboard_focus(RawWindow *rw, float x, float y) {
    return false;
}
    
bool on_keyboard_focus_lost(RawWindow *rw, float x, float y) {
    return false;
}

void on_render(RawWindow *rw, int stage) {
}

void on_resize(RawWindow *rw, int stage) {
}

MylarWindow *open_mylar_window(RawApp *app, WindowType type, Bounds bounds) {
    auto m = new MylarWindow;
    m->raw_window = windowing::open_window(app, type, PositioningInfo(bounds.x, bounds.y, bounds.w, bounds.h, 0));
    m->root = new Container();
    m->raw_window->on_mouse_move = on_mouse_move;
    m->raw_window->on_mouse_press = on_mouse_press;
    m->raw_window->on_scrolled = on_scrolled;
    m->raw_window->on_key_press = on_key_press;
    m->raw_window->on_mouse_enters = on_mouse_enters;
    m->raw_window->on_mouse_leaves = on_mouse_leaves;
    m->raw_window->on_keyboard_focus = on_keyboard_focus;
    m->raw_window->on_keyboard_focus_lost = on_keyboard_focus_lost;
    m->raw_window->on_render = on_render;
    m->raw_window->on_resize = on_resize;
    return m;
}

