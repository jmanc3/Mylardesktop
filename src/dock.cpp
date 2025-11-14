#include "dock.h"

#include "second.h"

#include "client/raw_windowing.h"
#include "client/windowing.h"

#include <cairo-deprecated.h>
#include <cairo.h>
#include <chrono>
#include <thread>

static RawApp *dock_app = nullptr;

void dock_start() {
    dock_app = windowing::open_app();
    RawWindowSettings settings;
    settings.pos.w = 800;
    settings.pos.h = 600;
    settings.name = "Dock";
    auto mylar = open_mylar_window(dock_app, WindowType::NORMAL, settings);
    mylar->root->real_bounds = Bounds(0, 0, 800, 600);
    mylar->root->user_data = mylar;
    mylar->root->when_paint = [](Container *root, Container *c) {
        auto mylar = (MylarWindow *) root->user_data;
        auto cr = mylar->raw_window->cr;
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_restore(cr);

        cairo_rectangle(cr, root->real_bounds.x, root->real_bounds.y, root->real_bounds.w, root->real_bounds.h);
        cairo_set_source_rgba(cr, 1, 1, 0, .4);
        cairo_paint(cr);
    };

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

