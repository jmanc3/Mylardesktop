/*
 *  The hyprland isolation file.
 *
 *  This file is the sole place where we anything hyprland specific is allowed to be included.
 *
 *  The purpose is to minimize our interaction surface so that our program stays as functional as possible on new updates, and we only need to fix up this file for new versions. 
 */

#include "hypriso.h"

#include "first.h"

#include <any>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>

HyprIso *hypriso = new HyprIso;

static int unique_id = 0;

struct HyprWindow {
    int id;  
    PHLWINDOW w;
};

static std::vector<HyprWindow *> hyprwindows;

struct HyprMonitor {
    int id;  
    PHLMONITOR m;
};

static std::vector<HyprMonitor *> hyprmonitors;

void on_open_window(PHLWINDOW w) {
    auto hw = new HyprWindow;
    hw->id = unique_id;
    hw->w = w;
    hyprwindows.push_back(hw);
    hypriso->on_window_open(hw->id);
    unique_id++;            
}

void on_close_window(PHLWINDOW w) {
    HyprWindow *hw = nullptr;
    int target_index = 0;
    for (int i = 0; i < hyprwindows.size(); i++) {
        auto hr = hyprwindows[i];
        if (hr->w == w) {
            target_index = i;
            hw = hr;
        }
    }
    if (hw) {
        hypriso->on_window_closed(hw->id);
        hyprwindows.erase(hyprwindows.begin() + target_index);
    }
}

void on_open_monitor(PHLMONITOR m) {
    auto hm = new HyprMonitor;
    hm->id = unique_id;
    hm->m = m;
    hyprmonitors.push_back(hm);
    hypriso->on_monitor_open(hm->id);
    unique_id++;            
}

void on_close_monitor(PHLMONITOR m) {
    HyprMonitor *hm = nullptr;
    int target_index = 0;
    for (int i = 0; i < hyprmonitors.size(); i++) {
        auto hr = hyprmonitors[i];
        if (hr->m == m) {
            target_index = i;
            hm = hr;
        }
    }
    if (hm) {
        hypriso->on_monitor_closed(hm->id);
        hyprmonitors.erase(hyprmonitors.begin() + target_index);
    }
}

void HyprIso::create_hooks_and_callbacks() {
    static auto mouseMove = HyprlandAPI::registerCallbackDynamic(globals->api, "mouseMove", 
        [](void* self, SCallbackInfo& info, std::any data) {
            auto  mouse = g_pInputManager->getMouseCoordsInternal();
            auto consume = false;
            if (hypriso->on_mouse_move) {
                auto  mouse = g_pInputManager->getMouseCoordsInternal();
                consume = hypriso->on_mouse_move(0, mouse.x, mouse.y);
            }
            info.cancelled = consume;
        });
    
    static auto mouseButton = HyprlandAPI::registerCallbackDynamic(globals->api, "mouseButton", 
        [](void* self, SCallbackInfo& info, std::any data) {
            auto e = std::any_cast<IPointer::SButtonEvent>(data);
            auto consume = false;
            if (hypriso->on_mouse_press) {
                auto  mouse = g_pInputManager->getMouseCoordsInternal();
                auto s = g_pCompositor->getMonitorFromCursor()->m_scale;
                consume = hypriso->on_mouse_press(e.mouse, e.button, e.state, mouse.x * s, mouse.y * s);
            }
            info.cancelled = consume;
        });
    
    static auto mouseAxis = HyprlandAPI::registerCallbackDynamic(globals->api, "mouseAxis", 
        [](void* self, SCallbackInfo& info, std::any data) {
            bool consume = false;
            auto p = std::any_cast<std::unordered_map<std::string, std::any>>(data);
            for (std::pair<const std::string, std::any> pair : p) {
                if (pair.first == "event") {
                    auto axisevent = std::any_cast<IPointer::SAxisEvent>(pair.second);
                    if (hypriso->on_scrolled) {
                        consume = hypriso->on_scrolled(0, axisevent.source, axisevent.axis, axisevent.relativeDirection, axisevent.delta, axisevent.deltaDiscrete, axisevent.mouse); 
                    }
                }
            }
            info.cancelled = consume;
        });
     
    static auto keyPress = HyprlandAPI::registerCallbackDynamic(globals->api, "keyPress", 
        [](void* self, SCallbackInfo& info, std::any data) {
            auto consume = false;
            if (hypriso->on_key_press) {
                auto p = std::any_cast<std::unordered_map<std::string, std::any>>(data);
                for (std::pair<const std::string, std::any> pair : p) {
                    if (pair.first == "event") {
                        auto skeyevent = std::any_cast<IKeyboard::SKeyEvent>(pair.second);
                        consume = hypriso->on_key_press(0, skeyevent.keycode, skeyevent.state, skeyevent.updateMods);
                    } else if (pair.first == "keyboard") {
                        auto ikeyboard = std::any_cast<Hyprutils::Memory::CSharedPointer<IKeyboard>>(pair.second);
                    }
                }
            }
            info.cancelled = consume;
        });


    static auto openWindow = HyprlandAPI::registerCallbackDynamic(globals->api, "openWindow", 
    [](void *self, SCallbackInfo& info, std::any data) {
        if (hypriso->on_window_open) {
            auto w = std::any_cast<PHLWINDOW>(data); // todo getorcreate ref on our side
            on_open_window(w);
        }
    });
    static auto closeWindow = HyprlandAPI::registerCallbackDynamic(globals->api, "closeWindow", 
    [](void *self, SCallbackInfo& info, std::any data) {
        if (hypriso->on_window_closed) {
            auto w = std::any_cast<PHLWINDOW>(data); // todo getorcreate ref on our side
            on_close_window(w);
        }
    });
    static auto monitorAdded = HyprlandAPI::registerCallbackDynamic(globals->api, "monitorAdded", 
    [](void *self, SCallbackInfo& info, std::any data) {
        if (hypriso->on_monitor_open) {
            auto m = std::any_cast<PHLMONITOR>(data); // todo getorcreate ref on our side
            on_open_monitor(m);
        }
    });
    static auto monitorRemoved = HyprlandAPI::registerCallbackDynamic(globals->api, "monitorRemoved", 
    [](void *self, SCallbackInfo& info, std::any data) {
        if (hypriso->on_monitor_closed) {
            auto m = std::any_cast<PHLMONITOR>(data); // todo getorcreate ref on our side
            on_close_monitor(m);            
        }
    });
    static auto render = HyprlandAPI::registerCallbackDynamic(globals->api, "render", 
    [](void *self, SCallbackInfo& info, std::any data) {
        if (hypriso->on_render) {
            for (auto m : hyprmonitors) {
                if (m->m == g_pHyprOpenGL->m_renderData.pMonitor) {
                    hypriso->on_render(m->id, (int) std::any_cast<eRenderStage>(data));
                }
            }
        }
    });

    for (auto w : g_pCompositor->m_windows) {
        on_open_window(w);
    }
    for (auto m : g_pCompositor->m_monitors) {
        on_open_monitor(m);
    }
}

