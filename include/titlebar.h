#ifndef titlebar_h_INCLUDED
#define titlebar_h_INCLUDED

#include <string>

namespace titlebar {
    void on_window_open(int id);
    void on_window_closed(int id);
    void on_draw_decos(std::string name, int monitor, int id, float a);
}

#endif // titlebar_h_INCLUDED
