#pragma once

namespace drag {
    void begin(int client_id);
    void motion(int client_id);
    void end(int client_id);
    
    void snap_window(int snap_mon, int cid, int pos);
    
    bool dragging();
    int drag_window();
}
