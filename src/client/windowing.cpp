#include "client/windowing.h"

#include "second.h"
#include "events.h"

std::vector<MylarWindow *> mylar_windows;

bool on_mouse_move(RawWindow *rw, float x, float y) {
    log("on_mouse_move");
    return false;
}

bool on_mouse_press(RawWindow *rw, int button, int state, float x, float y) {
    log("on_mouse_press");
    return false;
}

bool on_scrolled(RawWindow *rw, int source, int axis, int direction, double delta, int discrete, bool mouse) {
    log("on_scrolled");
    return false;
}

bool on_key_press(RawWindow *rw, int key, int state, bool update_mods) {
    log("on_key_press");
    return false;
}
    
bool on_mouse_enters(RawWindow *rw, float x, float y) {
    log("on_mouse_enters");
    return false;
}
    
bool on_mouse_leaves(RawWindow *rw, float x, float y) {
    log("on_mouse_leaves");
    return false;
}

bool on_keyboard_focus(RawWindow *rw, bool gained) {
    log("on_keyboard_focus");
    return false;
}
    
void on_render(RawWindow *rw, int w, int h) {
    for (auto m : mylar_windows) {
        if (m->raw_window == rw) {
            m->root->real_bounds = Bounds(0, 0, w, h);
            paint_root(m->root);
        }
    }
    log("on_render");
}

void on_resize(RawWindow *rw, int w, int h) {
    log("on_resize");
}

MylarWindow *open_mylar_window(RawApp *app, WindowType type, RawWindowSettings settings) {
    auto m = new MylarWindow;
    m->raw_window = windowing::open_window(app, type, settings);
    m->root = new Container();
    m->raw_window->on_mouse_move = on_mouse_move;
    m->raw_window->on_mouse_press = on_mouse_press;
    m->raw_window->on_scrolled = on_scrolled;
    m->raw_window->on_key_press = on_key_press;
    m->raw_window->on_mouse_enters = on_mouse_enters;
    m->raw_window->on_mouse_leaves = on_mouse_leaves;
    m->raw_window->on_keyboard_focus = on_keyboard_focus;
    m->raw_window->on_render = on_render;
    m->raw_window->on_resize = on_resize;
    mylar_windows.push_back(m);
    return m;
}

