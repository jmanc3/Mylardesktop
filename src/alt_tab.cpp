
#include "alt_tab.h"

#include "second.h"

#define LAST_TIME_ACTIVE "last_time_active"

static float sd = .65;
Bounds max_thumb = { 510 * sd, 310 * sd, 510 * sd, 310 * sd };

void alt_tab::on_window_open(int id) {
    Container *c = get_cid_container(id);

    assert(c && "alt_tab::on_window_open assumes Container for id has already been created");
    
    *datum<long>(c, LAST_TIME_ACTIVE) = get_current_time_in_ms();
}

void alt_tab::on_window_closed(int id) {
    
}

void fill_root(Container *root, Container *alt_tab_parent) {
    *datum<bool>(alt_tab_parent, "shown_yet") = false;
    alt_tab_parent->custom_type = (int) TYPE::ALT_TAB;
    alt_tab_parent->type = ::vbox;
    alt_tab_parent->receive_events_even_if_obstructed = true;
    alt_tab_parent->when_mouse_down = consume_event;
    alt_tab_parent->when_mouse_motion = consume_event;
    alt_tab_parent->when_drag = consume_event;
    alt_tab_parent->when_mouse_up = consume_event;
    alt_tab_parent->when_mouse_enters_container = paint {
        hypriso->all_lose_focus();
        //notify("enteres");
        consume_event(root, c);
    };
    alt_tab_parent->when_mouse_leaves_container = paint {
        hypriso->all_gain_focus();
        //notify("leaves");
        consume_event(root, c);
    };
    alt_tab_parent->pre_layout = [](Container *actual_root, Container *c, const Bounds &b) {
        auto root = get_rendering_root();
        if (!root) return;
        auto [rid, s, stage, active_id] = roots_info(actual_root, root);

        

        c->wanted_bounds = Bounds(center_x(root, 600), center_y(root, 450), 600, 450);
        c->real_bounds = c->wanted_bounds;
        ::layout(actual_root, c, c->real_bounds);
    };
    alt_tab_parent->when_paint = [](Container *actual_root, Container *c) {
        auto root = get_rendering_root();
        if (!root) return;

        auto [rid, s, stage, active_id] = roots_info(actual_root, root);
        if (stage != (int) STAGE::RENDER_POST_WINDOWS)
            return;
        renderfix

        bool any_subpart_damaged = false;
        auto shown_yet = datum<bool>(c, "shown_yet");
        if (any_subpart_damaged || !(*shown_yet)) {
            request_damage(root, c); // request full redamage
        }
        *shown_yet = true;
        if (c->state.mouse_pressing) {
            rect(c->real_bounds, {1, 1, 1, 1});
        } else if (c->state.mouse_hovering) {
            rect(c->real_bounds, {1, 0, 1, 1});
        }
 
        rect(c->real_bounds, {1, 1, 1, .4}, 20);
    };
}

static bool is_showing = false;

bool alt_tab::showing() {
    return showing;
}

void alt_tab::show() {
    //return;
    if (is_showing)
        return;
    is_showing = true;
    later_immediate([](Timer *) {
        hypriso->screenshot_all(); 
    });
    {
        auto m = actual_root;
        auto alt_tab_parent = m->child(FILL_SPACE, FILL_SPACE);
        fill_root(m, alt_tab_parent);
    }
}

void alt_tab::close(bool focus) {
    if (!is_showing)
        return;
    is_showing = false;
    {
        auto m = actual_root;
        for (int i = m->children.size() - 1; i >= 0; i--) {
            auto c = m->children[i];
            if (c->custom_type == (int) TYPE::ALT_TAB) {
                request_damage(m, c);
                delete c;
                m->children.erase(m->children.begin() + i);
            }
        }
    }
}

void alt_tab::move(int dir) {
    
}

void alt_tab::on_activated(int id) {
    Container *c = get_cid_container(id);

    assert(c && "alt_tab::on_activated assumes Container for id exists");    
    
    *datum<long>(c, LAST_TIME_ACTIVE) = get_current_time_in_ms();
}



/*
void layout_containers(std::vector<Container*>& items, Container* root)
{
    if (!root || items.empty())
        return;

    const float rootW = root->real_w;
    const float rootH = root->real_h;

    // --- PREPASS: determine min/max constraints across items ---
    float minW = 0, minH = 0, maxW = FLT_MAX, maxH = FLT_MAX;
    for (auto c : items) {
        minW = std::max(minW, c->min_w);
        minH = std::max(minH, c->min_h);
        maxW = std::min(maxW, c->max_w);
        maxH = std::min(maxH, c->max_h);
    }

    // --- Find best column count: try all counts, choose one that yields largest usable size ---
    struct Candidate { int cols; float cellW; float cellH; float area; };
    Candidate best = {1, minW, minH, minW * minH};

    for (int cols = 1; cols <= (int)items.size(); ++cols) {
        float cellW = rootW / cols;
        if (cellW < minW) break;
        if (cellW > maxW) cellW = maxW;

        int rows = (items.size() + cols - 1) / cols;
        float cellH = rootH / rows;
        if (cellH < minH) continue;
        if (cellH > maxH) cellH = maxH;

        float area = cellW * cellH;
        if (area > best.area)
            best = {cols, cellW, cellH, area};
    }

    const int cols = best.cols;
    const float cellW = best.cellW;
    const float cellH = best.cellH;

    // --- Center entire grid inside the root ---
    int rows = (items.size() + cols - 1) / cols;
    float usedW = cols * cellW;
    float usedH = rows * cellH;

    float offsetX = root->x + (rootW - usedW) * 0.5f;
    float offsetY = root->y + (rootH - usedH) * 0.5f;

    // --- Assign positions ---
    for (int i = 0; i < (int)items.size(); ++i) {
        int col = i % cols;
        int row = i / cols;

        Container* c = items[i];
        c->real_w = cellW;
        c->real_h = cellH;
        c->x = offsetX + col * cellW;
        c->y = offsetY + row * cellH;
    }
}
*/
