#include "snap_preview.h"

#include "second.h"
#include "drag.h"
#include "spring.h"
#include <iterator>
#include <sys/types.h>

static Timer *timer = nullptr;

struct SnapPreview {
    Bounds start;
    Bounds current;
    Bounds end;
    long time_since_change = 0;
    float scalar = 0; // 0->1

    bool is_animating = false;
    bool pos_is_none = true;
    SnapPosition target_pos = SnapPosition::NONE;
    int cid = -1;
};

static SnapPreview *preview = new SnapPreview;

static float total_time = 210.0f;
static float pad = 10.0f;

enum transition_type {
    unknown,
    none_to_none,
    none_to_some,
    some_to_none,
    some_to_same_some,
    some_to_other_some,
};

void calculate_current() {
    preview->current = {
        preview->start.x + ((preview->end.x - preview->start.x) * preview->scalar),
        preview->start.y + ((preview->end.y - preview->start.y) * preview->scalar),
        preview->start.w + ((preview->end.w - preview->start.w) * preview->scalar),
        preview->start.h + ((preview->end.h - preview->start.h) * preview->scalar),
    };
}

void update_snap_preview(int cid) {
    auto m = mouse();
    auto mon_cursor_on = hypriso->monitor_from_cursor();
    static SnapPosition previous_pos = SnapPosition::NONE;
    SnapPosition pos = mouse_to_snap_position(mon_cursor_on, m.x, m.y);
    auto target_bounds = snap_position_to_bounds(mon_cursor_on, pos);
    defer(previous_pos = pos);

    auto current = get_current_time_in_ms();
    if (previous_pos != pos) {
        preview->time_since_change = current;
        preview->scalar = 0;
    }
    preview->is_animating = (current - preview->time_since_change) < total_time;

    // determine transition type
    // 
    int transition_type = 0;
    if (pos == previous_pos) {
        if (pos == SnapPosition::NONE) {
            transition_type = none_to_none;
        } else {
            transition_type = some_to_same_some;
        }
    } else {
       if (previous_pos == SnapPosition::NONE) {
           transition_type = none_to_some;
       } else if (pos == SnapPosition::NONE) {
           transition_type = some_to_none;
       } else {
           transition_type = some_to_other_some;
       }
    }

    // update preview goals
    // 
    auto client_bounds = bounds_client(cid);
    if (hypriso->has_decorations(cid)) {
        client_bounds.y -= titlebar_h;
        client_bounds.h += titlebar_h;
    }
    if (transition_type == none_to_some) {
        if (preview->is_animating) {
            preview->start = preview->current;
            preview->end =  target_bounds; 
        } else {
            preview->start = client_bounds;
            preview->end =  target_bounds;
        }
    }

    if (transition_type == some_to_none) {
        preview->start = preview->current;
        preview->end = client_bounds;
    }

    if (transition_type == some_to_other_some) {
        preview->start = preview->current;
        preview->end = target_bounds;
    }

    if (transition_type == none_to_none) {
        preview->end = client_bounds; 
    }

    // fix end
    {
        auto mbounds = bounds_monitor(mon_cursor_on);
        mbounds.grow(pad);
        if (preview->end.x < mbounds.x) {
            preview->end.w -= mbounds.x - preview->end.x + pad;
            preview->end.x = mbounds.x;
        }
        if (preview->end.y < mbounds.y) {
            preview->end.h -= mbounds.y - preview->end.y + pad;
            preview->end.y = mbounds.y;
        }
        if (preview->end.x + preview->end.w > mbounds.x + mbounds.w) {
            preview->end.w -= ((preview->end.x + preview->end.w) - (mbounds.x + mbounds.w)) ;
        }
        if (preview->end.y + preview->end.h > mbounds.y + mbounds.h) {
            preview->end.h -= ((preview->end.y + preview->end.h) - (mbounds.y + mbounds.h)) ;
        }
    }

    preview->target_pos = pos;
    
    calculate_current();

    if (!timer) {
        timer = later(nullptr, 5.0, [](Timer *t) {
            t->keep_running = true;

            auto current = get_current_time_in_ms();
            auto dt = current - preview->time_since_change;
            auto state = springEvaluate(dt, 0.0f, 1000.0f, 0.0f, SpringParams{total_time, 1.0});

            if (dt < total_time * 3) {
                preview->is_animating = true;
                preview->scalar = state.value / 1000.0f;
                calculate_current();
            } else {
                preview->is_animating = false;
                preview->cid = -1;
                preview->scalar = 1.0;
                calculate_current();
                t->keep_running = false;
                timer = nullptr;
            }
            int m = hypriso->monitor_from_cursor();
            if (m != -1)
                hypriso->damage_entire(m);
        });
    }
}

// c is a ::CLIENT
void snap_preview::draw(Container *actual_root, Container *c) {
    auto root = get_rendering_root();
    if (!root)
        return;
    auto [rid, s, stage, active_id] = roots_info(actual_root, root);
    auto cursor_mon = hypriso->monitor_from_cursor();
    if (rid != cursor_mon)
        return;
    auto cid = *datum<int>(c, "cid");

    bool should_draw = preview->is_animating || preview->target_pos != SnapPosition::NONE;
    if (!(active_id == cid && stage == (int)STAGE::RENDER_PRE_WINDOW))
        return;
    if (!(drag::dragging() && cid == drag::drag_window()))
        return;

    renderfix

    update_snap_preview(cid);    
    auto pos = preview->current;
    pos.round();
    if (preview->is_animating || preview->target_pos != SnapPosition::NONE) {
        pos.shrink(pad);
        pos.x -= root->real_bounds.x;
        pos.y -= root->real_bounds.y;
        pos.scale(s); 
        rect(pos, {1, 1, 1, .3}, 0, hypriso->get_rounding(cid) * s, 2.0f, false, 0.0);
    }

    /*
    auto pos = preview->current;
    
    if (pos != SnapPosition::NONE) {
        Bounds b = snap_position_to_bounds(rid, pos);
        b.shrink(10);
        b.x -= root->real_bounds.x;
        b.y -= root->real_bounds.y;
        b.scale(s);
        rect(b, {1, 1, 1, .3}, 0, 0, 2.0f, false, 0.0);
    }*/
}

