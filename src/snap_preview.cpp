#include "snap_preview.h"

#include "second.h"
#include "drag.h"

// c is a ::CLIENT
void snap_preview::draw(Container *actual_root, Container *c) {
    auto root = get_rendering_root();
    if (!root)
        return;
    auto [rid, s, stage, active_id] = roots_info(actual_root, root);
    auto cid = *datum<int>(c, "cid");

    if (!(active_id == cid && stage == (int)STAGE::RENDER_PRE_WINDOW))
        return;
    if (!(drag::dragging() && cid == drag::drag_window()))
        return;
    auto cursor_mon = hypriso->monitor_from_cursor();
    if (rid != cursor_mon)
        return;

    renderfix

    auto m = mouse();
    SnapPosition pos = mouse_to_snap_position(cursor_mon, m.x, m.y);
    if (pos != SnapPosition::NONE) {
        Bounds b = snap_position_to_bounds(rid, pos);
        b.shrink(10);
        b.x -= root->real_bounds.x;
        b.y -= root->real_bounds.y;
        b.scale(s);
        rect(b, {1, 1, 1, .3}, 0, 0, 2.0f, false, 0.0);
    }
}

