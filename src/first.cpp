#include "first.h"

//#include "startup.h"
#include "second.h"

#include "client/test.h"
#include <thread>

#include <hyprland/src/plugins/PluginAPI.hpp>

Globals *globals = new Globals;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}
/*
struct MylarBar : public IHyprWindowDecoration {
    PHLWINDOW m_window;
    int m_size;
    
    MylarBar(PHLWINDOW w, int size) : IHyprWindowDecoration(w) {
        m_window = w;
        m_size = size;
    }
    
    SDecorationPositioningInfo getPositioningInfo() { 
        SDecorationPositioningInfo info;
        info.policy         = DECORATION_POSITION_STICKY;
        info.edges          = DECORATION_EDGE_TOP;
        info.priority       = 10005;
        info.reserved       = true;
        info.desiredExtents = {{0, m_size}, {0, 0}};
        return info;
    }
    void onPositioningReply(const SDecorationPositioningReply& reply) { }
    void draw(PHLMONITOR monitor, float const& a) { 
    }
    eDecorationType            getDecorationType() { return eDecorationType::DECORATION_GROUPBAR; }
    void                       updateWindow(PHLWINDOW) { }
    void                       damageEntire() { } 
    bool                       onInputOnDeco(const eInputType, const Vector2D&, std::any = {}) { return false; }
    eDecorationLayer           getDecorationLayer() { return eDecorationLayer::DECORATION_LAYER_BOTTOM; }
    uint64_t                   getDecorationFlags() { return eDecorationFlags::DECORATION_ALLOWS_MOUSE_INPUT; }
    std::string                getDisplayName() { return "MylarBar"; }
};

void add_float_rule() {
    g_pConfigManager->handleWindowRule("windowrulev2", "float, class:.*");
}

void new_test() {
    static auto openWindow = HyprlandAPI::registerCallbackDynamic(globals->api, "openWindow",
    [](void *self, SCallbackInfo& info, std::any data) {
        auto w = std::any_cast<PHLWINDOW>(data); // todo getorcreate ref on our side

        auto m = makeUnique<MylarBar>(w, 30);
        HyprlandAPI::addWindowDecoration(globals->api, w, std::move(m));
    });

    static auto configReloaded = HyprlandAPI::registerCallbackDynamic(globals->api, "configReloaded",
    [](void *self, SCallbackInfo& info, std::any data) {
        add_float_rule();
    });
    add_float_rule();
}
*/
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) { // When started as a plugin
    globals->api = handle;

    //new_test();
                                                                  
    second::begin();
    //startup::begin();
    std::thread th([]() {
        //start_dock();        
    });
    th.detach();

    return {"Mylardesktop", "Mylar is a smooth and beautiful wayland desktop, written on Hyprland", "jmanc3", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
   //startup::end(); 
   second::end(); 
   stop_dock();
   std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void init_mylar(void* h) { // When started directly from hyprland
    PLUGIN_INIT(h);
}

void exit_mylar(void* h) {
    PLUGIN_EXIT();
}

