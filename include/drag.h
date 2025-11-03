#pragma once

namespace drag {
    void begin(int client_id);
    void motion(int client_id);
    void end(int client_id);
    
    bool dragging();
    int drag_window();
}
