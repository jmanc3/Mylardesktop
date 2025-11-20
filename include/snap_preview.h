#pragma once

#include "container.h"

namespace snap_preview {
    void on_mouse_move(int x, int y); 
    void on_drag_start(int cid, int x, int y);
    void on_drag(int cid, int x, int y);
    void on_drag_end(int cid, int x, int y, int snap_type);
    void draw(Container *actual_root, Container *c);
}
