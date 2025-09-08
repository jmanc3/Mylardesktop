#include "startup.h"

#include "first.h"
#include "events.h"
#include "spring.h"
#include "container.h"
#include "hypriso.h"
#include <hyprland/src/render/OpenGL.hpp>

#include <format>

static Container *root = new Container;

// returning true means consume the event
bool on_mouse_move(int id, float x, float y) {
    Event event(x, y);
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

// i think this is being called once per monitor
void on_render(int id, int stage) {
    if (stage == (int) STAGE::RENDER_LAST_MOMENT) {
        auto m = g_pHyprOpenGL->m_renderData.pMonitor;
        auto l = m->logicalBox();
        l.scale(m->m_scale);
        ::layout(root, root, {l.x, l.y, l.w, l.h});
        paint_root(root);
        request_refresh();
    }
}

// returning true means consume the event
bool on_mouse_press(int id, int button, int state, float x, float y) {
    Event event(x, y, button, state);
    mouse_event(root, event);

    return false;
}

void on_window_open(int id) {
    // add a child to root which will rep the window titlebar
    auto tc = new ThinClient(id);
    hypriso->windows.push_back(tc);
    hypriso->reserve_titlebar(tc, 32);
}

void on_window_closed(int id) {
    for (int i = 0; i < hypriso->windows.size(); i++) {
        auto w = hypriso->windows[i];
        if (w->id == id)
            hypriso->windows.erase(hypriso->windows.begin() + i); 
    }
}

void on_monitor_open(int id) {
    auto tm = new ThinMonitor(id);
    hypriso->monitors.push_back(tm);
}

void on_monitor_closed(int id) {
    for (int i = 0; i < hypriso->monitors.size(); i++) {
        auto m = hypriso->monitors[i];
        if (m->id == id)
            hypriso->monitors.erase(hypriso->monitors.begin() + i); 
    } 
}

CBox tobox(Container *c) {
   return {c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h}; 
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

	auto b = root->child(::vbox, 200, 200);
	b->when_paint = [](Container *root, Container *c) {
    	if (c->state.mouse_pressing) {
            rect(tobox(c), {1, 1, 0, 1});	
    	} else {
            rect(tobox(c), {1, 1, 1, 1});	
    	}
	};
}

void startup::end() {
    
}


