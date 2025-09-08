#include "first.h"

#include "startup.h"

#include <hyprland/src/plugins/PluginAPI.hpp>

Globals *globals = new Globals;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) { // When started as a plugin
    globals->api = handle;

    startup::begin();

    return {"Mylardesktop", "Mylar is a smooth and beautiful wayland desktop, written on Hyprland", "jmanc3", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
   startup::end(); 
}

void init_mylar(void* h) { // When started directly from hyprland
    PLUGIN_INIT(h);
}

void exit_mylar(void* h) {
    PLUGIN_EXIT();
}

