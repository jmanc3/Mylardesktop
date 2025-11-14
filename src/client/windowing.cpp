#include "client/windowing.h"

#include "second.h"
#include "events.h"

std::vector<MylarWindow *> mylar_windows;

static MylarWindow *mylar(RawWindow *rw) {
    for (auto m : mylar_windows) {
        if (m->raw_window == rw) {
            return m;
        }
    }
    return nullptr;
}

bool on_mouse_move(RawWindow *rw, float x, float y) {
    log("on_mouse_move");
    auto m = mylar(rw);
    if (!m) return false;
    ::layout(m->root, m->root, m->root->real_bounds);
    Event event(x, y);
    move_event(m->root, event);
    return false;
}

bool on_mouse_press(RawWindow *rw, int button, int state, float x, float y) {
    log("on_mouse_press");
    auto m = mylar(rw);
    if (!m) return false;
    ::layout(m->root, m->root, m->root->real_bounds);
    Event event(x, y, button, state);
    mouse_event(m->root, event);
    return false;
}

bool on_scrolled(RawWindow *rw, int source, int axis, int direction, double delta, int discrete, bool mouse) {
    // delta 3820
    log("on_scrolled");
    auto m = mylar(rw);
    if (!m) return false;
    ::layout(m->root, m->root, m->root->real_bounds);
    Event event;
    event.x = m->root->mouse_current_x;
    event.y = m->root->mouse_current_x;
    event.scroll = true;
    event.axis = axis;
    event.direction = direction;
    event.delta = delta * .0001;
    event.descrete = discrete;
    event.from_mouse = mouse;
    second::layout_containers();
    mouse_event(m->root, event);
    return false;
}

bool on_key_press(RawWindow *rw, int key, int state, bool update_mods) {
    log("on_key_press");
    return false;
}
    
bool on_mouse_enters(RawWindow *rw, float x, float y) {
    log("on_mouse_enters");
    on_mouse_move(rw, x, y);
    return false;
}
    
bool on_mouse_leaves(RawWindow *rw, float x, float y) {
    log("on_mouse_leaves");
    on_mouse_move(rw, -1000, -1000);
    return false;
}

bool on_keyboard_focus(RawWindow *rw, bool gained) {
    log("on_keyboard_focus");
    return false;
}
    
void on_render(RawWindow *rw, int w, int h) {
    log("on_render");
    auto m = mylar(rw);
    if (!m) return;
    m->root->real_bounds = Bounds(0, 0, w, h);
    m->root->wanted_bounds = m->root->real_bounds;
    ::layout(m->root, m->root, m->root->real_bounds);
    paint_root(m->root);
}

void on_resize(RawWindow *rw, int w, int h) {
    log("on_resize");
    auto m = mylar(rw);
    if (!m) return;
    m->root->real_bounds = Bounds(0, 0, w, h);
}

MylarWindow *open_mylar_window(RawApp *app, WindowType type, RawWindowSettings settings) {
    auto m = new MylarWindow;
    m->raw_window = windowing::open_window(app, type, settings);
    assert(m->raw_window);
    m->root = new Container();
    m->root->real_bounds = Bounds(0, 0, settings.pos.w, settings.pos.h);
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

