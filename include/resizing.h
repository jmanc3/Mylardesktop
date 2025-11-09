#pragma once

namespace resizing {
    void on_window_open(int id);
    void on_window_closed(int id);
    
    void begin(int cid, int type);
    void motion(int cid);
    void end(int cid);

    bool resizing();
    int resizing_window();
}
