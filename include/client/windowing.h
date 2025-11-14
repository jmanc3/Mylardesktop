#pragma once

#include "container.h"
#include "client/raw_windowing.h"

struct MylarWindow {
    Container *root = nullptr;
    RawWindow *raw_window = nullptr; 
};

MylarWindow *open_mylar_window(RawApp *app, WindowType type, Bounds bounds);


