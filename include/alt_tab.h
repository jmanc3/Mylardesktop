#pragma once

namespace alt_tab {
    void on_window_open(int id);
    void on_window_closed(int id);
    void on_activated(int id); // on a window become the focused window that is
    
    void show();
    void close(bool focus = false);

    void move(int dir);

    bool showing();
}
