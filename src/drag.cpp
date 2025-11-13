#include "drag.h"

#include "second.h"
#include "defer.h"

#include <string>
#include <cmath>

struct DraggingData {
    int cid = -1;
    Bounds mouse_start;
    Bounds bounds_start;
};

DraggingData *data = nullptr;

void drag::begin(int cid) {
    //notify(fz("wants no decorations {}", hypriso->requested_client_side_decorations(cid)));
    data = new DraggingData;
    data->cid = cid;
    data->mouse_start = mouse();
    auto c = get_cid_container(cid);
    if (*datum<bool>(c, "drag_from_titlebar")) {
        data->mouse_start = *datum<Bounds>(c, "initial_click_position");
    }
    auto client_snapped = datum<bool>(c, "snapped");
    if (*client_snapped) {
        data->bounds_start = bounds_client(cid);

        *client_snapped = false;
        hypriso->should_round(cid, true);

        auto client_pre_snap_bounds = *datum<Bounds>(c, "pre_snap_bounds");
        //data->bounds_start.w = client_pre_snap_bounds.w;
        //data->bounds_start.h = client_pre_snap_bounds.h;

        auto b = data->bounds_start;
        auto MOUSECOORDS = data->mouse_start;
        auto mb = bounds_monitor(get_monitor(cid));

        float perc = (MOUSECOORDS.x - b.x) / b.w;
        bool window_left_side = b.x < mb.x + b.w * .5;
        bool click_left_side = perc <= .5;
        float size_from_left = b.w * perc;
        float size_from_right = b.w - size_from_left;
        bool window_smaller_after = b.w > client_pre_snap_bounds.w;
        float x = MOUSECOORDS.x - (perc * (client_pre_snap_bounds.w)); // perc based relocation
        log(fz("{} {} {} {} {} {} {}", perc, window_left_side, click_left_side, size_from_left, size_from_right, window_smaller_after, x));
        // keep window fully on screen
        if (!window_smaller_after) {
            if (click_left_side) {
                if (window_left_side) {
                    x = MOUSECOORDS.x - size_from_left;
                } else {
                    x = MOUSECOORDS.x - client_pre_snap_bounds.w + size_from_right;
                }
            } else {
                if (window_left_side) {
                    x = b.x;
                } else {
                    x = MOUSECOORDS.x - client_pre_snap_bounds.w + size_from_right;
                }
            }
        } else {
            // if offset larger than resulting window use percentage
        }

        hypriso->move_resize(cid, 
            x, 
            b.y, 
            client_pre_snap_bounds.w, 
            client_pre_snap_bounds.h);
        
        data->bounds_start = bounds_client(cid);
    } else {
        data->bounds_start = bounds_client(cid);
    }
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
    for (auto m : actual_monitors) {
        auto rid = *datum<int>(m, "cid");
        hypriso->damage_entire(rid);
    }
}

// TODO: multi-monitor broken
// I don't think it's mon, it's workspace that we have to worry about
void drag::snap_window(int snap_mon, int cid, int pos) {
    if (snap_mon == -1)
        return;
    auto c = get_cid_container(cid);
    if (!c) return;

    auto snapped = datum<bool>(c, "snapped");
    
    if (!(*snapped) && pos == (int) SnapPosition::NONE) // no need to unsnap
        return;

    if (*snapped) {
        // perform unsnap
        *snapped = false; 
        auto p = *datum<Bounds>(c, "pre_snap_bounds");
        auto mon = get_monitor(cid);
        auto bounds = bounds_reserved_monitor(mon);
        p.x = bounds.x + bounds.w * .5 - p.w * .5; 
        if (p.x < bounds.x)
            p.x = bounds.x;
        p.y = bounds.y + bounds.h * .5 - p.h * .5; 
        if (p.y < bounds.y)
            p.y = bounds.y;
        hypriso->move_resize(cid, p.x, p.y, p.w, p.h, false);
        hypriso->should_round(cid, true);
    } else {
        // perform snap
        *snapped = true; 
        *datum<Bounds>(c, "pre_snap_bounds") = bounds_client(cid);
        *datum<int>(c, "snap_type") = pos;

        auto p = snap_position_to_bounds(snap_mon, (SnapPosition) pos);
        if (pos != (int) SnapPosition::MAX) {
            //p.shrink(1 * scale(snap_mon));
        }
        float scalar = hypriso->has_decorations(cid); // if it has a titlebar
        hypriso->move_resize(cid, p.x, p.y + titlebar_h * scalar, p.w, p.h - titlebar_h * scalar, false);
        hypriso->should_round(cid, false);
    }
    hypriso->damage_entire(snap_mon);

    // This sends
    later(new int(0), 10, [](Timer *t) {
        t->keep_running = true;
        int *times = (int *) t->data; 
        *times = (*times) + 1;
        if (*times > 20) {
            t->keep_running = false;
            delete (int *) t->data;
        }
        if (hypriso->on_mouse_move) {
            auto m = mouse();
            hypriso->on_mouse_move(0, m.x, m.y);
        }
    });
}

void drag::end(int cid) {
    //notify("end");
    delete data;
    data = nullptr;

    int mon = hypriso->monitor_from_cursor();
    auto m = mouse();
    auto pos = mouse_to_snap_position(mon, m.x, m.y);
    snap_window(mon, cid, (int) pos);
    *datum<long>(actual_root, "drag_end_time") = get_current_time_in_ms();

    if (auto c = get_cid_container(cid)) {
        *datum<bool>(c, "drag_from_titlebar") = false;
    }
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