void rect(Hyprutils::Math::CBox box, CHyprColor color, float round, float roundingPower, bool blur, float blurA) {
    CRectPassElement::SRectData rectdata;
    rectdata.color         = color;
    rectdata.box           = box;
    rectdata.blur          = blur;
    rectdata.blurA         = blurA;
    rectdata.round         = round;
    rectdata.roundingPower = roundingPower;
    rectdata.clipBox       = box;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rectdata));
}

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
    void                       onPositioningReply(const SDecorationPositioningReply& reply) { }
    void                       draw(PHLMONITOR, float const& a) { }
    eDecorationType            getDecorationType() { return eDecorationType::DECORATION_GROUPBAR; }
    void                       updateWindow(PHLWINDOW) { }
    void                       damageEntire() { } 
    bool                       onInputOnDeco(const eInputType, const Vector2D&, std::any = {}) { return false; }
    eDecorationLayer           getDecorationLayer() { return eDecorationLayer::DECORATION_LAYER_BOTTOM; }
    uint64_t                   getDecorationFlags() { return eDecorationFlags::DECORATION_ALLOWS_MOUSE_INPUT; }
    std::string                getDisplayName() { return "MylarBar"; }
};

void HyprIso::reserve_titlebar(ThinClient *c, int size) {
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == c->id) {
            if (auto w = hyprwindow->w.get()) {
                for (auto& wd : w->m_windowDecorations)
                    if (wd->getDisplayName() == "MylarBar")
                        return;

                auto m = makeUnique<MylarBar>(hyprwindow->w, size);
                HyprlandAPI::addWindowDecoration(globals->api, hyprwindow->w, std::move(m));
            }
        }
    }
}

void request_refresh() {
    for (auto m : g_pCompositor->m_monitors) {
        g_pHyprRenderer->damageMonitor(m);
        g_pCompositor->scheduleFrameForMonitor(m);
    }
}

Hyprutils::Math::CBox bounds(ThinClient *w) {
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == w->id) {
            if (auto w = hyprwindow->w.get()) {
                return w->getFullWindowBoundingBox();
            }
        }
    }    
    return {0, 0, 0, 0};
}

std::string class_name(ThinClient *w) {
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == w->id) {
            if (auto w = hyprwindow->w.get()) {
                return w->m_class;
            }
        }
    }
 
   return ""; 
}

std::string title_name(ThinClient *w) {
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == w->id) {
            if (auto w = hyprwindow->w.get()) {
                return w->m_title;
            }
        }
    }
 
    return "";
}

Hyprutils::Math::CBox bounds(ThinMonitor *m) {
    for (auto hyprmonitor : hyprmonitors) {
        if (hyprmonitor->id == m->id) {
            if (auto m = hyprmonitor->m.get()) {
                return m->logicalBox();
            }
        }
    }    
    return {0, 0, 0, 0};
}


void notify(std::string text) {
    HyprlandAPI::addNotification(globals->api, text, {1, 1, 1, 1}, 4000);
}

