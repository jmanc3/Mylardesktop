
#include "alt_tab.h"

#include "second.h"

void alt_tab::on_window_open(int id) {
    
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
    alt_tab_parent->pre_layout = [](Container *root, Container *c, const Bounds &b) {
        auto [rid, s, stage, active_id] = from_root(root);
        c->wanted_bounds = Bounds(center_x(root, 200), center_y(root, 100), 200, 100);
        c->real_bounds = c->wanted_bounds;
    };
    alt_tab_parent->when_paint = paint {
        auto [rid, s, stage, active_id] = from_root(root);
        if (stage != (int) STAGE::RENDER_POST_WINDOWS)
            return;
        bool any_subpart_damaged = false;
        auto shown_yet = datum<bool>(c, "shown_yet");
        if (any_subpart_damaged || !(*shown_yet)) {
            request_damage(root, c); // request full redamage
        }
        *shown_yet = true;
        rect(c->real_bounds, {1, 1, 1, .7}, 20);
    };
}

void alt_tab::show() {
    notify("show");
    for (auto m : monitors) {
        auto alt_tab_parent = m->child(FILL_SPACE, FILL_SPACE);
        fill_root(m, alt_tab_parent);
        break;
    }
}

void alt_tab::close(bool focus) {
    notify("close");
    for (auto m : monitors) {
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

void alt_tab::on_activated(int id) {
    
}



