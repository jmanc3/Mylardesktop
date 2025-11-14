#include "dock.h"

#include "second.h"

#include "client/raw_windowing.h"
#include "client/windowing.h"

#include <chrono>
#include <thread>

static RawApp *dock_app = nullptr;

void dock_start() {
    dock_app = windowing::open_app();
    auto mylar = open_mylar_window(dock_app, WindowType::NORMAL, Bounds(0, 0, 800, 600));
    
    windowing::main_loop(dock_app);
}

void dock::start() {
    notify("dock start");
    std::thread t(dock_start);
    t.detach();
}

void dock::stop() {
     windowing::close_app(dock_app);
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

