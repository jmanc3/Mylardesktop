#include "drag.h"

#include "second.h"
#include "defer.h"

#include <string>

struct DraggingData {
    int cid = -1;
    Bounds mouse_start;
    Bounds bounds_start;
};

DraggingData *data = nullptr;

void drag::begin(int cid) {
    //notify("begin");
    data = new DraggingData;
    data->cid = cid;
    data->mouse_start = mouse();
    data->bounds_start = bounds_client(cid);
}

void drag::motion(int cid) {
    if (!data)
        return;
    auto mouse_current = mouse();
    auto diff_x = mouse_current.x - data->mouse_start.x;
    auto diff_y = mouse_current.y - data->mouse_start.y;
    auto new_bounds = data->bounds_start;
    new_bounds.x += diff_x;
    new_bounds.y += diff_y;
    hypriso->move_resize(cid, new_bounds);
    hypriso->damage_entire(get_monitor(cid));
}

void drag::end(int cid) {
    //notify("end");
    delete data;
    data = nullptr;
}

bool drag::dragging() {
    if (!data)
        return false;
    return data->cid != -1;
}

int drag::drag_window() {
    if (!data)
        return -1;
    return data->cid;
}
