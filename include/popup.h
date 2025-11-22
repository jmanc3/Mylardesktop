#pragma once

#include <string> 
#include <vector> 
#include <functional> 

struct PopOption {
    std::string text;
    std::string icon_left;

    std::vector<PopOption> submenu;

    bool closes_on_click = true;

    std::function<void()> on_clicked = nullptr;
};

namespace popup {
    void open(std::vector<PopOption> root, int x, int y);
    void close(std::string container_uuid);
}
