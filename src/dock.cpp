#include "dock.h"

#include "second.h"

#include "client/raw_windowing.h"
#include "client/windowing.h"

#include <cairo-deprecated.h>
#include <cairo.h>
#include <chrono>
#include <thread>

static RawApp *dock_app = nullptr;

static void paint_root(Container *root, Container *c) {
    auto mylar = (MylarWindow *) root->user_data;
    auto cr = mylar->raw_window->cr;
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_rectangle(cr, root->real_bounds.x, root->real_bounds.y, root->real_bounds.w, root->real_bounds.h);
    cairo_set_source_rgba(cr, 0, 0, 0, .5);
    cairo_fill(cr);    
}

static void paint_battery(Container *root, Container *c) {
    auto mylar = (MylarWindow*)root->user_data;
    auto cr = mylar->raw_window->cr;
    if (c->state.mouse_pressing) {
        cairo_rectangle(cr, c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h);
        cairo_set_source_rgba(cr, 1, 1, 1, 1);
        cairo_fill(cr);
    } else if (c->state.mouse_hovering) {
        cairo_rectangle(cr, c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h);
        cairo_set_source_rgba(cr, 1, 0, 1, 1);
        cairo_fill(cr);
    } else {
        cairo_rectangle(cr, c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h);
        cairo_set_source_rgba(cr, 0, 0, 1, 1);
        cairo_fill(cr);
    }
}

static void fill_root(Container *root) {
    root->when_paint = paint_root;

    auto battery = root->child(40, FILL_SPACE);
    battery->when_paint = paint_battery;
    battery->when_clicked = paint {
        auto mylar = (MylarWindow*)root->user_data;
        windowing::close_window(mylar->raw_window);
    };
};

void dock_start() {
    dock_app = windowing::open_app();
    RawWindowSettings settings;
    settings.pos.w = 0;
    settings.pos.h = 40;
    settings.name = "Dock";
    auto mylar = open_mylar_window(dock_app, WindowType::DOCK, settings);
    mylar->root->user_data = mylar;
    mylar->root->alignment = ALIGN_RIGHT;
    fill_root(mylar->root);

    notify("be");
    windowing::main_loop(dock_app);
    notify("asdf");
}

void dock::start() {
    //return;
    notify("dock start");
    std::thread t(dock_start);
    t.detach();
}

void dock::stop() {
    //return;
    windowing::close_app(dock_app);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

