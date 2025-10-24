/**
 * 
 * Event pumper and distributor
 * 
 */

#include "second.h"

#include "container.h"
#include "hypriso.h"
#include "titlebar.h"

static void any_container_closed(Container *) {
    
}

static bool on_mouse_move(int id, float x, float y) {
    return false;
}

static bool on_mouse_press(int id, int button, int state, float x, float y) {
    return false;
}

static bool on_scrolled(int id, int source, int axis, int direction, double delta, int discrete, bool mouse) {
    return false;
}

static bool on_key_press(int id, int key, int state, bool update_mods) {
    return false;
}

static void on_window_open(int id) {
    auto tc = new ThinClient(id);
    hypriso->windows.push_back(tc);
    
    set_window_corner_mask(id, 3);
    
    titlebar::on_window_open(id);
}

static void on_window_closed(int id) {
    titlebar::on_window_closed(id);

    for (int i = hypriso->windows.size() - 1; i >= 0; i--) {
        if (hypriso->windows[i]->id == id) {
            delete hypriso->windows[i];
            hypriso->windows.erase(hypriso->windows.begin() + i);
        }
    }
}

static void on_monitor_open(int id) {

}

static void on_monitor_closed(int id) {

}

static void on_activated(int id) {

}

static void on_draw_decos(std::string name, int monitor, int id, float a) {
    titlebar::on_draw_decos(name, monitor, id, a);
}

static void on_render(int id, int stage) {

}

static void on_drag_start_requested(int id) {

}

static void on_resize_start_requested(int id, RESIZE_TYPE type) {

}

static void on_config_reload() {

}

void second::begin() {
    on_any_container_close = any_container_closed;
    
    hypriso->create_config_variables();
        
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

	hypriso->create_callbacks();
	hypriso->create_hooks();
	
    hypriso->add_float_rule();
}

void second::end() {
    hypriso->end();    
}
