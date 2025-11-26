#pragma once

#include <string> 
#include <vector> 
#include <functional> 
#include "container.h"

struct PopupUserData : UserData {
    int cid = -1;
    int mid = -1;
};

struct PopOption {
    bool seperator = false; // Is just a line
    
    std::string text;
    std::string icon_left;

    std::vector<PopOption> submenu;

    bool closes_on_click = true;

    std::function<void()> on_clicked = nullptr;
};

namespace popup {
    void open(std::vector<PopOption> root, int x, int y, int cid = -1);
    void close(std::string container_uuid);
}
