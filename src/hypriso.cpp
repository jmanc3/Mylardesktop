/*
 *  The hyprland isolation file.
 *
 *  This file is the sole place where we anything hyprland specific is allowed to be included.
 *
 *  The purpose is to minimize our interaction surface so that our program stays as functional as possible on new updates, and we only need to fix up this file for new versions. 
 */

#include "hypriso.h"
#include "second.h"

#include <cstring>

#ifdef TRACY_ENABLE
#include "tracy/Tracy.hpp"
#endif

#include "container.h"
#include "first.h"
#include <cassert>
#include <hyprland/src/helpers/time/Time.hpp>

#include <algorithm>
#include <hyprutils/math/Vector2D.hpp>
#include <librsvg/rsvg.h>
#include <any>

#include <hyprland/src/helpers/Color.hpp>
#include <kde-server-decoration.hpp>
//#include <hyprland/protocols/kde-server-decoration.hpp>
//#include <hyprland/protocols/wlr-layer-shell-unstable-v1.hpp>

#include <hyprland/src/render/pass/ShadowPassElement.hpp>
#include <hyprland/src/render/pass/RendererHintsPassElement.hpp>
#include <hyprland/src/desktop/LayerSurface.hpp>
#include <hyprland/src/protocols/core/DataDevice.hpp>
#include <hyprland/src/protocols/PointerConstraints.hpp>
#include <hyprland/src/protocols/RelativePointer.hpp>
#include <hyprland/src/protocols/SessionLock.hpp>
#include <hyprland/src/protocols/LayerShell.hpp>

#define private public
#include <hyprland/src/render/pass/SurfacePassElement.hpp>
#include <hyprland/src/protocols/ServerDecorationKDE.hpp>
#include <hyprland/src/protocols/XDGDecoration.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#undef private

#include <hyprland/src/xwayland/XWayland.hpp>
#include <hyprland/src/xwayland/XWM.hpp>

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/managers/HookSystemManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include <hyprutils/utils/ScopeGuard.hpp>

#include <hyprlang.hpp>

HyprIso *hypriso = new HyprIso;

static int unique_id = 0;

void* pRenderWindow = nullptr;
void* pRenderLayer = nullptr;
void* pRenderMonitor = nullptr;
void* pRenderWorkspace = nullptr;
void* pRenderWorkspaceWindows = nullptr;
void* pRenderWorkspaceWindowsFullscreen = nullptr;
typedef void (*tRenderWindow)(void *, PHLWINDOW, PHLMONITOR, const Time::steady_tp&, bool decorate, eRenderPassMode, bool ignorePosition, bool standalone);
typedef void (*tRenderMonitor)(void *, PHLMONITOR pMonitor, bool commit);
typedef void (*tRenderWorkspace)(void *, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp &, const CBox &geom);
typedef void (*tRenderWorkspaceWindows)(void *, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp &);
typedef void (*tRenderWorkspaceWindowsFullscreen)(void *, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp &);

struct HyprWindow {
    int id;  
    PHLWINDOW w;

    bool is_hidden = false; // used in show/hide desktop
    bool was_hidden = false; // used in show/hide desktop
    
    CFramebuffer *fb = nullptr;
    Bounds w_size; // 0 -> 1, percentage of fb taken up by the actual window used for drawing
    
    CFramebuffer *deco_fb = nullptr;
    Bounds w_decos_size; // 0 -> 1, percentage of fb taken up by the actual window used for drawing

    int cornermask = 0; // when rendering the surface, what corners should be rounded
    bool no_rounding = false;
};

static std::vector<HyprWindow *> hyprwindows;

struct HyprMonitor {
    int id;  
    PHLMONITOR m;

    CFramebuffer *wallfb = nullptr;
    Bounds wall_size; // 0 -> 1, percentage of fb taken up by the actual window used for drawing
};

static std::vector<HyprMonitor *> hyprmonitors;

struct HyprWorkspaces {
    int id;
    PHLWORKSPACEREF w;
    CFramebuffer *buffer;
};

static std::vector<HyprWorkspaces *> hyprspaces;

struct Texture {
    SP<CTexture> texture;
    TextureInfo info;
};

static std::vector<Texture *> hyprtextures;

class AnyPass : public IPassElement {
public:
    struct AnyData {
        std::function<void(AnyPass*)> draw = nullptr;
        CBox box = {};
    };
    AnyData* m_data = nullptr;

    AnyPass(const AnyData& data) {
        m_data       = new AnyData;
        m_data->draw = data.draw;
    }
    virtual ~AnyPass() {
        delete m_data;
    }

    virtual void draw(const CRegion& damage) {
        // here we can draw anything
        if (m_data->draw) {
            m_data->draw(this);
        }
    }
    virtual bool needsLiveBlur() {
        return false;
    }
    virtual bool needsPrecomputeBlur() {
        return false;
    }
    //virtual std::optional<CBox> boundingBox() {
        //return {};
    //}
    
    virtual const char* passName() {
        return "CAnyPassElement";
    }
};

void set_rounding(int mask) {
    //return; //possibly the slow bomb
    if (!g_pHyprOpenGL || !g_pHyprOpenGL->m_shaders) {
        return;
    }
    // todo set shader mask to 3, and then to 0 afterwards
    g_pHyprOpenGL->useProgram(g_pHyprOpenGL->m_shaders->m_shQUAD.program);
    GLint loc = glGetUniformLocation(g_pHyprOpenGL->m_shaders->m_shQUAD.program, "cornerDisableMask");
    glUniform1i(loc, mask);
    GLint value;

    g_pHyprOpenGL->useProgram(g_pHyprOpenGL->m_shaders->m_shRGBA.program);
    loc = glGetUniformLocation(g_pHyprOpenGL->m_shaders->m_shRGBA.program, "cornerDisableMask");
    glUniform1i(loc, mask);

    g_pHyprOpenGL->useProgram(g_pHyprOpenGL->m_shaders->m_shRGBX.program);
    loc = glGetUniformLocation(g_pHyprOpenGL->m_shaders->m_shRGBX.program, "cornerDisableMask");
    glUniform1i(loc, mask);

    g_pHyprOpenGL->useProgram(g_pHyprOpenGL->m_shaders->m_shEXT.program);
    loc = glGetUniformLocation(g_pHyprOpenGL->m_shaders->m_shEXT.program, "cornerDisableMask");
    glUniform1i(loc, mask);

    g_pHyprOpenGL->useProgram(g_pHyprOpenGL->m_shaders->m_shCM.program);
    loc = glGetUniformLocation(g_pHyprOpenGL->m_shaders->m_shCM.program, "cornerDisableMask");
    glUniform1i(loc, mask);

    g_pHyprOpenGL->useProgram(g_pHyprOpenGL->m_shaders->m_shPASSTHRURGBA.program);
    loc = glGetUniformLocation(g_pHyprOpenGL->m_shaders->m_shPASSTHRURGBA.program, "cornerDisableMask");
    glUniform1i(loc, mask);

    g_pHyprOpenGL->useProgram(g_pHyprOpenGL->m_shaders->m_shBORDER1.program);
    loc = glGetUniformLocation(g_pHyprOpenGL->m_shaders->m_shBORDER1.program, "cornerDisableMask");
    glUniform1i(loc, mask);
}

PHLWINDOW get_window_from_mouse() {
    const auto      MOUSECOORDS = g_pInputManager->getMouseCoordsInternal();
    const PHLWINDOW PWINDOW     = g_pCompositor->vectorToWindowUnified(MOUSECOORDS, RESERVED_EXTENTS | INPUT_EXTENTS | ALLOW_FLOATING);
    return PWINDOW;
}

void on_open_monitor(PHLMONITOR m);
void interleave_floating_and_tiled_windows();

static void float_target(PHLWINDOW PWINDOW) {
    if (!PWINDOW)
        return;
    if (PWINDOW->m_isFloating)
        return;

    // remove drag status
    if (!g_pInputManager->m_currentlyDraggedWindow.expired())
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);

    if (PWINDOW->m_groupData.pNextWindow.lock() && PWINDOW->m_groupData.pNextWindow.lock() != PWINDOW) {
        const auto PCURRENT = PWINDOW->getGroupCurrent();

        PCURRENT->m_isFloating = true;
        g_pLayoutManager->getCurrentLayout()->changeWindowFloatingMode(PCURRENT);

        PHLWINDOW curr = PCURRENT->m_groupData.pNextWindow.lock();
        while (curr != PCURRENT) {
            curr->m_isFloating = PCURRENT->m_isFloating;
            curr               = curr->m_groupData.pNextWindow.lock();
        }
    } else {
        PWINDOW->m_isFloating = true;

        g_pLayoutManager->getCurrentLayout()->changeWindowFloatingMode(PWINDOW);
    }

    if (PWINDOW->m_workspace) {
        PWINDOW->m_workspace->updateWindows();
        PWINDOW->m_workspace->updateWindowData();
    }

    g_pLayoutManager->getCurrentLayout()->recalculateMonitor(PWINDOW->monitorID());
    g_pCompositor->updateAllWindowsAnimatedDecorationValues();

    return;
}

void on_open_window(PHLWINDOW w) {
    for (auto m : g_pCompositor->m_monitors) {
        on_open_monitor(m);
    }
   if (auto surface = w->m_xdgSurface) {
        if (auto toplevel = surface->m_toplevel.lock()) {
            auto resource = toplevel->m_resource;
            if (resource) {
                resource->setMove([](CXdgToplevel*, wl_resource*, uint32_t) {
                    //notify("move requested");
                    if (hypriso->on_drag_start_requested) {
                        if (auto w = get_window_from_mouse()) {
                            for (auto hw : hyprwindows) {
                                if (w == hw->w) {
                                    hypriso->on_drag_start_requested(hw->id);
                                }
                            }
                        }
                    }
                });
                resource->setResize([](CXdgToplevel* t, wl_resource*, uint32_t, xdgToplevelResizeEdge e) {
                    for (auto w : g_pCompositor->m_windows) {
                        if (auto surf = w->m_xdgSurface.lock()) {
                            if (auto top = surf->m_toplevel.lock()) {
                                auto resource = top->m_resource;
                                if (resource.get() == t) {
                                    auto type = RESIZE_TYPE::NONE;
                                    if (e == XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
                                        type = RESIZE_TYPE::NONE;
                                    } else if (e == XDG_TOPLEVEL_RESIZE_EDGE_TOP) {
                                        type = RESIZE_TYPE::TOP;
                                    } else if (e == XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM) {
                                        type = RESIZE_TYPE::BOTTOM;
                                    } else if (e == XDG_TOPLEVEL_RESIZE_EDGE_LEFT) {
                                        type = RESIZE_TYPE::LEFT;
                                    } else if (e == XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT) {
                                        type = RESIZE_TYPE::TOP_LEFT;
                                    } else if (e == XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT) {
                                        type = RESIZE_TYPE::BOTTOM_LEFT;
                                    } else if (e == XDG_TOPLEVEL_RESIZE_EDGE_RIGHT) {
                                        type = RESIZE_TYPE::RIGHT;
                                    } else if (e == XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT) {
                                        type = RESIZE_TYPE::TOP_RIGHT;
                                    } else if (e == XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT) {
                                        type = RESIZE_TYPE::BOTTOM_RIGHT;
                                    }
                                    int id = 0;
                                    for (auto hw : hyprwindows) 
                                        if (hw->w == w)
                                            id = hw->id;
                                    hypriso->on_resize_start_requested(id, type);
                                }
                            }
                        }
                    }
                });
            }
        }
   }
    
    // Validate that we care about the window
    //if (w->m_X11DoesntWantBorders)
        //return;

    /*
    for (const auto &s : NProtocols::serverDecorationKDE->m_decos) {
        if (w->m_xdgSurface && s->m_surf == w->m_xdgSurface) {
            notify(std::to_string((int) s->m_mostRecentlyRequested));
            if (s->m_mostRecentlyRequested == ORG_KDE_KWIN_SERVER_DECORATION_MODE_CLIENT) {
               return;
            }
        }
    }

    for (const auto &[a, b] : NProtocols::xdgDecoration->m_decorations) {
        if (w->m_xdgSurface && w->m_xdgSurface->m_toplevel && w->m_xdgSurface->m_toplevel->m_resource && b->m_resource == w->m_xdgSurface->m_toplevel->m_resource) {
            if (b->mostRecentlyRequested == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE) {
                return;
            }
        }
    }
    */

    // Or csd client side requested
    
    auto hw = new HyprWindow;
    hw->id = unique_id++;
    hw->w = w;
    hyprwindows.push_back(hw);
    hypriso->on_window_open(hw->id);
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
    for (auto mons : hyprmonitors) {
        if (mons->m == m)
            return;
    }
    auto hm = new HyprMonitor;
    hm->id = unique_id++;
    hm->m = m;
    hyprmonitors.push_back(hm);
    hypriso->on_monitor_open(hm->id);
    notify("monitor open");
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

inline CFunctionHook* g_pOnSurfacePassDraw = nullptr;
typedef void (*origSurfacePassDraw)(CSurfacePassElement *, const CRegion& damage);
void hook_onSurfacePassDraw(void* thisptr, const CRegion& damage) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    auto  spe = (CSurfacePassElement *) thisptr;
    auto window = spe->m_data.pWindow;
    //notify("alo");
    int cornermask = 0;
    for (auto hw: hyprwindows)
        if (hw->w == window)
            cornermask = hw->cornermask;
    set_rounding(cornermask); // only top rounding
    (*(origSurfacePassDraw)g_pOnSurfacePassDraw->m_original)(spe, damage);
    set_rounding(0);
}

void fix_window_corner_rendering() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    //return;
    static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "draw");
    // TODO: check if m.address is same as set_rounding even though signature is SurfacePassElement
    for (auto m : METHODS) {
        if (m.signature.find("SurfacePassElement") != std::string::npos) {
            //notify(m.demangled);
            g_pOnSurfacePassDraw = HyprlandAPI::createFunctionHook(globals->api, m.address, (void*)&hook_onSurfacePassDraw);
            g_pOnSurfacePassDraw->hook();
            return;
        }
    }
}


inline CFunctionHook* g_pOnReadProp = nullptr;
typedef void (*origOnReadProp)(void*, SP<CXWaylandSurface> XSURF, uint32_t atom, xcb_get_property_reply_t* reply);
    //void CXWM::readProp(SP<CXWaylandSurface> XSURF, uint32_t atom, xcb_get_property_reply_t* reply) {
void hook_OnReadProp(void* thisptr, SP<CXWaylandSurface> XSURF, uint32_t atom, xcb_get_property_reply_t* reply) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    (*(origOnReadProp)g_pOnReadProp->m_original)(thisptr, XSURF, atom, reply);

    const auto* value    = sc<const char*>(xcb_get_property_value(reply));

    auto handleMotifs = [&]() {
        // Motif hints are 5 longs: flags, functions, decorations, input_mode, status
        const uint32_t* hints = rc<const uint32_t*>(value);

        std::vector<uint32_t> m_motifs;
        m_motifs.assign(hints, hints + std::min<size_t>(reply->value_len, 5));

        for (const auto &w : g_pCompositor->m_windows) {
            if (w->m_xwaylandSurface == XSURF) {
                w->m_X11DoesntWantBorders = false;
                g_pXWaylandManager->checkBorders(w);

                const uint32_t flags       = m_motifs[0];
                const uint32_t decorations = m_motifs.size() > 2 ? m_motifs[2] : 1;
                const uint32_t MWM_HINTS_DECORATIONS = (1L << 1);

                if ((flags & MWM_HINTS_DECORATIONS) && decorations == 0) {
                    w->m_X11DoesntWantBorders = true;
                }

                // has decora
                bool has_decorations = false;
                for (auto& wd : w->m_windowDecorations)
                    if (wd->getDisplayName() == "MylarBar")
                        has_decorations = true;

                if (w->m_X11DoesntWantBorders && has_decorations) {
                    for (auto it = w.get()->m_windowDecorations.rbegin(); it != w.get()->m_windowDecorations.rend(); ++it) {
                        auto bar = (IHyprWindowDecoration *) it->get();
                        if (bar->getDisplayName() == "MylarBar" || bar->getDisplayName() == "MylarResize") {
                            HyprlandAPI::removeWindowDecoration(globals->api, bar);
                            for (auto hw : hyprwindows) {
                                if (hw->w == w && hypriso->on_window_closed) {
                                    hypriso->on_window_closed(hw->id);
                                }
                            }
                        }
                    }
                } else if (!w->m_X11DoesntWantBorders && !has_decorations) {
                    // add
                    for (auto hw : hyprwindows) {
                        if (hw->w == w && hypriso->on_window_open) {
                           hypriso->on_window_open(hw->id);
                        }
                    }
                }
            }
        }
    };
    if (atom == HYPRATOMS["_MOTIF_WM_HINTS"])
        handleMotifs();
}

static wl_event_source *source = nullptr;

void remove_request_listeners() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    for (auto& w : g_pCompositor->m_windows) {
        if (auto surface = w->m_xdgSurface) {
            if (auto toplevel = surface->m_toplevel.lock()) {
                auto resource = toplevel->m_resource;
                if (resource) {
                    resource->setMove(nullptr);
                    resource->setResize(nullptr);
                }
            }
        }
    }
}

inline CFunctionHook* g_pOnRMS = nullptr;
typedef Vector2D (*origOnRMS)(void*);
Vector2D hook_OnRMS(void* thisptr) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    //recheck_csd_for_all_wayland_windows();
    return Vector2D(10, 10);
    //return (*(origOnRMS)g_pOnRMS->m_original)(thisptr);
}

void overwrite_min() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "requestedMinSize");
        g_pOnRMS = HyprlandAPI::createFunctionHook(globals->api, METHODS[0].address, (void*)&hook_OnRMS);
        g_pOnRMS->hook();
    }
    
}

void recheck_csd_for_all_wayland_windows() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (source)    
        return;
    source = wl_event_loop_add_timer(g_pCompositor->m_wlEventLoop, [](void *) {
        source = nullptr;

        for (auto w : g_pCompositor->m_windows) {
            if (w->m_isX11)
                continue;
            
            bool remove_csd = false;
            for (const auto &s : NProtocols::serverDecorationKDE->m_decos) {
                if (w->m_xdgSurface && s->m_surf == w->m_xdgSurface) {
                    //notify(std::to_string((int) s->m_mostRecentlyRequested));
                    if (s->m_mostRecentlyRequested == ORG_KDE_KWIN_SERVER_DECORATION_MODE_CLIENT) {
                        remove_csd = true;
                    }
                }
            }

            for (const auto &[a, b] : NProtocols::xdgDecoration->m_decorations) {
                if (w->m_xdgSurface && w->m_xdgSurface->m_toplevel && w->m_xdgSurface->m_toplevel->m_resource && b->m_resource == w->m_xdgSurface->m_toplevel->m_resource) {
                    if (b->mostRecentlyRequested == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE) {
                        remove_csd = true;
                    }
                }
            }


            bool has_csd = false;
            for (auto& wd : w->m_windowDecorations)
                if (wd->getDisplayName() == "MylarBar")
                    has_csd = true;
            if (has_csd && remove_csd) {
                for (auto hw : hyprwindows) {
                    if (hw->w == w) {
                        hypriso->on_window_closed(hw->id);
                    }
                }
            } else if (!has_csd && !remove_csd) {
                for (auto hw : hyprwindows) {
                    if (hw->w == w) {
                        hypriso->on_window_open(hw->id);
                    }
                }
            }
        }
        
        return 0; // 0 means stop timer, >0 means retry in that amount of ms
    }, nullptr);
    wl_event_source_timer_update(source, 10); // 10ms
}

inline CFunctionHook* g_pOnKDECSD = nullptr;
typedef uint32_t (*origOnKDECSD)(void*);
uint32_t hook_OnKDECSD(void* thisptr) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    recheck_csd_for_all_wayland_windows();
    return (*(origOnKDECSD)g_pOnKDECSD->m_original)(thisptr);
}

inline CFunctionHook* g_pOnXDGCSD = nullptr;
typedef zxdgToplevelDecorationV1Mode (*origOnXDGCSD)(void*);
zxdgToplevelDecorationV1Mode hook_OnXDGCSD(void* thisptr) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    recheck_csd_for_all_wayland_windows();
    return (*(origOnXDGCSD)g_pOnXDGCSD->m_original)(thisptr);
}

void detect_csd_request_change() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // hook xdg and kde csd request mode, then set timeout for 25 ms, 5 times which checks and updates csd for current windows based on most recent requests
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "kdeDefaultModeCSD");
        g_pOnKDECSD = HyprlandAPI::createFunctionHook(globals->api, METHODS[0].address, (void*)&hook_OnKDECSD);
        g_pOnKDECSD->hook();
    }
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "xdgDefaultModeCSD");
        g_pOnXDGCSD = HyprlandAPI::createFunctionHook(globals->api, METHODS[0].address, (void*)&hook_OnXDGCSD);
        g_pOnXDGCSD->hook();
    }

    // hook props change xwayland function, parse motifs, set or remove decorations as needed
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "readProp");
        g_pOnReadProp = HyprlandAPI::createFunctionHook(globals->api, METHODS[0].address, (void*)&hook_OnReadProp);
        g_pOnReadProp->hook();
    }

}

void detect_move_resize_requests() {
   /*if (auto surface = PWINDOW->m_xdgSurface) {
        if (auto toplevel = surface->m_toplevel.lock()) {
            auto resource = *toplevel.*result<r_data_tag>::ptr;
            if (resource) {
            //HyprlandAPI::createFunctionHook(stable_hypr::APIHANDLE, resource->requests.move, dest);
            resource->setMove(on_move_requsted);
            resource->setResize(on_resize_requested);
            }
        }
   }*/
  
}

inline CFunctionHook* g_pRenderWindowHook = nullptr;
typedef void (*origRenderWindowFunc)(void*, PHLWINDOW pWindow, PHLMONITOR pMonitor, const Time::steady_tp& time, bool decorate, eRenderPassMode mode, bool ignorePosition, bool standalone);
void hook_RenderWindow(void* thisptr, PHLWINDOW pWindow, PHLMONITOR pMonitor, const Time::steady_tp& time, bool decorate, eRenderPassMode mode, bool ignorePosition, bool standalone) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Hyprlang::INT* rounding_amount = nullptr;
    int initial_value = 0;

    Hyprlang::INT* border_size = nullptr;
    int initial_border_size = 0;
    
    for (auto hw : hyprwindows) {
        if (hw->w == pWindow && hw->no_rounding) {
            Hyprlang::CConfigValue* val = g_pConfigManager->getHyprlangConfigValuePtr("decoration:rounding");
            rounding_amount = (Hyprlang::INT*)val->dataPtr();
            initial_value = *rounding_amount;
            *rounding_amount = 0;

            Hyprlang::CConfigValue* val2 = g_pConfigManager->getHyprlangConfigValuePtr("general:border_size");
            border_size = (Hyprlang::INT*)val2->dataPtr();
            initial_border_size = *border_size;
            *border_size = 0;
        }
    }

    (*(origRenderWindowFunc)g_pRenderWindowHook->m_original)(thisptr, pWindow, pMonitor, time, decorate, mode, ignorePosition, standalone);
    if (rounding_amount) {
        *rounding_amount = initial_value;
        *border_size = initial_border_size;
    }
}

inline CFunctionHook* g_pWindowRoundingHook = nullptr;
typedef float (*origWindowRoundingFunc)(CWindow *);
float hook_WindowRounding(void* thisptr) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    float result = (*(origWindowRoundingFunc)g_pWindowRoundingHook->m_original)((CWindow *)thisptr);
    return result;
}

inline CFunctionHook* g_pWindowRoundingPowerHook = nullptr;
typedef float (*origWindowRoundingPowerFunc)(CWindow *);
float hook_WindowRoundingPower(void* thisptr) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    float result = (*(origWindowRoundingPowerFunc)g_pWindowRoundingPowerHook->m_original)((CWindow *)thisptr);
    return result;
}

void hook_render_functions() {
    //return;
    /*
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "rounding");
        for (auto m : METHODS) {
            if (m.signature.find("CWindow") != std::string::npos) {
                g_pWindowRoundingHook       = HyprlandAPI::createFunctionHook(globals->api, m.address, (void*)&hook_WindowRounding);
                g_pWindowRoundingHook->hook();
                break;
            }
        }
    }
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "roundingPower");
        for (auto m : METHODS) {
            if (m.signature.find("CWindow") != std::string::npos) {
                g_pWindowRoundingPowerHook       = HyprlandAPI::createFunctionHook(globals->api, m.address, (void*)&hook_WindowRoundingPower);
                g_pWindowRoundingPowerHook->hook();
                break;
            }
        }
    }
    */
 
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "renderWindow");
        g_pRenderWindowHook       = HyprlandAPI::createFunctionHook(globals->api, METHODS[0].address, (void*)&hook_RenderWindow);
        g_pRenderWindowHook->hook();
        pRenderWindow = METHODS[0].address;
    }
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "renderLayer");
        pRenderLayer = METHODS[0].address;
    }
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "renderWorkspace");
        pRenderWorkspace = METHODS[0].address;
    }
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "renderMonitor");
        pRenderMonitor = METHODS[0].address;
    }
    
}

inline CFunctionHook* g_pOnCircleNextHook = nullptr;
typedef bool (*origOnCircleNextFunc)(void*, const IPointer::SButtonEvent&);
SDispatchResult hook_onCircleNext(void* thisptr, std::string arg) {
    // we don't call the original function because we want to remove it
    return {};
}

void disable_default_alt_tab_behaviour() {
    return;
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "circleNext");
        g_pOnCircleNextHook       = HyprlandAPI::createFunctionHook(globals->api, METHODS[0].address, (void*)&hook_onCircleNext);
        g_pOnCircleNextHook->hook();
    }
}

void set_window_corner_mask(int id, int cornermask) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    notify("deprecated(set_window_corner_mask): use set_corner_rendering_mask_for_window");
    hypriso->set_corner_rendering_mask_for_window(id, cornermask);
}

std::string HyprIso::class_name(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == id) {
            if (auto w = hyprwindow->w.get()) {
                return w->m_class;
            }
        }
    }
 
    return "";
}

float HyprIso::get_rounding(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw: hyprwindows)
        if (hw->id == id)
            return hw->w->rounding();
    return 0;
}

void HyprIso::floatit(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    //return;
    for (auto hw: hyprwindows)
        if (hw->id == id)
            float_target(hw->w);
}

float HyprIso::get_varfloat(std::string target, float default_float) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    //return default_float;
    
    auto confval = HyprlandAPI::getConfigValue(globals->api, target);
    if (!confval)
        return default_float;

    auto VAR = (Hyprlang::FLOAT* const*)confval->getDataStaticPtr();
    return **VAR; 
}

RGBA HyprIso::get_varcolor(std::string target, RGBA default_color) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    //return default_color;
    
    auto confval = HyprlandAPI::getConfigValue(globals->api, target);
    if (!confval)
        return default_color;

    auto VAR = (Hyprlang::INT* const*)confval->getDataStaticPtr();
    auto color = CHyprColor(**VAR);
    return RGBA(color.r, color.g, color.b, color.a);         
}

void HyprIso::create_config_variables() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_button_bg_hovered_color", Hyprlang::INT{*configStringToInt("rgba(202020ff)")});
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_button_bg_pressed_color", Hyprlang::INT{*configStringToInt("rgba(111111ff)")});
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_closed_button_bg_hovered_color", Hyprlang::INT{*configStringToInt("rgba(dd1111ff)")});
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_closed_button_bg_pressed_color", Hyprlang::INT{*configStringToInt("rgba(880000ff)")});
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_closed_button_icon_color_hovered_pressed", Hyprlang::INT{*configStringToInt("rgba(ffffffff)")});

    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_focused_color", Hyprlang::INT{*configStringToInt("rgba(000000ff)")});
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_unfocused_color", Hyprlang::INT{*configStringToInt("rgba(222222ff)")});
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_focused_text_color", Hyprlang::INT{*configStringToInt("rgba(ffffffff)")});
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_unfocused_text_color", Hyprlang::INT{*configStringToInt("rgba(999999ff)")});
    
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:thumb_to_position_time", Hyprlang::FLOAT{355});
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:snap_helper_fade_in", Hyprlang::FLOAT{400});

    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_button_ratio", Hyprlang::FLOAT{1.4375});
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_text_h", Hyprlang::FLOAT{15});
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_icon_h", Hyprlang::FLOAT{21});
    HyprlandAPI::addConfigValue(globals->api, "plugin:mylardesktop:titlebar_button_icon_h", Hyprlang::FLOAT{13});
}

void HyprIso::create_callbacks() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto m : g_pCompositor->m_monitors) {
        on_open_monitor(m);
    }

    for (auto w : g_pCompositor->m_windows) {
        on_open_window(w);
    }
 
    static auto openWindow  = HyprlandAPI::registerCallbackDynamic(globals->api, "openWindow", [](void* self, SCallbackInfo& info, std::any data) {
        if (hypriso->on_window_open) {
            auto w = std::any_cast<PHLWINDOW>(data); // todo getorcreate ref on our side
            on_open_window(w);
        }
    });
    static auto closeWindow1 = HyprlandAPI::registerCallbackDynamic(globals->api, "closeWindow", [](void* self, SCallbackInfo& info, std::any data) {
        if (hypriso->on_window_closed) {
            auto w = std::any_cast<PHLWINDOW>(data); // todo getorcreate ref on our side
            on_close_window(w);
        }
    });

    static auto openLayer  = HyprlandAPI::registerCallbackDynamic(globals->api, "openLayer", [](void* self, SCallbackInfo& info, std::any data) {
        //if (hypriso->on_window_open) {
            //auto w = std::any_cast<PHLLS>(data); // todo getorcreate ref on our side
            //on_open_window(w);
        //}
        if (hypriso->on_layer_change) {
            hypriso->on_layer_change();
        }
    });
    static auto closeLayer = HyprlandAPI::registerCallbackDynamic(globals->api, "closeLayer", [](void* self, SCallbackInfo& info, std::any data) {
        //if (hypriso->on_window_closed) {
            //auto w = std::any_cast<PHLLS>(data); // todo getorcreate ref on our side
            //w->m_realPosition
            //on_close_window(w);
        //}
        if (hypriso->on_layer_change) {
            hypriso->on_layer_change();
        }
    });
    
    static auto render = HyprlandAPI::registerCallbackDynamic(globals->api, "render", [](void* self, SCallbackInfo& info, std::any data) {
        //return;
        auto stage = std::any_cast<eRenderStage>(data);
        if (stage == eRenderStage::RENDER_PRE) {
            #ifdef TRACY_ENABLE
                FrameMarkStart("Render");
            #endif        
        }
        if (hypriso->on_render) {
            for (auto m : hyprmonitors) {
                if (m->m == g_pHyprOpenGL->m_renderData.pMonitor) {
                    hypriso->on_render(m->id, (int)stage);
                }
            }
        }
        if (stage == eRenderStage::RENDER_LAST_MOMENT) {
            #ifdef TRACY_ENABLE
                FrameMarkEnd("Render");
            #endif
        }
    });
    
    static auto mouseMove = HyprlandAPI::registerCallbackDynamic(globals->api, "mouseMove", [](void* self, SCallbackInfo& info, std::any data) {
        //return;
        auto consume = false;
        if (hypriso->on_mouse_move) {
            auto mouse = g_pInputManager->getMouseCoordsInternal();
            auto m     = g_pCompositor->getMonitorFromCursor();
            consume    = hypriso->on_mouse_move(0, mouse.x * m->m_scale, mouse.y * m->m_scale);
        }
        info.cancelled = consume;
    });

    static auto mouseButton = HyprlandAPI::registerCallbackDynamic(globals->api, "mouseButton", [](void* self, SCallbackInfo& info, std::any data) {
        auto e       = std::any_cast<IPointer::SButtonEvent>(data);
        auto consume = false;
        if (hypriso->on_mouse_press) {
            auto mouse = g_pInputManager->getMouseCoordsInternal();
            auto s     = g_pCompositor->getMonitorFromCursor()->m_scale;
            consume    = hypriso->on_mouse_press(e.mouse, e.button, e.state, mouse.x * s, mouse.y * s);
        }
        info.cancelled = consume;
    });

    static auto mouseAxis = HyprlandAPI::registerCallbackDynamic(globals->api, "mouseAxis", [](void* self, SCallbackInfo& info, std::any data) {
        bool consume = false;
        auto p       = std::any_cast<std::unordered_map<std::string, std::any>>(data);
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

    static auto keyPress = HyprlandAPI::registerCallbackDynamic(globals->api, "keyPress", [](void* self, SCallbackInfo& info, std::any data) {
        auto consume = false;
        if (hypriso->on_key_press) {
            auto p = std::any_cast<std::unordered_map<std::string, std::any>>(data);
            for (std::pair<const std::string, std::any> pair : p) {
                if (pair.first == "event") {
                    auto skeyevent = std::any_cast<IKeyboard::SKeyEvent>(pair.second);
                    consume        = hypriso->on_key_press(0, skeyevent.keycode, skeyevent.state, skeyevent.updateMods);
                } else if (pair.first == "keyboard") {
                    auto ikeyboard = std::any_cast<Hyprutils::Memory::CSharedPointer<IKeyboard>>(pair.second);
                }
            }
        }
        info.cancelled = consume;
    });
    /*
    static auto monitorAdded = HyprlandAPI::registerCallbackDynamic(globals->api, "monitorAdded", [](void* self, SCallbackInfo& info, std::any data) {
        if (hypriso->on_monitor_open) {
            auto m = std::any_cast<PHLMONITOR>(data); // todo getorcreate ref on our side
            on_open_monitor(m);
        }
    });
    
    static auto monitorRemoved = HyprlandAPI::registerCallbackDynamic(globals->api, "monitorRemoved", [](void* self, SCallbackInfo& info, std::any data) {
        if (hypriso->on_monitor_closed) {
            auto m = std::any_cast<PHLMONITOR>(data); // todo getorcreate ref on our side
            on_close_monitor(m);
        }
    });
    */
    

    static auto configReloaded = HyprlandAPI::registerCallbackDynamic(globals->api, "configReloaded", [](void* self, SCallbackInfo& info, std::any data) {
        if (hypriso->on_config_reload) {
            hypriso->on_config_reload();
            
            Hyprlang::CConfigValue* val = g_pConfigManager->getHyprlangConfigValuePtr("input:follow_mouse");
            auto f = (Hyprlang::INT*)val->dataPtr();
            //initial_value = *f;
            *f = 2;
        }
    });

    for (auto e : g_pCompositor->m_workspaces) {
        auto hs = new HyprWorkspaces;
        hs->w = e;
        hs->id = unique_id++;
        hs->buffer = new CFramebuffer;
        hyprspaces.push_back(hs);
    }

    static auto createWorkspace = HyprlandAPI::registerCallbackDynamic(globals->api, "createWorkspace", [](void* self, SCallbackInfo& info, std::any data) {
        auto s = std::any_cast<CWorkspace*>(data)->m_self.lock();
        auto hs = new HyprWorkspaces;
        hs->w = s;
        hs->id = unique_id++;
        hs->buffer = new CFramebuffer;
        hyprspaces.push_back(hs);
    });

    static auto destroyWorkspace = HyprlandAPI::registerCallbackDynamic(globals->api, "destroyWorkspace", [](void* self, SCallbackInfo& info, std::any data) {
        auto s = std::any_cast<CWorkspace*>(data);
        for (int i = hyprspaces.size() - 1; i >= 0; i--) {
            auto hs = hyprspaces[i];
            bool remove = false;
            if (!hs->w.lock()) {
                remove = true;
            } else if (hs->w.get() == s) {
                remove = true;
            }
            if (remove) {
                delete hs->buffer;
                hyprspaces.erase(hyprspaces.begin() + i);
            }
        }
    });

    static auto activeWindow = HyprlandAPI::registerCallbackDynamic(globals->api, "activeWindow", [](void* self, SCallbackInfo& info, std::any data) {
        auto p = std::any_cast<PHLWINDOW>(data);
        if (hypriso->on_activated) {
            for (auto h : hyprwindows) {
                if (h->w == p) {
                    hypriso->on_activated(h->id);
                }
            }
        }
    });
}

inline CFunctionHook* g_pOnArrangeMonitors = nullptr;
typedef void (*origArrangeMonitors)(CCompositor *);
void hook_onArrangeMonitors(void* thisptr) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto spe = (CCompositor *) thisptr;
    (*(origArrangeMonitors) g_pOnArrangeMonitors->m_original)(spe);
    //notify("Some change in monitors");
    // Go through all and check any that no longer exist or that need to exist

    return;
    for (const auto &m : g_pCompositor->m_monitors) {
        bool already_in = false;
        for (auto hm : hyprmonitors) {
            if (hm->m == m) {
                already_in = true;
            }
        }

        if (!already_in) {
            on_open_monitor(m);
        }
    }
    for (int i = hyprmonitors.size() - 1; i >= 0; i--) {
        auto hm = hyprmonitors[i];
        bool in = false;
        for (const auto &m : g_pCompositor->m_monitors) {
            if (hm->m == m) {
                in = true;
            }
        }

        if (!in) {
            on_close_monitor(hm->m);
        }
    }
}

void hook_monitor_arrange() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "arrangeMonitors");
    for (auto m : METHODS) {
        if (m.signature.find("CCompositor") != std::string::npos) {
            g_pOnArrangeMonitors = HyprlandAPI::createFunctionHook(globals->api, m.address, (void*)&hook_onArrangeMonitors);
            g_pOnArrangeMonitors->hook();
            return;
        }
    }
}

inline CFunctionHook* g_pOnArrangeLayers = nullptr;
typedef void (*origArrangeLayers)(CHyprRenderer *, const MONITORID& monitor);
void hook_onArrangeLayers(void* thisptr, const MONITORID& monitor) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto spe = (CHyprRenderer *) thisptr;
    (*(origArrangeLayers)g_pOnArrangeLayers->m_original)(spe, monitor);
    //notify("dock added");
}

void hook_dock_change() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    //g_pHyprRenderer->arrangeLayersForMonitor(m_id);
    static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "arrangeLayersForMonitor");
    // TODO: check if m.address is same as set_rounding even though signature is SurfacePassElement
    for (auto m : METHODS) {
        if (m.signature.find("CHyprRenderer") != std::string::npos) {
            //notify(m.demangled);
            //notify(m.demangled);
            g_pOnArrangeLayers = HyprlandAPI::createFunctionHook(globals->api, m.address, (void*)&hook_onArrangeLayers);
            g_pOnArrangeLayers->hook();
            return;
        }
    }
}

void HyprIso::create_hooks() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    //return;
    fix_window_corner_rendering();
    disable_default_alt_tab_behaviour();
    detect_csd_request_change();
    detect_move_resize_requests();    
    overwrite_min();
    hook_render_functions();
    interleave_floating_and_tiled_windows();
    hook_dock_change();
    hook_monitor_arrange();
}

bool HyprIso::alt_tabbable(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto h : hyprwindows) {
        if (h->id == id) {
            bool found = false;
            for (const auto &w : g_pCompositor->m_windows) {
                if (w == h->w && w->m_isMapped) {
                    found = true;
                }
            }
            return found;
        }
    }

    return false; 
}


void HyprIso::end() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    g_pHyprRenderer->m_renderPass.removeAllOfType("CRectPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CBorderPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CTexPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CAnyPassElement");
    remove_request_listeners();
}

CBox tocbox(Bounds b) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    return {b.x, b.y, b.w, b.h};
}

Bounds tobounds(CBox box) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    return {box.x, box.y, box.w, box.h};
}

void rect(Bounds box, RGBA color, int cornermask, float round, float roundingPower, bool blur, float blurA) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    //return;
    if (box.h <= 0 || box.w <= 0)
        return;
    bool clip = hypriso->clip;
    Bounds clipbox = hypriso->clipbox;
    if (clip && !tocbox(clipbox).overlaps(tocbox(box))) {
        return; 
    }
    if (cornermask == 16)
        round = 0;
    AnyPass::AnyData anydata([box, color, cornermask, round, roundingPower, blur, blurA, clip, clipbox](AnyPass* pass) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
        CHyprOpenGLImpl::SRectRenderData rectdata;
        auto region = new CRegion(tocbox(box));
        rectdata.damage        = region;
        rectdata.blur          = blur;
        rectdata.blurA         = blurA;
        rectdata.round         = std::round(round);
        rectdata.roundingPower = roundingPower;
        rectdata.xray = false;

        if (clip)
            g_pHyprOpenGL->m_renderData.clipBox = tocbox(clipbox);
        
        // TODO: who is responsible for cleaning up this damage?
        set_rounding(cornermask); // only top side
        g_pHyprOpenGL->renderRect(tocbox(box), CHyprColor(color.r, color.g, color.b, color.a), rectdata);
        set_rounding(0);
        if (clip)
            g_pHyprOpenGL->m_renderData.clipBox = CBox();
    });
    anydata.box = tocbox(box);
    g_pHyprRenderer->m_renderPass.add(makeUnique<AnyPass>(std::move(anydata)));
}

void border(Bounds box, RGBA color, float size, int cornermask, float round, float roundingPower, bool blur, float blurA) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    if (box.h <= 0 || box.w <= 0)
        return;
    CBorderPassElement::SBorderData rectdata;
    rectdata.grad1         = CHyprColor(color.r, color.g, color.b, color.a);
    rectdata.grad2         = CHyprColor(color.r, color.g, color.b, color.a);
    rectdata.box           = tocbox(box);
    rectdata.round         = round;
    rectdata.outerRound    = round;
    rectdata.borderSize    = size;
    rectdata.roundingPower = roundingPower;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(rectdata));
}

void shadow(Bounds box, RGBA color, float rounding, float roundingPower, float size) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    AnyPass::AnyData anydata([box, color, rounding, roundingPower, size](AnyPass* pass) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
        if (g_pCompositor->m_lastWindow.get()) {
            auto current = g_pHyprOpenGL->m_renderData.currentWindow;
            g_pHyprOpenGL->m_renderData.currentWindow = g_pCompositor->m_lastWindow;
            g_pHyprOpenGL->renderRoundedShadow(tocbox(box), rounding, roundingPower, size, CHyprColor(color.r, color.g, color.b, color.a), 1.0);
            g_pHyprOpenGL->m_renderData.currentWindow = current;
        }
    });
    g_pHyprRenderer->m_renderPass.add(makeUnique<AnyPass>(std::move(anydata)));
}


struct MylarBar : public IHyprWindowDecoration {
    PHLWINDOW m_window;
    int m_size;
    
    MylarBar(PHLWINDOW w, int size) : IHyprWindowDecoration(w) {
        m_window = w;
        m_size = size;
    }
    
    SDecorationPositioningInfo getPositioningInfo() { 
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
        
        SDecorationPositioningInfo info;
        info.policy         = DECORATION_POSITION_STICKY;
        info.edges          = DECORATION_EDGE_TOP;
        info.priority       = 10005;
        info.reserved       = true;
        info.desiredExtents = {{0, m_size}, {0, 0}};
        return info;
    }
    void onPositioningReply(const SDecorationPositioningReply& reply) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
        
        //g_pHyprRenderer->damageMonitor(m_window->m_monitor.lock());
        //draw(m_window->m_monitor.lock(), 1.0);
    }
    void draw(PHLMONITOR monitor, float const& a) { 
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
        
        if (!hypriso->on_draw_decos)
           return; 
        for (auto m : hyprmonitors) {
            if (m->m == monitor) {
                for (auto w : hyprwindows) {
                   if (w->w == m_window)  {
                       hypriso->on_draw_decos(getDisplayName(), m->id, w->id, a);
                       return;
                   }
                }
            }
        }
    }
    eDecorationType getDecorationType() { return eDecorationType::DECORATION_GROUPBAR; }
    void updateWindow(PHLWINDOW) { 
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
        
        //g_pHyprRenderer->damageMonitor(m_window->m_monitor.lock());
        //draw(m_window->m_monitor.lock(), 1.0);
    }
    void damageEntire() { 
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
        
        //g_pHyprRenderer->damageMonitor(m_window->m_monitor.lock());
        //hypriso->damage_entire(m_window->m_monitorMovedFrom);
        //draw(m_window->m_monitor.lock(), 1.0);
    } 
    bool onInputOnDeco(const eInputType, const Vector2D&, std::any = {}) { return false; }
    eDecorationLayer getDecorationLayer() { return eDecorationLayer::DECORATION_LAYER_BOTTOM; }
    uint64_t getDecorationFlags() { return eDecorationFlags::DECORATION_PART_OF_MAIN_WINDOW; }
    std::string getDisplayName() { return "MylarBar"; }
};

bool HyprIso::wants_titlebar(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
   for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == id) {
            if (hyprwindow->w->m_X11DoesntWantBorders) {
                return false;
            }
        }
   }
      
    return true;
}

void HyprIso::reserve_titlebar(int id, int size) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == id) {
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
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    for (auto m : g_pCompositor->m_monitors) {
        g_pHyprRenderer->damageMonitor(m);
        g_pCompositor->scheduleFrameForMonitor(m);
    }
}

void request_refresh_only() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    for (auto m : g_pCompositor->m_monitors) {
        g_pCompositor->scheduleFrameForMonitor(m);
    }
}

Bounds bounds_full(ThinClient *w) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    notify("deprecated(bounds_full): use bounds_full_client");
    return bounds_full_client(w->id);
}

Bounds bounds(ThinClient *w) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    notify("deprecated(bounds): use bounds_client");
    return bounds_client(w->id);
}

Bounds real_bounds(ThinClient *w) {
    notify("deprecated(real_bounds): use real_bounds_client");
    return real_bounds_client(w->id);
}

std::string class_name(ThinClient *w) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == w->id) {
            if (auto w = hyprwindow->w.get()) {
                return w->m_class;
            }
        }
    }
 
   return ""; 
}

std::string HyprIso::title_name(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == id) {
            if (auto w = hyprwindow->w.get()) {
                return w->m_title;
            }
        }
    }
 
    return "";
}

std::string title_name(ThinClient *w) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == w->id) {
            if (auto w = hyprwindow->w.get()) {
                return w->m_title;
            }
        }
    }
 
    return "";
}

Bounds bounds(ThinMonitor *m) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    notify("deprecated(bounds): use bounds_monitor");
    return bounds_monitor(m->id);
}

Bounds bounds_reserved(ThinMonitor *m) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    notify("deprecated(bounds_reserved): use bounds_reserved_monitor");
    return bounds_reserved_monitor(m->id);
}

void notify(std::string text) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    HyprlandAPI::addNotification(globals->api, text, {1, 1, 1, 1}, 4000);
}

int current_rendering_monitor() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    if (auto m = g_pHyprOpenGL->m_renderData.pMonitor.lock()) {
        for (auto hyprmonitor : hyprmonitors) {
            if (hyprmonitor->m == m) {
                return hyprmonitor->id;
            }
        } 
    }
    return -1;
}

int current_rendering_window() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    if (auto c = g_pHyprOpenGL->m_renderData.currentWindow.lock()) {
        for (auto hyprwindow : hyprwindows) {
            if (hyprwindow->w == c) {
                return hyprwindow->id; 
            }
        }         
    }
    return -1;
}

float scale(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hyprmonitor : hyprmonitors) {
        if (hyprmonitor->id == id) {
            return hyprmonitor->m->m_scale;
        }
    }
    return 1.0;
}

std::vector<int> HyprIso::get_workspaces(int monitor) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    std::vector<int> vec;
    for (auto hm : hyprmonitors) {
        if (hm->id == monitor) {
            for (auto e : g_pCompositor->m_workspaces) {
                if (hm->m == e->m_monitor) {
                    vec.push_back(e->m_id);
                }
            }
        }
    }
    return vec;
}

int HyprIso::get_active_workspace(int monitor) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hm : hyprmonitors) {
        if (hm->id == monitor) {
            if (hm->m->m_activeWorkspace.get()) {
                return hm->m->m_activeWorkspace->m_id;
            }
        }
    }
    return -1;
}

int HyprIso::get_workspace(int client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == client) {
            if (hw->w->m_workspace.get()) {
                return hw->w->m_workspace->m_id;
            }
        }
    }
    return -1;
}


std::vector<int> get_window_stacking_order() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    std::vector<int> vec;
    for (auto w : g_pCompositor->m_windows) {
        for (auto hyprwindow : hyprwindows) {
            if (hyprwindow->w == w) {
                vec.push_back(hyprwindow->id);
            }
        }        
    }
    
    return vec;
}

void HyprIso::move(int id, int x, int y) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto c : hyprwindows) {
        if (c->id == id) {
            c->w->m_realPosition->setValueAndWarp({x, y});
        }
    }
}

void HyprIso::move_resize(int id, int x, int y, int w, int h, bool instant) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto c : hyprwindows) {
        if (c->id == id) {
            if (instant) {
                c->w->m_realPosition->setValueAndWarp({x, y});
                c->w->m_realSize->setValueAndWarp({w, h});
            } else {
                *c->w->m_realPosition = {x, y};
                *c->w->m_realSize = {w, h};
            }
            c->w->sendWindowSize(true);
            c->w->updateWindowDecos();
        }
    }
}
void HyprIso::move_resize(int id, Bounds b, bool instant) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    move_resize(id, b.x, b.y, b.w, b.h, instant);
}


bool paint_svg_to_surface(cairo_surface_t* surface, std::string path, int target_size) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    GFile* gfile = g_file_new_for_path(path.c_str());
    if (gfile == nullptr)
        return false;
    RsvgHandle* handle = rsvg_handle_new_from_gfile_sync(gfile, RSVG_HANDLE_FLAGS_NONE, NULL, NULL);

    // TODO: is this correct?
    if (handle == nullptr)
        return false;

    auto* temp_context = cairo_create(surface);
    cairo_save(temp_context);
    cairo_set_operator(temp_context, CAIRO_OPERATOR_CLEAR);
    cairo_paint(temp_context);
    cairo_restore(temp_context);

    cairo_save(temp_context);
    const RsvgRectangle viewport{0, 0, (double)target_size, (double)target_size};
    rsvg_handle_render_layer(handle, temp_context, NULL, &viewport, nullptr);
    cairo_restore(temp_context);
    cairo_destroy(temp_context);

    g_object_unref(gfile);
    g_object_unref(handle);

    return true;
}

bool paint_png_to_surface(cairo_surface_t* surface, std::string path, int target_size) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto* png_surface = cairo_image_surface_create_from_png(path.c_str());

    if (cairo_surface_status(png_surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(png_surface);
        return false;
    }

    auto* temp_context = cairo_create(surface);
    int   w            = cairo_image_surface_get_width(png_surface);
    int   h            = cairo_image_surface_get_height(png_surface);

    cairo_save(temp_context);
    cairo_set_operator(temp_context, CAIRO_OPERATOR_CLEAR);
    cairo_paint(temp_context);
    cairo_restore(temp_context);

    cairo_save(temp_context);
    if (target_size != w) {
        double scale = ((double)target_size) / ((double)w);
        cairo_scale(temp_context, scale, scale);
    }
    cairo_set_source_surface(temp_context, png_surface, 0, 0);
    cairo_paint(temp_context);
    cairo_restore(temp_context);
    cairo_destroy(temp_context);
    cairo_surface_destroy(png_surface);

    return true;
}

cairo_surface_t* cairo_image_surface_create_from_xpm(const std::string& path) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Failed to open XPM file: " << path << std::endl;
        return nullptr;
    }

    std::string              line;
    std::vector<std::string> lines;

    // Read lines into a buffer
    while (std::getline(file, line)) {
        size_t start = line.find('"');
        size_t end   = line.rfind('"');
        if (start != std::string::npos && end != std::string::npos && end > start) {
            lines.push_back(line.substr(start + 1, end - start - 1));
        }
    }

    if (lines.empty())
        return nullptr;

    // Parse header
    std::istringstream header(lines[0]);
    int                width, height, num_colors, chars_per_pixel;
    header >> width >> height >> num_colors >> chars_per_pixel;

    // Parse color map
    std::unordered_map<std::string, uint32_t> color_map;
    for (int i = 1; i <= num_colors; ++i) {
        std::string entry     = lines[i];
        std::string key       = entry.substr(0, chars_per_pixel);
        std::string color_str = entry.substr(entry.find("c ") + 2);

        uint32_t    color = 0x00000000; // Default to transparent

        if (color_str == "None") {
            color = 0x00000000;
        } else if (color_str[0] == '#') {
            // Parse hex color
            color_str      = color_str.substr(1); // skip '#'
            unsigned int r = 0, g = 0, b = 0;

            if (color_str.length() == 6) {
                std::istringstream(color_str.substr(0, 2)) >> std::hex >> r;
                std::istringstream(color_str.substr(2, 2)) >> std::hex >> g;
                std::istringstream(color_str.substr(4, 2)) >> std::hex >> b;
            }

            color = (0xFF << 24) | (r << 16) | (g << 8) | b; // ARGB
        }

        color_map[key] = color;
    }

    // Create surface
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    unsigned char*   data    = cairo_image_surface_get_data(surface);
    int              stride  = cairo_image_surface_get_stride(surface);

    // Parse pixels
    for (int y = 0; y < height; ++y) {
        const std::string& row = lines[y + 1 + num_colors];
        for (int x = 0; x < width; ++x) {
            std::string    key   = row.substr(x * chars_per_pixel, chars_per_pixel);
            uint32_t       color = color_map.count(key) ? color_map[key] : 0x00000000;

            unsigned char* pixel = data + y * stride + x * 4;
            pixel[0]             = (color >> 0) & 0xFF;  // B
            pixel[1]             = (color >> 8) & 0xFF;  // G
            pixel[2]             = (color >> 16) & 0xFF; // R
            pixel[3]             = (color >> 24) & 0xFF; // A
        }
    }

    cairo_surface_mark_dirty(surface);
    return surface;
}

bool paint_xpm_to_surface(cairo_surface_t* surface, std::string path, int target_size) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto* xpm_surface = cairo_image_surface_create_from_xpm(path.c_str());
    if (!xpm_surface)
        return false;

    if (cairo_surface_status(xpm_surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(xpm_surface);
        return false;
    }

    auto* temp_context = cairo_create(surface);
    int   w            = cairo_image_surface_get_width(xpm_surface);

    cairo_save(temp_context);
    cairo_set_operator(temp_context, CAIRO_OPERATOR_CLEAR);
    cairo_paint(temp_context);
    cairo_restore(temp_context);

    cairo_save(temp_context);
    if (target_size != w) {
        double scale = ((double)target_size) / ((double)w);
        cairo_scale(temp_context, scale, scale);
    }
    cairo_set_source_surface(temp_context, xpm_surface, 0, 0);
    cairo_paint(temp_context);
    cairo_restore(temp_context);
    cairo_destroy(temp_context);
    cairo_surface_destroy(xpm_surface);

    return true;
}

bool paint_surface_with_image(cairo_surface_t* surface, std::string path, int target_size, void (*upon_completion)(bool)) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    bool success = false;
    if (path.find(".svg") != std::string::npos) {
        success = paint_svg_to_surface(surface, path, target_size);
    } else if (path.find(".png") != std::string::npos) {
        success = paint_png_to_surface(surface, path, target_size);
    } else if (path.find(".xpm") != std::string::npos) {
        success = paint_xpm_to_surface(surface, path, target_size);
    }
    if (upon_completion != nullptr) {
        upon_completion(success);
    }
    return success;
}

cairo_surface_t* accelerated_surface(int w, int h) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    cairo_surface_t* raw_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);

    /*
  cairo_surface_t *fast_surface = cairo_surface_create_similar_image(
          cairo_get_target(client_entity->cr), CAIRO_FORMAT_ARGB32, w, h);
          */

    if (cairo_surface_status(raw_surface) != CAIRO_STATUS_SUCCESS)
        return nullptr;

    return raw_surface;
}

void load_icon_full_path(cairo_surface_t** surface, std::string path, int target_size) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (path.find("svg") != std::string::npos) {
        *surface = accelerated_surface(target_size, target_size);
        paint_svg_to_surface(*surface, path, target_size);
    } else if (path.find("png") != std::string::npos) {
        *surface = accelerated_surface(target_size, target_size);
        paint_png_to_surface(*surface, path, target_size);
    } else if (path.find("xpm") != std::string::npos) {
        *surface = accelerated_surface(target_size, target_size);
        paint_xpm_to_surface(*surface, path, target_size);
    }
}

SP<CTexture> missingTexure(int size) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    SP<CTexture> tex = makeShared<CTexture>();
    tex->allocate();

    const auto CAIROSURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 512, 512);
    const auto CAIRO        = cairo_create(CAIROSURFACE);

    cairo_set_antialias(CAIRO, CAIRO_ANTIALIAS_NONE);
    cairo_save(CAIRO);
    cairo_set_source_rgba(CAIRO, 0, 0, 0, 1);
    cairo_set_operator(CAIRO, CAIRO_OPERATOR_SOURCE);
    cairo_paint(CAIRO);
    cairo_set_source_rgba(CAIRO, 1, 0, 1, 1);
    cairo_rectangle(CAIRO, 256, 0, 256, 256);
    cairo_fill(CAIRO);
    cairo_rectangle(CAIRO, 0, 256, 256, 256);
    cairo_fill(CAIRO);
    cairo_restore(CAIRO);

    cairo_surface_flush(CAIROSURFACE);

    tex->m_size = {512, 512};

    // copy the data to an OpenGL texture we have
    const GLint glFormat = GL_RGBA;
    const GLint glType   = GL_UNSIGNED_BYTE;

    const auto  DATA = cairo_image_surface_get_data(CAIROSURFACE);
    tex->bind();
    tex->setTexParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    tex->setTexParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    tex->setTexParameter(GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    tex->setTexParameter(GL_TEXTURE_SWIZZLE_B, GL_RED);
    glTexImage2D(GL_TEXTURE_2D, 0, glFormat, tex->m_size.x, tex->m_size.y, 0, glFormat, glType, DATA);

    cairo_surface_destroy(CAIROSURFACE);
    cairo_destroy(CAIRO);

    return tex;
}

SP<CTexture> loadAsset(const std::string& filename, int target_size) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    cairo_surface_t* icon = nullptr;
    load_icon_full_path(&icon, filename, target_size);
    if (!icon)
        return {};

    const auto CAIROFORMAT = cairo_image_surface_get_format(icon);
    auto       tex         = makeShared<CTexture>();

    tex->allocate();
    tex->m_size = {cairo_image_surface_get_width(icon), cairo_image_surface_get_height(icon)};

    const GLint glIFormat = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_RGB32F : GL_RGBA;
    const GLint glFormat  = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_RGB : GL_RGBA;
    const GLint glType    = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_FLOAT : GL_UNSIGNED_BYTE;

    const auto  DATA = cairo_image_surface_get_data(icon);
    tex->bind();
    tex->setTexParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    tex->setTexParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    if (CAIROFORMAT != CAIRO_FORMAT_RGB96F) {
        tex->setTexParameter(GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        tex->setTexParameter(GL_TEXTURE_SWIZZLE_B, GL_RED);
    }

    glTexImage2D(GL_TEXTURE_2D, 0, glIFormat, tex->m_size.x, tex->m_size.y, 0, glFormat, glType, DATA);

    cairo_surface_destroy(icon);

    return tex;
}

void free_text_texture(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (int i = 0; i < hyprtextures.size(); i++) {
        auto h = hyprtextures[i];
        if (h->info.id == id) {
            h->texture.reset();
            printf("free: %d\n", h->info.id);
            hyprtextures.erase(hyprtextures.begin() + i);
            delete h;
        }
    }
}

TextureInfo gen_texture(std::string path, float h) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    log("gen texture");
    //notify("gen texture");
    auto tex = loadAsset(path, h);
    if (tex.get()) {
        auto t = new Texture;
        t->texture = tex;
        TextureInfo info;
        info.id = unique_id++;
        info.w = t->texture->m_size.x;
        info.h = t->texture->m_size.y;
        printf("generate pic: %d\n", info.id);
        t->info = info;
        hyprtextures.push_back(t);
        return t->info;
    }
    return {};
}

TextureInfo gen_text_texture(std::string font, std::string text, float h, RGBA color) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    log("gen text texture");
    //notify("gen text");
    auto tex = g_pHyprOpenGL->renderText(text, CHyprColor(color.r, color.g, color.b, color.a), h, false, font, 0);
    if (tex.get()) {
        auto t = new Texture;
        t->texture = tex;
        TextureInfo info;
        info.id = unique_id++;
        info.w = t->texture->m_size.x;
        info.h = t->texture->m_size.y;
        t->info = info;
        hyprtextures.push_back(t);
        printf("generate text: %d\n", info.id);
        return t->info;
    }
    return {};
}

void draw_texture(TextureInfo info, int x, int y, float a, float clip_w) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    //return;
    for (auto t : hyprtextures) {
        
       if (t->info.id == info.id) {
            CTexPassElement::SRenderData data;
            data.tex = t->texture;
            data.box = {(float) x, (float) y, data.tex->m_size.x, data.tex->m_size.y};
            data.box.x = x;
            data.box.round();
            auto inter = data.box;
            if (hypriso->clip) {
                inter = tocbox(hypriso->clipbox);
                //.intersection(data.box)
                if (data.box.inside(inter)) {
                    inter = tocbox(hypriso->clipbox).intersection(data.box);
                }
            }
            
            data.clipBox = inter;
            if (clip_w != 0.0) {
                data.clipBox.w = clip_w;
            }
            data.a = 1.0 * a;
            g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(std::move(data)));
            
       }
    }
}

void setCursorImageUntilUnset(std::string cursor) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    g_pInputManager->setCursorImageUntilUnset(cursor);

}

void unsetCursorImage() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    g_pInputManager->unsetCursorImage();
}

int get_monitor(int client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
       if (hw->id == client) {
           for (auto hm : hyprmonitors) {
              if (hm->m == hw->w->m_monitor) {
                  return hm->id;
              } 
           }
       } 
    }
    return -1; 
}

Bounds mouse() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto mouse = g_pInputManager->getMouseCoordsInternal();
    return {mouse.x, mouse.y, mouse.x, mouse.y};
}

void close_window(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            g_pCompositor->closeWindow(hw->w);
        }
    }
}

int HyprIso::monitor_from_cursor() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto m = g_pCompositor->getMonitorFromCursor();
    for (auto hm : hyprmonitors) {
        if (hm->m == m) {
            return hm->id;   
        }
    }
    return -1;
}

bool HyprIso::is_mapped(int id) {
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            return hw->w->m_isMapped;
        }
    }
    return false; 
}

bool HyprIso::is_hidden(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            return hw->is_hidden;
        }
    }
    return false; 
}

void HyprIso::set_hidden(int id, bool state) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            hw->w->updateWindowDecos();
            hw->w->setHidden(state);
            hw->is_hidden = state;
        }
    }
    for (auto m : hyprmonitors) {
        hypriso->damage_entire(m->id);
    }
 
    /*
    if (state) {
        // only if w is top w
        alt_tab_menu.change_showing(true);
        if (!alt_tab_menu.options.empty()) {
            auto& o = alt_tab_menu.options[0];
            if (o.window == w) {
                tab_next_window();
            }
        }
        alt_tab_menu.change_showing(false);

        auto window_data = get_or_create_window_data(w.get());
        //for (auto it = w.get()->m_windowDecorations.rbegin(); it != w.get()->m_windowDecorations.rend(); ++it) {
            //auto bar = (IHyprWindowDecoration *) it->get();
            //if (bar->getDisplayName() == "MylarBar" || bar->getDisplayName() == "MylarResize") {
                //HyprlandAPI::removeWindowDecoration(stable_hypr::APIHANDLE, bar);
            //}
        //}
        window_data->iconified = true;
        w->updateWindowDecos();
        w->setHidden(true);
    } else {
        w->setHidden(false);
        //add_decorations_to_window(w);
        get_or_create_window_data(w.get())->iconified = false;
        switchToWindow(w, true);
    }
    */
}

void HyprIso::bring_to_front(int id, bool focus) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            if (focus)
                g_pCompositor->focusWindow(hw->w);
            g_pCompositor->changeWindowZOrder(hw->w, true);
        }
    }
}

Bounds HyprIso::min_size(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            auto s = hw->w->requestedMinSize();
            return {s.x, s.y, s.x, s.y};
        }
    }
    return {20, 20, 20, 20};
}

bool HyprIso::has_decorations(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            for (const auto &decos : hw->w->m_windowDecorations) {
               if (decos->getDisplayName() == "MylarBar") {
                   return true;
               } 
            }
        }
    }
    return false;
}


bool HyprIso::is_x11(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            return hw->w->m_isX11;
        }
    }
    return false;
}

void HyprIso::send_key(uint32_t key) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto k : g_pInputManager->m_keyboards) {
        IKeyboard::SKeyEvent event;
        event.timeMs     = get_current_time_in_ms();
        event.updateMods = false;
        event.keycode    = key;
        event.state      = WL_KEYBOARD_KEY_STATE_PRESSED;
        g_pInputManager->onKeyboardKey(event, k);
        event.timeMs = get_current_time_in_ms();
        event.state  = WL_KEYBOARD_KEY_STATE_RELEASED;
        g_pInputManager->onKeyboardKey(event, k);
    }
}

bool HyprIso::is_fullscreen(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            return hw->w->isFullscreen();
        }
    }
    return false;
}

void HyprIso::should_round(int id, bool state) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            hw->no_rounding = !state;
        }
    }
}

void HyprIso::damage_entire(int monitor) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hm : hyprmonitors) {
        if (hm->id == monitor) {
            g_pHyprRenderer->damageMonitor(hm->m);
        }
    }
}

void HyprIso::damage_box(Bounds b) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    g_pHyprRenderer->damageBox(b.x, b.y, b.w, b.h);
}

int later_action(void* user_data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto timer = (Timer*)user_data;
    if (timer->func)
        timer->func(timer);
    if (!timer->keep_running) {
        // remove from vec
        wl_event_source_remove(timer->source);
        delete timer;
    } else {
        wl_event_source_timer_update(timer->source, timer->delay);
    }
    return 0;
}

Timer* later(void* data, float time_ms, const std::function<void(Timer*)>& fn) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto timer    = new Timer;
    timer->func   = fn;
    timer->data   = data;
    timer->delay  = time_ms;
    timer->source = wl_event_loop_add_timer(g_pCompositor->m_wlEventLoop, &later_action, timer);
    wl_event_source_timer_update(timer->source, time_ms);
    return timer;
}

Timer* later(float time_ms, const std::function<void(Timer*)>& fn) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto timer    = new Timer;
    timer->func   = fn;
    timer->delay  = time_ms;
    timer->source = wl_event_loop_add_timer(g_pCompositor->m_wlEventLoop, &later_action, timer);
    wl_event_source_timer_update(timer->source, time_ms);
    return timer;
}

Timer* later_immediate(const std::function<void(Timer*)>& fn) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    return later(1, fn);
}

void screenshot_monitor(CFramebuffer* buffer, PHLMONITOR m) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (!buffer || !pRenderMonitor)
        return;
    if (!m || !m->m_output || m->m_pixelSize.x <= 0 || m->m_pixelSize.y <= 0)
        return;
    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    g_pHyprRenderer->makeEGLCurrent();
    buffer->alloc(m->m_pixelSize.x, m->m_pixelSize.y, DRM_FORMAT_ABGR8888);
    g_pHyprRenderer->beginRender(m, fakeDamage, RENDER_MODE_FULL_FAKE, nullptr, buffer);
    g_pHyprRenderer->m_bRenderingSnapshot = true;
    g_pHyprOpenGL->clear(CHyprColor(0, 0, 0, 0)); // JIC
    g_pHyprOpenGL->m_renderData.pMonitor = m;
    (*(tRenderMonitor)pRenderMonitor)(g_pHyprRenderer.get(), m, false);
    g_pHyprOpenGL->m_renderData.pMonitor  = m;
    g_pHyprOpenGL->m_renderData.outFB     = buffer;
    g_pHyprOpenGL->m_renderData.currentFB = buffer;
    g_pHyprRenderer->endRender();
    g_pHyprRenderer->m_bRenderingSnapshot = false;
}


void render_wallpaper(PHLMONITOR pMonitor, const Time::steady_tp& time, const Vector2D& translate, const float& scale) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    static auto PDIMSPECIAL      = CConfigValue<Hyprlang::FLOAT>("decoration:dim_special");
    static auto PBLURSPECIAL     = CConfigValue<Hyprlang::INT>("decoration:blur:special");
    static auto PBLUR            = CConfigValue<Hyprlang::INT>("decoration:blur:enabled");
    static auto PXPMODE          = CConfigValue<Hyprlang::INT>("render:xp_mode");
    static auto PSESSIONLOCKXRAY = CConfigValue<Hyprlang::INT>("misc:session_lock_xray");

    /*
    if (!pMonitor)
        return;

    if (g_pSessionLockManager->isSessionLocked() && !*PSESSIONLOCKXRAY) {
        // We stop to render workspaces as soon as the lockscreen was sent the "locked" or "finished" (aka denied) event.
        // In addition we make sure to stop rendering workspaces after misc:lockdead_screen_delay has passed.
        if (g_pSessionLockManager->shallConsiderLockMissing() || g_pSessionLockManager->clientLocked() || g_pSessionLockManager->clientDenied())
            return;
    }

    // todo: matrices are buggy atm for some reason, but probably would be preferable in the long run
    // g_pHyprOpenGL->saveMatrix();
    // g_pHyprOpenGL->setMatrixScaleTranslate(translate, scale);

    SRenderModifData RENDERMODIFDATA;
    if (translate != Vector2D{0, 0})
        RENDERMODIFDATA.modifs.emplace_back(std::make_pair<>(SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE, translate));
    if (scale != 1.f)
        RENDERMODIFDATA.modifs.emplace_back(std::make_pair<>(SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE, scale));

    if (!RENDERMODIFDATA.modifs.empty())
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{RENDERMODIFDATA}));

    Hyprutils::Utils::CScopeGuard x([&RENDERMODIFDATA] {
        if (!RENDERMODIFDATA.modifs.empty()) {
            g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{SRenderModifData{}}));
        }
    });
    */
    //g_pHyprRenderer->renderBackground(pMonitor);
    g_pHyprOpenGL->clearWithTex();

    for (auto const& ls : pMonitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]) {
        g_pHyprRenderer->renderLayer(ls.lock(), pMonitor, time);
    }

    EMIT_HOOK_EVENT("render", RENDER_POST_WALLPAPER);

    for (auto const& ls : pMonitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]) {
        g_pHyprRenderer->renderLayer(ls.lock(), pMonitor, time);
    }

    for (auto const& ls : pMonitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]) {
        g_pHyprRenderer->renderLayer(ls.lock(), pMonitor, time);
    }

    for (auto const& ls : pMonitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]) {
        g_pHyprRenderer->renderLayer(ls.lock(), pMonitor, time);
    }
}

void actual_screenshot_wallpaper(CFramebuffer* buffer, PHLMONITOR m) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (!buffer || !pRenderMonitor)
        return;
    if (!m || !m->m_output || m->m_pixelSize.x <= 0 || m->m_pixelSize.y <= 0)
        return;
    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    g_pHyprRenderer->makeEGLCurrent();
    buffer->alloc(m->m_pixelSize.x, m->m_pixelSize.y, DRM_FORMAT_ABGR8888);
    g_pHyprRenderer->beginRender(m, fakeDamage, RENDER_MODE_FULL_FAKE, nullptr, buffer);
    g_pHyprOpenGL->clear(CHyprColor(0, 0, 1, 1)); // JIC

    const auto NOW = Time::steadyNow();
    render_wallpaper(m, NOW, {0.0, 0.0}, 1.0);
    
    g_pHyprRenderer->endRender();
}

void screenshot_workspace(CFramebuffer* buffer, PHLWORKSPACEREF w, PHLMONITOR m, bool include_cursor) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    //return;
    if (!buffer || pRenderWorkspace == nullptr)
        return;
    if (!m || !m->m_output || m->m_pixelSize.x <= 0 || m->m_pixelSize.y <= 0)
        return;
    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    g_pHyprRenderer->makeEGLCurrent();
    buffer->alloc(m->m_pixelSize.x, m->m_pixelSize.y, DRM_FORMAT_ABGR8888);
    g_pHyprRenderer->beginRender(m, fakeDamage, RENDER_MODE_FULL_FAKE, nullptr, buffer);
    g_pHyprOpenGL->clear(CHyprColor(0, 0, 0, 0)); // JIC
    g_pHyprOpenGL->m_renderData.pMonitor = m;
    const auto NOW = Time::steadyNow();

    notify("screenshot " + std::to_string(w->m_id) + " " + std::to_string((unsigned long long) buffer));

    /*
    pMonitor->m_activeWorkspace = PWORKSPACE;
    g_pDesktopAnimationManager->startAnimation(PWORKSPACE, CDesktopAnimationManager::ANIMATION_TYPE_IN, true, true);
    PWORKSPACE->m_visible = true;

    if (PWORKSPACE == startedOn)
        pMonitor->m_activeSpecialWorkspace = openSpecial;

    g_pHyprRenderer->renderWorkspace(pMonitor.lock(), PWORKSPACE, Time::steadyNow(), monbox);

    PWORKSPACE->m_visible = false;
    g_pDesktopAnimationManager->startAnimation(PWORKSPACE, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false, true);

    if (PWORKSPACE == startedOn)
        pMonitor->m_activeSpecialWorkspace.reset();
    */

    auto backup = m->m_activeWorkspace;
    auto visibility = w->m_visible;
    w->m_visible = true;
    
    //(*(tRenderWorkspace)pRenderWorkspace)(g_pHyprRenderer.get(), m, w.lock(), NOW, CBox(0, 0, (int)m->m_pixelSize.x, (int)m->m_pixelSize.y));
    //(*(tRenderWorkspace)pRenderWorkspace)(g_pHyprRenderer.get(), m, w.lock(), NOW, CBox(0, 0, (int)m->m_pixelSize.x, (int)m->m_pixelSize.y));
    (*(tRenderWorkspaceWindows)pRenderWorkspaceWindows)(g_pHyprRenderer.get(), m, w.lock(), NOW);
    
    /*
    if (auto wmon = w->m_monitor.lock()) {
        (*(tRenderWorkspace)pRenderWorkspace)(g_pHyprRenderer.get(), wmon, w.lock(), NOW, CBox(0, 0, (int)m->m_pixelSize.x, (int)m->m_pixelSize.y));
    }
*/

    w->m_visible = visibility;
    m->m_activeWorkspace = backup;
    //(*(tRenderWorkspaceWindowsFullscreen)pRenderWorkspaceWindowsFullscreen)(g_pHyprRenderer.get(), m, w.lock(), NOW);

    
    g_pHyprRenderer->endRender();
}

void makeSnapshot(PHLWINDOW pWindow, CFramebuffer *PFRAMEBUFFER) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // we trust the window is valid.
    const auto PMONITOR = pWindow->m_monitor.lock();

    if (!PMONITOR || !PMONITOR->m_output || PMONITOR->m_pixelSize.x <= 0 || PMONITOR->m_pixelSize.y <= 0)
        return;

    if (!g_pHyprRenderer->shouldRenderWindow(pWindow))
        return; // ignore, window is not being rendered

    Debug::log(LOG, "renderer: making a snapshot of {:x}", rc<uintptr_t>(pWindow.get()));

    // we need to "damage" the entire monitor
    // so that we render the entire window
    // this is temporary, doesn't mess with the actual damage
    CRegion      fakeDamage{0, 0, PMONITOR->m_transformedSize.x, PMONITOR->m_transformedSize.y};

    PHLWINDOWREF ref{pWindow};

    g_pHyprRenderer->makeEGLCurrent();

    //const auto PFRAMEBUFFER = &g_pHyprOpenGL->m_windowFramebuffers[ref];

    PFRAMEBUFFER->alloc(PMONITOR->m_pixelSize.x, PMONITOR->m_pixelSize.y, DRM_FORMAT_ABGR8888);

    g_pHyprRenderer->beginRender(PMONITOR, fakeDamage, RENDER_MODE_FULL_FAKE, nullptr, PFRAMEBUFFER);

    g_pHyprRenderer->m_bRenderingSnapshot = true;

    g_pHyprOpenGL->clear(CHyprColor(0, 0, 0, 0)); // JIC

    g_pHyprRenderer->renderWindow(pWindow, PMONITOR, Time::steadyNow(), true, RENDER_PASS_ALL, true);

    g_pHyprRenderer->endRender();

    g_pHyprRenderer->m_bRenderingSnapshot = false;
}

void renderWindow(PHLWINDOW pWindow, PHLMONITOR pMonitor, const Time::steady_tp& time, bool decorate, eRenderPassMode mode, bool ignorePosition, bool standalone) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (pWindow->m_fadingOut) {
        if (pMonitor == pWindow->m_monitor) // TODO: fix this
            g_pHyprRenderer->renderSnapshot(pWindow);
        return;
    }

    if (!pWindow->m_isMapped)
        return;

    TRACY_GPU_ZONE("RenderWindow");

    const auto                       PWORKSPACE = pWindow->m_workspace;
    const auto                       REALPOS    = pWindow->m_realPosition->value() + (pWindow->m_pinned ? Vector2D{} : PWORKSPACE->m_renderOffset->value());
    static auto                      PDIMAROUND = CConfigValue<Hyprlang::FLOAT>("decoration:dim_around");

    CSurfacePassElement::SRenderData renderdata = {pMonitor, time};
    CBox                             textureBox = {REALPOS.x, REALPOS.y, std::max(pWindow->m_realSize->value().x, 5.0), std::max(pWindow->m_realSize->value().y, 5.0)};

    renderdata.pos.x = textureBox.x;
    renderdata.pos.y = textureBox.y;
    renderdata.w     = textureBox.w;
    renderdata.h     = textureBox.h;

    if (ignorePosition) {
        renderdata.pos.x = pMonitor->m_position.x;
        renderdata.pos.y = pMonitor->m_position.y;
    } else {
        const bool ANR = pWindow->isNotResponding();
        if (ANR && pWindow->m_notRespondingTint->goal() != 0.2F)
            *pWindow->m_notRespondingTint = 0.2F;
        else if (!ANR && pWindow->m_notRespondingTint->goal() != 0.F)
            *pWindow->m_notRespondingTint = 0.F;
    }

    //if (standalone)
        //decorate = false;

    // whether to use m_fMovingToWorkspaceAlpha, only if fading out into an invisible ws
    const bool USE_WORKSPACE_FADE_ALPHA = pWindow->m_monitorMovedFrom != -1 && (!PWORKSPACE || !PWORKSPACE->isVisible());

    renderdata.surface   = pWindow->m_wlSurface->resource();
    renderdata.dontRound = pWindow->isEffectiveInternalFSMode(FSMODE_FULLSCREEN) || pWindow->m_windowData.noRounding.valueOrDefault();
    renderdata.fadeAlpha = pWindow->m_alpha->value() * (pWindow->m_pinned || USE_WORKSPACE_FADE_ALPHA ? 1.f : PWORKSPACE->m_alpha->value()) *
        (USE_WORKSPACE_FADE_ALPHA ? pWindow->m_movingToWorkspaceAlpha->value() : 1.F) * pWindow->m_movingFromWorkspaceAlpha->value();
    renderdata.alpha         = pWindow->m_activeInactiveAlpha->value();
    renderdata.decorate      = decorate && !pWindow->m_X11DoesntWantBorders && !pWindow->isEffectiveInternalFSMode(FSMODE_FULLSCREEN);
    renderdata.rounding      = standalone || renderdata.dontRound ? 0 : pWindow->rounding() * pMonitor->m_scale;
    renderdata.roundingPower = standalone || renderdata.dontRound ? 2.0f : pWindow->roundingPower();
    //renderdata.blur          = !standalone && g_pHyprRenderer->shouldBlur(pWindow);
    renderdata.blur          = false;
    renderdata.pWindow       = pWindow;

    if (standalone) {
        renderdata.alpha     = 1.f;
        renderdata.fadeAlpha = 1.f;
    }

    // apply opaque
    if (pWindow->m_windowData.opaque.valueOrDefault())
        renderdata.alpha = 1.f;

    renderdata.pWindow = pWindow;

    // for plugins
    g_pHyprOpenGL->m_renderData.currentWindow = pWindow;

    EMIT_HOOK_EVENT("render", RENDER_PRE_WINDOW);

    const auto fullAlpha = renderdata.alpha * renderdata.fadeAlpha;

    if (*PDIMAROUND && pWindow->m_windowData.dimAround.valueOrDefault() && !g_pHyprRenderer->m_bRenderingSnapshot && mode != RENDER_PASS_POPUP) {
        CBox                        monbox = {0, 0, g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.x, g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.y};
        CRectPassElement::SRectData data;
        data.color = CHyprColor(0, 0, 0, *PDIMAROUND * fullAlpha);
        data.box   = monbox;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));
    }

    renderdata.pos.x += pWindow->m_floatingOffset.x;
    renderdata.pos.y += pWindow->m_floatingOffset.y;

    // if window is floating and we have a slide animation, clip it to its full bb
    if (!ignorePosition && pWindow->m_isFloating && !pWindow->isFullscreen() && PWORKSPACE->m_renderOffset->isBeingAnimated() && !pWindow->m_pinned) {
        CRegion rg =
            pWindow->getFullWindowBoundingBox().translate(-pMonitor->m_position + PWORKSPACE->m_renderOffset->value() + pWindow->m_floatingOffset).scale(pMonitor->m_scale);
        renderdata.clipBox = rg.getExtents();
    }

    // render window decorations first, if not fullscreen full
    if (mode == RENDER_PASS_ALL || mode == RENDER_PASS_MAIN) {

        const bool TRANSFORMERSPRESENT = !pWindow->m_transformers.empty();

        if (TRANSFORMERSPRESENT) {
            g_pHyprOpenGL->bindOffMain();

            for (auto const& t : pWindow->m_transformers) {
                t->preWindowRender(&renderdata);
            }
        }

        if (renderdata.decorate) {
            for (auto const& wd : pWindow->m_windowDecorations) {
                if (wd->getDecorationLayer() != DECORATION_LAYER_BOTTOM)
                    continue;

                wd->draw(pMonitor, fullAlpha);
            }

            for (auto const& wd : pWindow->m_windowDecorations) {
                if (wd->getDecorationLayer() != DECORATION_LAYER_UNDER)
                    continue;

                wd->draw(pMonitor, fullAlpha);
            }
        }

        static auto PXWLUSENN = CConfigValue<Hyprlang::INT>("xwayland:use_nearest_neighbor");
        if ((pWindow->m_isX11 && *PXWLUSENN) || pWindow->m_windowData.nearestNeighbor.valueOrDefault())
            renderdata.useNearestNeighbor = true;

        if (pWindow->m_wlSurface->small() && !pWindow->m_wlSurface->m_fillIgnoreSmall && renderdata.blur) {
            CBox wb = {renderdata.pos.x - pMonitor->m_position.x, renderdata.pos.y - pMonitor->m_position.y, renderdata.w, renderdata.h};
            wb.scale(pMonitor->m_scale).round();
            CRectPassElement::SRectData data;
            data.color = CHyprColor(0, 0, 0, 0);
            data.box   = wb;
            data.round = renderdata.dontRound ? 0 : renderdata.rounding - 1;
            data.blur  = true;
            data.blurA = renderdata.fadeAlpha;
            data.xray  = g_pHyprOpenGL->shouldUseNewBlurOptimizations(nullptr, pWindow);
            g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));
            renderdata.blur = false;
        }

        renderdata.surfaceCounter = 0;
        pWindow->m_wlSurface->resource()->breadthfirst(
            [&renderdata, &pWindow](SP<CWLSurfaceResource> s, const Vector2D& offset, void* data) {
                renderdata.localPos    = offset;
                renderdata.texture     = s->m_current.texture;
                renderdata.surface     = s;
                renderdata.mainSurface = s == pWindow->m_wlSurface->resource();
                g_pHyprRenderer->m_renderPass.add(makeUnique<CSurfacePassElement>(renderdata));
                renderdata.surfaceCounter++;
            },
            nullptr);

        renderdata.useNearestNeighbor = false;

        if (renderdata.decorate) {
            for (auto const& wd : pWindow->m_windowDecorations) {
                if (wd->getDecorationLayer() != DECORATION_LAYER_OVER)
                    continue;

                wd->draw(pMonitor, fullAlpha);
            }
        }

        if (TRANSFORMERSPRESENT) {
            CFramebuffer* last = g_pHyprOpenGL->m_renderData.currentFB;
            for (auto const& t : pWindow->m_transformers) {
                last = t->transform(last);
            }

            g_pHyprOpenGL->bindBackOnMain();
            g_pHyprOpenGL->renderOffToMain(last);
        }
    }

    g_pHyprOpenGL->m_renderData.clipBox = CBox();

    if (mode == RENDER_PASS_ALL || mode == RENDER_PASS_POPUP) {
        if (!pWindow->m_isX11) {
            CBox geom = pWindow->m_xdgSurface->m_current.geometry;

            renderdata.pos -= geom.pos();
            renderdata.dontRound       = true; // don't round popups
            renderdata.pMonitor        = pMonitor;
            renderdata.squishOversized = false; // don't squish popups
            renderdata.popup           = true;

            static CConfigValue PBLURIGNOREA = CConfigValue<Hyprlang::FLOAT>("decoration:blur:popups_ignorealpha");

            renderdata.blur = g_pHyprRenderer->shouldBlur(pWindow->m_popupHead);

            if (renderdata.blur) {
                renderdata.discardMode |= DISCARD_ALPHA;
                renderdata.discardOpacity = *PBLURIGNOREA;
            }

            if (pWindow->m_windowData.nearestNeighbor.valueOrDefault())
                renderdata.useNearestNeighbor = true;

            renderdata.surfaceCounter = 0;

            pWindow->m_popupHead->breadthfirst(
                [&renderdata](WP<CPopup> popup, void* data) {
                    if (popup->m_fadingOut) {
                        g_pHyprRenderer->renderSnapshot(popup);
                        return;
                    }

                    if (!popup->m_wlSurface || !popup->m_wlSurface->resource() || !popup->m_mapped)
                        return;
                    const auto     pos    = popup->coordsRelativeToParent();
                    const Vector2D oldPos = renderdata.pos;
                    renderdata.pos += pos;
                    renderdata.fadeAlpha = popup->m_alpha->value();

                    popup->m_wlSurface->resource()->breadthfirst(
                        [&renderdata](SP<CWLSurfaceResource> s, const Vector2D& offset, void* data) {
                            renderdata.localPos    = offset;
                            renderdata.texture     = s->m_current.texture;
                            renderdata.surface     = s;
                            renderdata.mainSurface = false;
                            g_pHyprRenderer->m_renderPass.add(makeUnique<CSurfacePassElement>(renderdata));
                            renderdata.surfaceCounter++;
                        },
                        data);

                    renderdata.pos = oldPos;
                },
                &renderdata);

            renderdata.alpha = 1.F;
        }

        if (decorate) {
            for (auto const& wd : pWindow->m_windowDecorations) {
                if (wd->getDecorationLayer() != DECORATION_LAYER_OVERLAY)
                    continue;

                wd->draw(pMonitor, fullAlpha);
            }
        }
    }

    EMIT_HOOK_EVENT("render", RENDER_POST_WINDOW);

    g_pHyprOpenGL->m_renderData.currentWindow.reset();
}


void screenshot_window_with_decos(CFramebuffer* buffer, PHLWINDOW w) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    //makeSnapshot(w, buffer);
    //return;
    //return;
    if (!buffer || !pRenderWindow || !w)
        return;
    const auto m = w->m_monitor.lock();
    if (!m || !m->m_output || m->m_pixelSize.x <= 0 || m->m_pixelSize.y <= 0)
        return;
    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};

    g_pHyprRenderer->makeEGLCurrent();

    auto ex = g_pDecorationPositioner->getWindowDecorationExtents(w, false);
    buffer->alloc(m->m_pixelSize.x + ex.topLeft.x + ex.bottomRight.x, m->m_pixelSize.y + ex.topLeft.y + ex.bottomRight.y, DRM_FORMAT_ABGR8888);
    g_pHyprRenderer->beginRender(m, fakeDamage, RENDER_MODE_FULL_FAKE, nullptr, buffer);

    g_pHyprRenderer->m_bRenderingSnapshot = true;
    g_pHyprOpenGL->clear(CHyprColor(0, 0, 0, 0)); // JIC

    auto const NOW = Time::steadyNow();

    auto fo = w->m_floatingOffset;
    w->m_floatingOffset.x -= w->m_realPosition->value().x - ex.topLeft.x;
    w->m_floatingOffset.y -= w->m_realPosition->value().y - ex.topLeft.y;
    auto before = w->m_hidden;
    w->m_hidden = false;
    //(*(tRenderWindow)pRenderWindow)(g_pHyprRenderer.get(), w, m, NOW, true, RENDER_PASS_ALL, false, true);

    //notify("screen deco");
    //(*(tRenderWindow)pRenderWindow)(g_pHyprRenderer.get(), w, m, NOW, true, RENDER_PASS_ALL, false, true);
    //
    renderWindow(w, m, NOW, true, RENDER_PASS_ALL, false, true);
    w->m_hidden = before;
    
    /*
    for (auto& de : w->m_windowDecorations) {
        if (de->getDisplayName() == "MylarBar") {
            int clientid = 0;
            for (auto ci : hyprwindows) {
                if (ci->w == w) {
                    clientid = ci->id;
                }
            }
            int monitorid = get_monitor(clientid);

            hypriso->on_draw_decos(de->getDisplayName(), monitorid, clientid, 1.0);
        }
    }
    */
    //(*(tRenderWindow)pRenderWindow)(g_pHyprRenderer.get(), w, m, NOW, true, RENDER_PASS_ALL, false, true);
    w->m_floatingOffset = fo;

    g_pHyprRenderer->endRender();

    g_pHyprRenderer->m_bRenderingSnapshot = false;
}

void screenshot_window(HyprWindow *hw, PHLWINDOW w, bool include_decorations) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    //return;
    if (!pRenderWindow || !w.get())
        return;
    const auto m = w->m_monitor.lock();
    if (!m || !m->m_output || m->m_pixelSize.x <= 0 || m->m_pixelSize.y <= 0)
        return;
    if (include_decorations) {
        bool h = w->m_hidden;
        w->m_hidden = false;
        screenshot_window_with_decos(hw->deco_fb, w);
       //m->m_scale 
        hw->w_decos_size = tobounds(w->getFullWindowBoundingBox());
        hw->w_decos_size.scale(m->m_scale);
        w->m_hidden = h;

        return;
    }

    // we need to "damage" the entire monitor
    // so that we render the entire window
    // this is temporary, doesnt mess with the actual damage
    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    g_pHyprRenderer->makeEGLCurrent();
    hw->fb->alloc(m->m_pixelSize.x, m->m_pixelSize.y, DRM_FORMAT_ABGR8888);
    g_pHyprRenderer->beginRender(m, fakeDamage, RENDER_MODE_FULL_FAKE, nullptr, hw->fb);
    g_pHyprRenderer->m_bRenderingSnapshot = true;
    g_pHyprOpenGL->clear(CHyprColor(0, 0, 0, 0)); // JIC
    auto const NOW = Time::steadyNow();
    (*(tRenderWindow)pRenderWindow)(g_pHyprRenderer.get(), w, m, NOW, false, RENDER_PASS_MAIN, true, true);
    g_pHyprRenderer->endRender();
    g_pHyprRenderer->m_bRenderingSnapshot = false;

    hw->w_size = Bounds(0, 0, (w->m_realSize->value().x * m->m_scale), (w->m_realSize->value().y * m->m_scale));
}

void HyprIso::screenshot_all() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto w : g_pCompositor->m_windows) {
        bool has_mylar_bar = false;
        for (const auto &decos : w->m_windowDecorations) 
            if (decos->getDisplayName() == "MylarBar")
                has_mylar_bar = true;
            
        if (true) {
            for (auto hw : hyprwindows) {
                if (hw->w == w) {
                    if (!hw->fb)
                        hw->fb = new CFramebuffer;
                    screenshot_window(hw, w, false);
                }
            }
        }
    }
}

void HyprIso::draw_workspace(int mon, int id, Bounds b, int rounding) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    //return;
    for (auto hs : hyprspaces) {
        if (hs->w->m_id != id)
            continue;
        if (!hs->buffer->isAllocated())
            continue;
        //notify("draw space " + std::to_string(id));
        AnyPass::AnyData anydata([b, hs, rounding](AnyPass* pass) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
            //notify("draw");
            auto roundingPower = 2.0f;
            auto cornermask = 0;
            auto tex = hs->buffer->getTexture();
            notify(std::to_string(hs->w->m_id) + " " + std::to_string((unsigned long long) hs->buffer));
            
            auto box = tocbox(b);

            CHyprOpenGLImpl::STextureRenderData data;
            data.allowCustomUV = true;

            data.round = rounding;
            data.noAA = true;
            data.roundingPower = roundingPower;
            g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft     = Vector2D(0, 0);
            g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = Vector2D(
                std::min(1.0, 1.0),
                std::min(1.0, 1.0)
            );
            set_rounding(cornermask);
            g_pHyprOpenGL->renderTexture(tex, box, data);
            set_rounding(0);
            g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft     = Vector2D(-1, -1);
            g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = Vector2D(-1, -1);
        });
        g_pHyprRenderer->m_renderPass.add(makeUnique<AnyPass>(std::move(anydata)));
    }
};

void HyprIso::draw_wallpaper(int mon, Bounds b, int rounding) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    //return;
    for (auto hm : hyprmonitors) {
        if (hm->id != mon)
            continue;
        if (!hm->wallfb)
            continue;
        AnyPass::AnyData anydata([hm, mon, b, rounding](AnyPass* pass) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
             //notify("draw");
            auto roundingPower = 2.0f;
            auto cornermask = 0;
            auto tex = hm->wallfb->getTexture();
            auto box = tocbox(b);

            CHyprOpenGLImpl::STextureRenderData data;
            data.allowCustomUV = true;

            data.round = rounding;
            data.roundingPower = roundingPower;
            g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft     = Vector2D(0, 0);
            g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = Vector2D(
                std::min(1.0, 1.0),
                std::min(1.0, 1.0)
            );
            set_rounding(cornermask);
            g_pHyprOpenGL->renderTexture(tex, box, data);
            set_rounding(0);
            g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft     = Vector2D(-1, -1);
            g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = Vector2D(-1, -1);
        });
        g_pHyprRenderer->m_renderPass.add(makeUnique<AnyPass>(std::move(anydata)));
    }
};

void HyprIso::screenshot_wallpaper(int mon) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
   for (auto hm : hyprmonitors) {
       if (hm->id == mon) {
           if (!hm->wallfb)
               hm->wallfb = new CFramebuffer;
           actual_screenshot_wallpaper(hm->wallfb, hm->m);
           hm->wall_size = Bounds(hm->m->m_position.x, hm->m->m_position.y, 
               hm->m->m_pixelSize.x, hm->m->m_pixelSize.y);
       }
   }
}

void HyprIso::screenshot_space(int mon, int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    //notify("attempt screenshot " + std::to_string(id));
    for (auto hs : hyprspaces) {
        //notify("against " + std::to_string(hs->w->m_id));
        if (hs->w->m_id == id) {
            //notify("screenshot " + std::to_string(id));
            screenshot_workspace(hs->buffer, hs->w, hs->w->m_monitor.lock(), false);
        }
        // for (auto hm : hyprmonitors) {
        //     if (hs->w.lock() && hs->w->m_monitor == hm->m && mon == hm->id) {
        //         if (hs->w->m_id == id) {
        //             screenshot_workspace(hs->buffer, hs->w, hm->m, false);
        //             return;
        //         }
        //     }
        // }
    }
}

void HyprIso::screenshot_deco(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto w : g_pCompositor->m_windows) {
        for (auto hw : hyprwindows) {
            if (hw->w == w && hw->id == id) {
                if (!hw->deco_fb)
                    hw->deco_fb = new CFramebuffer;
                screenshot_window(hw, w, true);
            }
        }
    }
}



// Will stretch the thumbnail if the aspect ratio passed in is different from thumbnail
void HyprIso::draw_thumbnail(int id, Bounds b, int rounding, float roundingPower, int cornermask, float alpha) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
    // return;
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            if (hw->fb && hw->fb->isAllocated()) {
                bool clip = this->clip;
                Bounds clipbox = this->clipbox;
                AnyPass::AnyData anydata([id, b, hw, rounding, roundingPower, cornermask, alpha, clip, clipbox](AnyPass* pass) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
 
                    auto tex = hw->fb->getTexture();
                    auto box = tocbox(b);
                    CHyprOpenGLImpl::STextureRenderData data;
                    data.allowCustomUV = true;
                    data.round = rounding;
                    data.roundingPower = roundingPower;
                    data.a = alpha;
                    g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft     = Vector2D(0, 0);
                    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = Vector2D(
                        std::min(hw->w_size.w / hw->fb->m_size.x, 1.0), 
                        std::min(hw->w_size.h / hw->fb->m_size.y, 1.0) 
                    );
                    set_rounding(cornermask);
                    if (clip)
                        g_pHyprOpenGL->m_renderData.clipBox = tocbox(clipbox);
                    g_pHyprOpenGL->renderTexture(tex, box, data);
                    set_rounding(0);
                    g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft     = Vector2D(-1, -1);
                    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = Vector2D(-1, -1);
                    if (clip)
                        g_pHyprOpenGL->m_renderData.clipBox = tocbox(Bounds());
                });
                g_pHyprRenderer->m_renderPass.add(makeUnique<AnyPass>(std::move(anydata)));
            }
        }
    }
}

void HyprIso::draw_deco_thumbnail(int id, Bounds b, int rounding, float roundingPower, int cornermask) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // return;
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            if (hw->deco_fb && hw->deco_fb->isAllocated()) {
                AnyPass::AnyData anydata([id, b, hw, rounding, roundingPower, cornermask](AnyPass* pass) {
                    auto tex = hw->deco_fb->getTexture();
                    auto box = tocbox(b);
                    CHyprOpenGLImpl::STextureRenderData data;
                    data.allowCustomUV = true;
                    data.round = rounding;
                    data.roundingPower = roundingPower;
                    g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft     = Vector2D(0, 0);
                    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = Vector2D(
                        std::min(hw->w_decos_size.w / hw->deco_fb->m_size.x, 1.0), 
                        std::min(hw->w_decos_size.h / hw->deco_fb->m_size.y, 1.0) 
                    );
                    set_rounding(cornermask);
                    g_pHyprOpenGL->renderTexture(tex, box, data);
                    set_rounding(0);
                    g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft     = Vector2D(-1, -1);
                    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = Vector2D(-1, -1);
                });
                g_pHyprRenderer->m_renderPass.add(makeUnique<AnyPass>(std::move(anydata)));
            }
        }
    }
}

void HyprIso::draw_raw_deco_thumbnail(int id, Bounds b, int rounding, float roundingPower, int cornermask) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // return;
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            if (hw->deco_fb && hw->deco_fb->isAllocated()) {
                AnyPass::AnyData anydata([id, b, hw, rounding, roundingPower, cornermask](AnyPass* pass) {
                    auto tex = hw->deco_fb->getTexture();
                    auto box = tocbox(b);
                    CHyprOpenGLImpl::STextureRenderData data;
                    data.allowCustomUV = true;
                    data.round = rounding;
                    data.roundingPower = roundingPower;
                    //g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft     = Vector2D(0, 0);
                    //g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = Vector2D(
                        //std::min(hw->w_decos_size.w / hw->deco_fb->m_size.x, 1.0), 
                        //std::min(hw->w_decos_size.h / hw->deco_fb->m_size.y, 1.0) 
                    //);
                    set_rounding(cornermask);
                    g_pHyprOpenGL->renderTexture(tex, box, data);
                    set_rounding(0);
                    g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft     = Vector2D(-1, -1);
                    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = Vector2D(-1, -1);
                });
                g_pHyprRenderer->m_renderPass.add(makeUnique<AnyPass>(std::move(anydata)));
            }
        }
    }
}


void HyprIso::set_zoom_factor(float amount) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Hyprlang::CConfigValue* val = g_pConfigManager->getHyprlangConfigValuePtr("cursor:zoom_factor");
    auto zoom_amount = (Hyprlang::FLOAT*)val->dataPtr();
    *zoom_amount = amount;
    
    for (auto const& m : g_pCompositor->m_monitors) {
        *(m->m_cursorZoom) = amount;
        g_pLayoutManager->getCurrentLayout()->recalculateMonitor(m->m_id);
    }    
}

int HyprIso::parent(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            if (auto w = hw->w->parent()) {
                for (auto hw : hyprwindows) {
                    if (hw->w == w) {
                        return hw->id;
                    }
                }
            }            
        }
    }
    return -1;
}

void HyprIso::set_reserved_edge(int side, int amount) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    SMonitorAdditionalReservedArea value;
    if (side == (int) RESIZE_TYPE::TOP) {
        value.top = amount;
    } else if (side == (int) RESIZE_TYPE::LEFT) {
        value.left = amount;
    } else if (side == (int) RESIZE_TYPE::RIGHT) {
        value.right = amount;
    } else if (side == (int) RESIZE_TYPE::BOTTOM) {
        value.bottom = amount;
    }
    g_pConfigManager->m_mAdditionalReservedAreas["Mylardesktop"] = value;
}

void HyprIso::show_desktop() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        hypriso->set_hidden(hw->id, hw->was_hidden);
    }
    for (auto r : hyprmonitors) {
        hypriso->damage_entire(r->id);
    }
}

void HyprIso::hide_desktop() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        hw->was_hidden = hw->is_hidden;
        hypriso->set_hidden(hw->id, true);
    }
    for (auto r : hyprmonitors) {
        hypriso->damage_entire(r->id);
    }
}

static void updateRelativeCursorCoords() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    static auto PNOWARPS = CConfigValue<Hyprlang::INT>("cursor:no_warps");

    if (*PNOWARPS)
        return;

    if (g_pCompositor->m_lastWindow)
        g_pCompositor->m_lastWindow->m_relativeCursorCoordsOnLastWarp = g_pInputManager->getMouseCoordsInternal() - g_pCompositor->m_lastWindow->m_position;
}

void HyprIso::move_to_workspace(int workspace) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    g_pKeybindManager->changeworkspace(std::to_string(workspace));
}

void HyprIso::move_to_workspace(int id, int workspace) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    PHLWINDOW PWINDOW;
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            PWINDOW = hw->w;
            break;
        }
    }
    if (!PWINDOW.get())
        return;        
    std::string args = std::to_string(workspace);
    
    const auto& [WORKSPACEID, workspaceName] = getWorkspaceIDNameFromString(args);
    if (WORKSPACEID == WORKSPACE_INVALID) {
        Debug::log(LOG, "Invalid workspace in moveActiveToWorkspace");
        return;
    }

    if (WORKSPACEID == PWINDOW->workspaceID()) {
        Debug::log(LOG, "Not moving to workspace because it didn't change.");
        return;
    }

    auto        pWorkspace            = g_pCompositor->getWorkspaceByID(WORKSPACEID);
    PHLMONITOR  pMonitor              = nullptr;
    const auto  POLDWS                = PWINDOW->m_workspace;
    static auto PALLOWWORKSPACECYCLES = CConfigValue<Hyprlang::INT>("binds:allow_workspace_cycles");

    updateRelativeCursorCoords();

    g_pHyprRenderer->damageWindow(PWINDOW);

    if (pWorkspace) {
        const auto FULLSCREENMODE = PWINDOW->m_fullscreenState.internal;
        g_pCompositor->moveWindowToWorkspaceSafe(PWINDOW, pWorkspace);
        pMonitor = pWorkspace->m_monitor.lock();
        g_pCompositor->setActiveMonitor(pMonitor);
        g_pCompositor->setWindowFullscreenInternal(PWINDOW, FULLSCREENMODE);
    } else {
        pWorkspace = g_pCompositor->createNewWorkspace(WORKSPACEID, PWINDOW->monitorID(), workspaceName, false);
        pMonitor   = pWorkspace->m_monitor.lock();
        g_pCompositor->moveWindowToWorkspaceSafe(PWINDOW, pWorkspace);
    }

    POLDWS->m_lastFocusedWindow = POLDWS->getFirstWindow();

    if (pWorkspace->m_isSpecialWorkspace)
        pMonitor->setSpecialWorkspace(pWorkspace);
    else if (POLDWS->m_isSpecialWorkspace)
        POLDWS->m_monitor.lock()->setSpecialWorkspace(nullptr);

    if (*PALLOWWORKSPACECYCLES)
        pWorkspace->rememberPrevWorkspace(POLDWS);

    pMonitor->changeWorkspace(pWorkspace);

    g_pCompositor->focusWindow(PWINDOW);
    PWINDOW->warpCursor();
}

void HyprIso::reload() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    g_pConfigManager->reload(); 
}

void HyprIso::add_float_rule() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    g_pConfigManager->handleWindowRule("windowrulev2", "float, class:.*");
}

void HyprIso::overwrite_defaults() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    return;
    {
        Hyprlang::CConfigValue* val = g_pConfigManager->getHyprlangConfigValuePtr("decoration:blur:enabled");
        auto target = (Hyprlang::INT*)val->dataPtr();
        *target = 1;
    }
    {
        Hyprlang::CConfigValue* val = g_pConfigManager->getHyprlangConfigValuePtr("decoration:blur:size");
        auto target = (Hyprlang::INT*)val->dataPtr();
        *target= 13;
    }
    {
        Hyprlang::CConfigValue* val = g_pConfigManager->getHyprlangConfigValuePtr("decoration:blur:passes");
        auto target = (Hyprlang::INT*)val->dataPtr();
        *target= 3;
    }
    {
        Hyprlang::CConfigValue* val = g_pConfigManager->getHyprlangConfigValuePtr("decoration:blur:noise");
        auto target = (Hyprlang::FLOAT*)val->dataPtr();
        *target= .04;
    }
    {
        Hyprlang::CConfigValue* val = g_pConfigManager->getHyprlangConfigValuePtr("decoration:blur:vibrancy");
        auto target = (Hyprlang::FLOAT*)val->dataPtr();
        *target= .4696;
    }

 
    
    //g_pConfigManager->handleWindowRule("windowrulev2", "float, class:.*");
}

Bounds HyprIso::floating_offset(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            return {hw->w->m_floatingOffset.x, hw->w->m_floatingOffset.y, 0, 0};
        }
    }

    return {};
}
Bounds HyprIso::workspace_offset(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            if (hw->w->m_workspace) {
                if (!hw->w->m_pinned) {
                    auto off = hw->w->m_workspace->m_renderOffset->value();
                    return {off.x, off.y, 0, 0};
                }
            }
        }
    }
    return {};
}

bool HyprIso::has_focus(int client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw: hyprwindows) {
        if (hw->id == client) {
            return hw->w == g_pCompositor->m_lastWindow;
        }
    }
    return false;
}

PHLWINDOW vectorToWindowUnified(const Vector2D& pos, uint8_t properties, PHLWINDOW pIgnoreWindow) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    const auto  PMONITOR          = g_pCompositor->getMonitorFromVector(pos);
    static auto PRESIZEONBORDER   = CConfigValue<Hyprlang::INT>("general:resize_on_border");
    static auto PBORDERSIZE       = CConfigValue<Hyprlang::INT>("general:border_size");
    static auto PBORDERGRABEXTEND = CConfigValue<Hyprlang::INT>("general:extend_border_grab_area");
    static auto PSPECIALFALLTHRU  = CConfigValue<Hyprlang::INT>("input:special_fallthrough");
    const auto  BORDER_GRAB_AREA  = *PRESIZEONBORDER ? *PBORDERSIZE + *PBORDERGRABEXTEND : 0;
    const bool  ONLY_PRIORITY     = properties & FOCUS_PRIORITY;

    // pinned windows on top of floating regardless
    if (properties & ALLOW_FLOATING) {
        for (auto const& w : g_pCompositor->m_windows | std::views::reverse) {
            if (ONLY_PRIORITY && !w->priorityFocus())
                continue;

            if (w->m_isFloating && w->m_isMapped && !w->isHidden() && !w->m_X11ShouldntFocus && w->m_pinned && !w->m_windowData.noFocus.valueOrDefault() && w != pIgnoreWindow) {
                const auto BB  = w->getWindowBoxUnified(properties);
                CBox       box = BB.copy().expand(!w->isX11OverrideRedirect() ? BORDER_GRAB_AREA : 0);
                if (box.containsPoint(g_pPointerManager->position()))
                    return w;

                if (!w->m_isX11) {
                    if (w->hasPopupAt(pos))
                        return w;
                }
            }
        }
    }

    auto windowForWorkspace = [&](bool special) -> PHLWINDOW {
        auto floating = [&](bool aboveFullscreen) -> PHLWINDOW {
            for (auto const& w : g_pCompositor->m_windows | std::views::reverse) {

                if (special && !w->onSpecialWorkspace()) // because special floating may creep up into regular
                    continue;

                if (!w->m_workspace)
                    continue;

                if (ONLY_PRIORITY && !w->priorityFocus())
                    continue;

                const auto PWINDOWMONITOR = w->m_monitor.lock();

                // to avoid focusing windows behind special workspaces from other monitors
                if (!*PSPECIALFALLTHRU && PWINDOWMONITOR && PWINDOWMONITOR->m_activeSpecialWorkspace && w->m_workspace != PWINDOWMONITOR->m_activeSpecialWorkspace) {
                    const auto BB = w->getWindowBoxUnified(properties);
                    if (BB.x >= PWINDOWMONITOR->m_position.x && BB.y >= PWINDOWMONITOR->m_position.y &&
                        BB.x + BB.width <= PWINDOWMONITOR->m_position.x + PWINDOWMONITOR->m_size.x && BB.y + BB.height <= PWINDOWMONITOR->m_position.y + PWINDOWMONITOR->m_size.y)
                        continue;
                }

                if (w->m_isMapped && w->m_workspace->isVisible() && !w->isHidden() && !w->m_windowData.noFocus.valueOrDefault() &&
+                    w != pIgnoreWindow) {
                    // OR windows should add focus to parent
                    if (w->m_X11ShouldntFocus && !w->isX11OverrideRedirect())
                        continue;

                    const auto BB  = w->getWindowBoxUnified(properties);
                    CBox       box = BB.copy().expand(!w->isX11OverrideRedirect() ? BORDER_GRAB_AREA : 0);
                    if (box.containsPoint(g_pPointerManager->position())) {

                        if (w->m_isX11 && w->isX11OverrideRedirect() && !w->m_xwaylandSurface->wantsFocus()) {
                            // Override Redirect
                            return g_pCompositor->m_lastWindow.lock(); // we kinda trick everything here.
                            // TODO: this is wrong, we should focus the parent, but idk how to get it considering it's nullptr in most cases.
                        }

                        return w;
                    }

                    if (!w->m_isX11) {
                        if (w->hasPopupAt(pos))
                            return w;
                    }
                }
            }

            return nullptr;
        };

        if (properties & ALLOW_FLOATING) {
            // first loop over floating cuz they're above, m_lWindows should be sorted bottom->top, for tiled it doesn't matter.
            auto found = floating(true);
            if (found)
                return found;
        }

        if (properties & FLOATING_ONLY) {
            //return floating(false);
            return nullptr;
        }

        const WORKSPACEID WSPID      = special ? PMONITOR->activeSpecialWorkspaceID() : PMONITOR->activeWorkspaceID();
        const auto        PWORKSPACE = g_pCompositor->getWorkspaceByID(WSPID);

        if (PWORKSPACE->m_hasFullscreenWindow && !(properties & SKIP_FULLSCREEN_PRIORITY) && !ONLY_PRIORITY)
            return PWORKSPACE->getFullscreenWindow();

        auto found = floating(false);
        if (found)
            return found;

        // for windows, we need to check their extensions too, first.
        for (auto const& w : g_pCompositor->m_windows) {
            if (ONLY_PRIORITY && !w->priorityFocus())
                continue;

            if (special != w->onSpecialWorkspace())
                continue;

            if (!w->m_workspace)
                continue;

            if (!w->m_isX11 && !w->m_isFloating && w->m_isMapped && w->workspaceID() == WSPID && !w->isHidden() && !w->m_X11ShouldntFocus &&
                !w->m_windowData.noFocus.valueOrDefault() && w != pIgnoreWindow) {
                if (w->hasPopupAt(pos))
                    return w;
            }
        }

        for (auto const& w : g_pCompositor->m_windows) {
            if (ONLY_PRIORITY && !w->priorityFocus())
                continue;

            if (special != w->onSpecialWorkspace())
                continue;

            if (!w->m_workspace)
                continue;

            if (!w->m_isFloating && w->m_isMapped && w->workspaceID() == WSPID && !w->isHidden() && !w->m_X11ShouldntFocus && !w->m_windowData.noFocus.valueOrDefault() &&
                w != pIgnoreWindow) {
                CBox box = (properties & USE_PROP_TILED) ? w->getWindowBoxUnified(properties) : CBox{w->m_position, w->m_size};
                if (box.containsPoint(pos))
                    return w;
            }
        }

        return nullptr;
    };

    // special workspace
    if (PMONITOR->m_activeSpecialWorkspace && !*PSPECIALFALLTHRU)
        return windowForWorkspace(true);

    if (PMONITOR->m_activeSpecialWorkspace) {
        const auto PWINDOW = windowForWorkspace(true);

        if (PWINDOW)
            return PWINDOW;
    }

    return windowForWorkspace(false);
}


void mouseMoveUnified(uint32_t time, bool refocus, bool mouse, std::optional<Vector2D> overridePos) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    g_pInputManager->m_lastInputMouse = mouse;

    if (!g_pCompositor->m_readyToProcess || g_pCompositor->m_isShuttingDown || g_pCompositor->m_unsafeState)
        return;

    Vector2D const mouseCoords        = overridePos.value_or(g_pInputManager->getMouseCoordsInternal());
    auto const     MOUSECOORDSFLOORED = mouseCoords.floor();

    if (MOUSECOORDSFLOORED == g_pInputManager->m_lastCursorPosFloored && !refocus)
        return;

    static auto PFOLLOWMOUSE          = CConfigValue<Hyprlang::INT>("input:follow_mouse");
    static auto PFOLLOWMOUSETHRESHOLD = CConfigValue<Hyprlang::FLOAT>("input:follow_mouse_threshold");
    static auto PMOUSEREFOCUS         = CConfigValue<Hyprlang::INT>("input:mouse_refocus");
    static auto PFOLLOWONDND          = CConfigValue<Hyprlang::INT>("misc:always_follow_on_dnd");
    static auto PFLOATBEHAVIOR        = CConfigValue<Hyprlang::INT>("input:float_switch_override_focus");
    static auto PMOUSEFOCUSMON        = CConfigValue<Hyprlang::INT>("misc:mouse_move_focuses_monitor");
    static auto PRESIZEONBORDER       = CConfigValue<Hyprlang::INT>("general:resize_on_border");
    static auto PRESIZECURSORICON     = CConfigValue<Hyprlang::INT>("general:hover_icon_on_border");

//CWLDataDeviceProtocol

    const auto  FOLLOWMOUSE = *PFOLLOWONDND && PROTO::data->dndActive() ? 1 : *PFOLLOWMOUSE;

    if (FOLLOWMOUSE == 1 && g_pInputManager->m_lastCursorMovement.getSeconds() < 0.5)
        g_pInputManager->m_mousePosDelta += MOUSECOORDSFLOORED.distance(g_pInputManager->m_lastCursorPosFloored);
    else
        g_pInputManager->m_mousePosDelta = 0;

    g_pInputManager->m_foundSurfaceToFocus.reset();
    g_pInputManager->m_foundLSToFocus.reset();
    g_pInputManager->m_foundWindowToFocus.reset();
    SP<CWLSurfaceResource> foundSurface;
    Vector2D               surfaceCoords;
    Vector2D               surfacePos = Vector2D(-1337, -1337);
    PHLWINDOW              pFoundWindow;
    PHLLS                  pFoundLayerSurface;

    EMIT_HOOK_EVENT_CANCELLABLE("mouseMove", MOUSECOORDSFLOORED);

    g_pInputManager->m_lastCursorPosFloored = MOUSECOORDSFLOORED;

    const auto PMONITOR = g_pInputManager->isLocked() && g_pCompositor->m_lastMonitor ? g_pCompositor->m_lastMonitor.lock() : g_pCompositor->getMonitorFromCursor();

    // this can happen if there are no displays hooked up to Hyprland
    if (PMONITOR == nullptr)
        return;

    if (PMONITOR->m_cursorZoom->value() != 1.f)
        g_pHyprRenderer->damageMonitor(PMONITOR);

    bool skipFrameSchedule = PMONITOR->shouldSkipScheduleFrameOnMouseEvent();

    if (!PMONITOR->m_solitaryClient.lock() && g_pHyprRenderer->shouldRenderCursor() && g_pPointerManager->softwareLockedFor(PMONITOR->m_self.lock()) && !skipFrameSchedule)
        g_pCompositor->scheduleFrameForMonitor(PMONITOR, Aquamarine::IOutput::AQ_SCHEDULE_CURSOR_MOVE);

    // constraints
    if (!g_pSeatManager->m_mouse.expired() && g_pInputManager->isConstrained()) {
        const auto SURF       = CWLSurface::fromResource(g_pCompositor->m_lastFocus.lock());
        const auto CONSTRAINT = SURF ? SURF->constraint() : nullptr;

        if (CONSTRAINT) {
            if (CONSTRAINT->isLocked()) {
                const auto HINT = CONSTRAINT->logicPositionHint();
                g_pCompositor->warpCursorTo(HINT, true);
            } else {
                const auto RG           = CONSTRAINT->logicConstraintRegion();
                const auto CLOSEST      = RG.closestPoint(mouseCoords);
                const auto BOX          = SURF->getSurfaceBoxGlobal();
                const auto CLOSESTLOCAL = (CLOSEST - (BOX.has_value() ? BOX->pos() : Vector2D{})) * (SURF->getWindow() ? SURF->getWindow()->m_X11SurfaceScaledBy : 1.0);

                g_pCompositor->warpCursorTo(CLOSEST, true);
                g_pSeatManager->sendPointerMotion(time, CLOSESTLOCAL);
                PROTO::relativePointer->sendRelativeMotion(sc<uint64_t>(time) * 1000, {}, {});
            }

            return;

        } else
            Debug::log(ERR, "BUG THIS: Null SURF/CONSTRAINT in mouse refocus. Ignoring constraints. {:x} {:x}", rc<uintptr_t>(SURF.get()), rc<uintptr_t>(CONSTRAINT.get()));
    }

    if (PMONITOR != g_pCompositor->m_lastMonitor && (*PMOUSEFOCUSMON || refocus) && g_pInputManager->m_forcedFocus.expired())
        g_pCompositor->setActiveMonitor(PMONITOR);

    // check for windows that have focus priority like our permission popups
    pFoundWindow = g_pCompositor->vectorToWindowUnified(mouseCoords, FOCUS_PRIORITY);
    if (pFoundWindow)
        foundSurface = g_pCompositor->vectorWindowToSurface(mouseCoords, pFoundWindow, surfaceCoords);

    if (!foundSurface && g_pSessionLockManager->isSessionLocked()) {

        // set keyboard focus on session lock surface regardless of layers
        const auto PSESSIONLOCKSURFACE = g_pSessionLockManager->getSessionLockSurfaceForMonitor(PMONITOR->m_id);
        const auto foundLockSurface    = PSESSIONLOCKSURFACE ? PSESSIONLOCKSURFACE->surface->surface() : nullptr;

        g_pCompositor->focusSurface(foundLockSurface);

        // search for interactable abovelock surfaces for pointer focus, or use session lock surface if not found
        for (auto& lsl : PMONITOR->m_layerSurfaceLayers | std::views::reverse) {
            foundSurface = g_pCompositor->vectorToLayerSurface(mouseCoords, &lsl, &surfaceCoords, &pFoundLayerSurface, true);

            if (foundSurface)
                break;
        }

        if (!foundSurface) {
            surfaceCoords = mouseCoords - PMONITOR->m_position;
            foundSurface  = foundLockSurface;
        }

        if (refocus) {
            g_pInputManager->m_foundLSToFocus      = pFoundLayerSurface;
            g_pInputManager->m_foundWindowToFocus  = pFoundWindow;
            g_pInputManager->m_foundSurfaceToFocus = foundSurface;
        }

        g_pSeatManager->setPointerFocus(foundSurface, surfaceCoords);
        g_pSeatManager->sendPointerMotion(time, surfaceCoords);

        return;
    }

    PHLWINDOW forcedFocus = g_pInputManager->m_forcedFocus.lock();

    if (!forcedFocus)
        forcedFocus = g_pCompositor->getForceFocus();

    if (forcedFocus && !foundSurface) {
        pFoundWindow = forcedFocus;
        surfacePos   = pFoundWindow->m_realPosition->value();
        foundSurface = pFoundWindow->m_wlSurface->resource();
    }

    // if we are holding a pointer button,
    // and we're not dnd-ing, don't refocus. Keep focus on last surface.
    if (!PROTO::data->dndActive() && !g_pInputManager->m_currentlyHeldButtons.empty() && g_pCompositor->m_lastFocus && g_pCompositor->m_lastFocus->m_mapped &&
        g_pSeatManager->m_state.pointerFocus && !g_pInputManager->m_hardInput) {
        foundSurface = g_pSeatManager->m_state.pointerFocus.lock();

        // IME popups aren't desktop-like elements
        // TODO: make them.
        CInputPopup* foundPopup = g_pInputManager->m_relay.popupFromSurface(foundSurface);
        if (foundPopup) {
            surfacePos             = foundPopup->globalBox().pos();
            g_pInputManager->m_focusHeldByButtons   = true;
            g_pInputManager->m_refocusHeldByButtons = refocus;
        } else {
            auto HLSurface = CWLSurface::fromResource(foundSurface);

            if (HLSurface) {
                const auto BOX = HLSurface->getSurfaceBoxGlobal();

                if (BOX) {
                    const auto PWINDOW = HLSurface->getWindow();
                    surfacePos         = BOX->pos();
                    pFoundLayerSurface = HLSurface->getLayer();
                    if (!pFoundLayerSurface)
                        pFoundWindow = !PWINDOW || PWINDOW->isHidden() ? g_pCompositor->m_lastWindow.lock() : PWINDOW;
                } else // reset foundSurface, find one normally
                    foundSurface = nullptr;
            } else // reset foundSurface, find one normally
                foundSurface = nullptr;
        }
    }

    g_pLayoutManager->getCurrentLayout()->onMouseMove(g_pInputManager->getMouseCoordsInternal());

    // forced above all
    if (!g_pInputManager->m_exclusiveLSes.empty()) {
        if (!foundSurface)
            foundSurface = g_pCompositor->vectorToLayerSurface(mouseCoords, &g_pInputManager->m_exclusiveLSes, &surfaceCoords, &pFoundLayerSurface);

        if (!foundSurface) {
            foundSurface = (*g_pInputManager->m_exclusiveLSes.begin())->m_surface->resource();
            surfacePos   = (*g_pInputManager->m_exclusiveLSes.begin())->m_realPosition->goal();
        }
    }

    if (!foundSurface)
        foundSurface = g_pCompositor->vectorToLayerPopupSurface(mouseCoords, PMONITOR, &surfaceCoords, &pFoundLayerSurface);

    // overlays are above fullscreen
    if (!foundSurface)
        foundSurface = g_pCompositor->vectorToLayerSurface(mouseCoords, &PMONITOR->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], &surfaceCoords, &pFoundLayerSurface);

    // also IME popups
    if (!foundSurface) {
        auto popup = g_pInputManager->m_relay.popupFromCoords(mouseCoords);
        if (popup) {
            foundSurface = popup->getSurface();
            surfacePos   = popup->globalBox().pos();
        }
    }

    // also top layers
    if (!foundSurface)
        foundSurface = g_pCompositor->vectorToLayerSurface(mouseCoords, &PMONITOR->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_TOP], &surfaceCoords, &pFoundLayerSurface);

    // then, we check if the workspace doesn't have a fullscreen window
    const auto PWORKSPACE   = PMONITOR->m_activeSpecialWorkspace ? PMONITOR->m_activeSpecialWorkspace : PMONITOR->m_activeWorkspace;
    const auto PWINDOWIDEAL = g_pCompositor->vectorToWindowUnified(mouseCoords, RESERVED_EXTENTS | INPUT_EXTENTS | ALLOW_FLOATING);
    if (PWORKSPACE->m_hasFullscreenWindow && !foundSurface && PWORKSPACE->m_fullscreenMode == FSMODE_FULLSCREEN) {
        pFoundWindow = PWORKSPACE->getFullscreenWindow();

        if (!pFoundWindow) {
            // what the fuck, somehow happens occasionally??
            PWORKSPACE->m_hasFullscreenWindow = false;
            return;
        }

        if (PWINDOWIDEAL &&
            ((PWINDOWIDEAL->m_isFloating && (PWINDOWIDEAL->m_createdOverFullscreen || PWINDOWIDEAL->m_pinned)) /* floating over fullscreen or pinned */
             || (PMONITOR->m_activeSpecialWorkspace == PWINDOWIDEAL->m_workspace) /* on an open special workspace */))
            pFoundWindow = PWINDOWIDEAL;

        if (!pFoundWindow->m_isX11) {
            foundSurface = g_pCompositor->vectorWindowToSurface(mouseCoords, pFoundWindow, surfaceCoords);
            surfacePos   = Vector2D(-1337, -1337);
        } else {
            foundSurface = pFoundWindow->m_wlSurface->resource();
            surfacePos   = pFoundWindow->m_realPosition->value();
        }
    }

    // then windows
    if (!foundSurface) {
        if (PWORKSPACE->m_hasFullscreenWindow && PWORKSPACE->m_fullscreenMode == FSMODE_MAXIMIZED) {
            if (!foundSurface) {
                if (PMONITOR->m_activeSpecialWorkspace) {
                    if (pFoundWindow != PWINDOWIDEAL)
                        pFoundWindow = g_pCompositor->vectorToWindowUnified(mouseCoords, RESERVED_EXTENTS | INPUT_EXTENTS | ALLOW_FLOATING);

                    if (pFoundWindow && !pFoundWindow->onSpecialWorkspace()) {
                        pFoundWindow = PWORKSPACE->getFullscreenWindow();
                    }
                } else {
                    // if we have a maximized window, allow focusing on a bar or something if in reserved area.
                    if (g_pCompositor->isPointOnReservedArea(mouseCoords, PMONITOR)) {
                        foundSurface = g_pCompositor->vectorToLayerSurface(mouseCoords, &PMONITOR->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM], &surfaceCoords,
                                                                           &pFoundLayerSurface);
                    }

                    if (!foundSurface) {
                        if (pFoundWindow != PWINDOWIDEAL)
                            pFoundWindow = g_pCompositor->vectorToWindowUnified(mouseCoords, RESERVED_EXTENTS | INPUT_EXTENTS | ALLOW_FLOATING);

                        if (!(pFoundWindow && (pFoundWindow->m_isFloating && (pFoundWindow->m_createdOverFullscreen || pFoundWindow->m_pinned))))
                            pFoundWindow = PWORKSPACE->getFullscreenWindow();
                    }
                }
            }

        } else {
            if (pFoundWindow != PWINDOWIDEAL)
                pFoundWindow = g_pCompositor->vectorToWindowUnified(mouseCoords, RESERVED_EXTENTS | INPUT_EXTENTS | ALLOW_FLOATING);
        }

        if (pFoundWindow) {
            if (!pFoundWindow->m_isX11) {
                foundSurface = g_pCompositor->vectorWindowToSurface(mouseCoords, pFoundWindow, surfaceCoords);
                if (!foundSurface) {
                    foundSurface = pFoundWindow->m_wlSurface->resource();
                    surfacePos   = pFoundWindow->m_realPosition->value();
                }
            } else {
                foundSurface = pFoundWindow->m_wlSurface->resource();
                surfacePos   = pFoundWindow->m_realPosition->value();
            }
        }
    }

    // then surfaces below
    if (!foundSurface)
        foundSurface = g_pCompositor->vectorToLayerSurface(mouseCoords, &PMONITOR->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM], &surfaceCoords, &pFoundLayerSurface);

    if (!foundSurface)
        foundSurface = g_pCompositor->vectorToLayerSurface(mouseCoords, &PMONITOR->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND], &surfaceCoords, &pFoundLayerSurface);

    if (g_pPointerManager->softwareLockedFor(PMONITOR->m_self.lock()) > 0 && !skipFrameSchedule)
        g_pCompositor->scheduleFrameForMonitor(g_pCompositor->m_lastMonitor.lock(), Aquamarine::IOutput::AQ_SCHEDULE_CURSOR_MOVE);

    // FIXME: This will be disabled during DnD operations because we do not exactly follow the spec
    // xdg-popup grabs should be keyboard-only, while they are absolute in our case...
    if (g_pSeatManager->m_seatGrab && !g_pSeatManager->m_seatGrab->accepts(foundSurface) && !PROTO::data->dndActive()) {
        if (g_pInputManager->m_hardInput || refocus) {
            g_pSeatManager->setGrab(nullptr);
            return; // setGrab will refocus
        } else {
            // we need to grab the last surface.
            foundSurface = g_pSeatManager->m_state.pointerFocus.lock();

            auto HLSurface = CWLSurface::fromResource(foundSurface);

            if (HLSurface) {
                const auto BOX = HLSurface->getSurfaceBoxGlobal();

                if (BOX.has_value())
                    surfacePos = BOX->pos();
            }
        }
    }

    if (!foundSurface) {
        if (!g_pInputManager->m_emptyFocusCursorSet) {
            if (*PRESIZEONBORDER && *PRESIZECURSORICON && g_pInputManager->m_borderIconDirection != BORDERICON_NONE) {
                g_pInputManager->m_borderIconDirection = BORDERICON_NONE;
                unsetCursorImage();
            }

            // TODO: maybe wrap?
            if (g_pInputManager->m_clickBehavior == CLICKMODE_KILL)
                g_pInputManager->setCursorImageOverride("crosshair");
            else
                g_pInputManager->setCursorImageOverride("left_ptr");

            g_pInputManager->m_emptyFocusCursorSet = true;
        }

        g_pSeatManager->setPointerFocus(nullptr, {});

        if (refocus || g_pCompositor->m_lastWindow.expired()) // if we are forcing a refocus, and we don't find a surface, clear the kb focus too!
            g_pCompositor->focusWindow(nullptr);

        return;
    }

    g_pInputManager->m_emptyFocusCursorSet = false;

    Vector2D surfaceLocal = surfacePos == Vector2D(-1337, -1337) ? surfaceCoords : mouseCoords - surfacePos;

    if (pFoundWindow && !pFoundWindow->m_isX11 && surfacePos != Vector2D(-1337, -1337)) {
        // calc for oversized windows... fucking bullshit.
        CBox geom = pFoundWindow->m_xdgSurface->m_current.geometry;

        surfaceLocal = mouseCoords - surfacePos + geom.pos();
    }

    if (pFoundWindow && pFoundWindow->m_isX11) // for x11 force scale zero
        surfaceLocal = surfaceLocal * pFoundWindow->m_X11SurfaceScaledBy;

    bool allowKeyboardRefocus = true;

    if (!refocus && g_pCompositor->m_lastFocus) {
        const auto PLS = g_pCompositor->getLayerSurfaceFromSurface(g_pCompositor->m_lastFocus.lock());

        if (PLS && PLS->m_layerSurface->m_current.interactivity == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE)
            allowKeyboardRefocus = false;
    }

    // set the values for use
    if (refocus) {
        g_pInputManager->m_foundLSToFocus      = pFoundLayerSurface;
        g_pInputManager->m_foundWindowToFocus  = pFoundWindow;
        g_pInputManager->m_foundSurfaceToFocus = foundSurface;
    }

    if (g_pInputManager->m_currentlyDraggedWindow.lock() && pFoundWindow != g_pInputManager->m_currentlyDraggedWindow) {
        g_pSeatManager->setPointerFocus(foundSurface, surfaceLocal);
        return;
    }

    if (pFoundWindow && foundSurface == pFoundWindow->m_wlSurface->resource() && !g_pInputManager->m_cursorImageOverridden) {
        const auto BOX = pFoundWindow->getWindowMainSurfaceBox();
        if (VECNOTINRECT(mouseCoords, BOX.x, BOX.y, BOX.x + BOX.width, BOX.y + BOX.height))
            g_pInputManager->setCursorImageOverride("left_ptr");
        else
            g_pInputManager->restoreCursorIconToApp();
    }

    if (pFoundWindow) {
        // change cursor icon if hovering over border
        if (*PRESIZEONBORDER && *PRESIZECURSORICON) {
            if (!pFoundWindow->isFullscreen() && !pFoundWindow->hasPopupAt(mouseCoords)) {
                g_pInputManager->setCursorIconOnBorder(pFoundWindow);
            } else if (g_pInputManager->m_borderIconDirection != BORDERICON_NONE) {
                unsetCursorImage();
            }
        }

        if (FOLLOWMOUSE != 1 && !refocus) {
            if (pFoundWindow != g_pCompositor->m_lastWindow.lock() && g_pCompositor->m_lastWindow.lock() &&
                ((pFoundWindow->m_isFloating && *PFLOATBEHAVIOR == 2) || (g_pCompositor->m_lastWindow->m_isFloating != pFoundWindow->m_isFloating && *PFLOATBEHAVIOR != 0))) {
                // enter if change floating style
                if (FOLLOWMOUSE != 3 && allowKeyboardRefocus)
                    g_pCompositor->focusWindow(pFoundWindow, foundSurface);
                g_pSeatManager->setPointerFocus(foundSurface, surfaceLocal);
            } else if (FOLLOWMOUSE == 2 || FOLLOWMOUSE == 3)
                g_pSeatManager->setPointerFocus(foundSurface, surfaceLocal);

            if (pFoundWindow == g_pCompositor->m_lastWindow)
                g_pSeatManager->setPointerFocus(foundSurface, surfaceLocal);

            if (FOLLOWMOUSE != 0 || pFoundWindow == g_pCompositor->m_lastWindow)
                g_pSeatManager->setPointerFocus(foundSurface, surfaceLocal);

            if (g_pSeatManager->m_state.pointerFocus == foundSurface)
                g_pSeatManager->sendPointerMotion(time, surfaceLocal);

            g_pInputManager->m_lastFocusOnLS = false;
            return; // don't enter any new surfaces
        } else {
            if (time == 0 && refocus) {
                g_pInputManager->m_lastMouseFocus = pFoundWindow;

                // TODO: this looks wrong. When over a popup, it constantly is switching.
                // Temp fix until that's figured out. Otherwise spams windowrule lookups and other shit.
                if (g_pInputManager->m_lastMouseFocus.lock() != pFoundWindow || g_pCompositor->m_lastWindow.lock() != pFoundWindow) {
                    if (g_pInputManager->m_mousePosDelta > *PFOLLOWMOUSETHRESHOLD || refocus) {
                        const bool hasNoFollowMouse = pFoundWindow && pFoundWindow->m_windowData.noFollowMouse.valueOrDefault();

                        if (refocus || !hasNoFollowMouse)
                            g_pCompositor->focusWindow(pFoundWindow, foundSurface);
                    }
                } else
                    g_pCompositor->focusSurface(foundSurface, pFoundWindow);
            }
            /*if (allowKeyboardRefocus && ((FOLLOWMOUSE != 3 && (*PMOUSEREFOCUS || m_lastMouseFocus.lock() != pFoundWindow)) || refocus)) {
                if (m_lastMouseFocus.lock() != pFoundWindow || g_pCompositor->m_lastWindow.lock() != pFoundWindow || g_pCompositor->m_lastFocus != foundSurface || refocus) {
                    m_lastMouseFocus = pFoundWindow;

                    // TODO: this looks wrong. When over a popup, it constantly is switching.
                    // Temp fix until that's figured out. Otherwise spams windowrule lookups and other shit.
                    if (m_lastMouseFocus.lock() != pFoundWindow || g_pCompositor->m_lastWindow.lock() != pFoundWindow) {
                        if (m_mousePosDelta > *PFOLLOWMOUSETHRESHOLD || refocus) {
                            const bool hasNoFollowMouse = pFoundWindow && pFoundWindow->m_windowData.noFollowMouse.valueOrDefault();

                            if (refocus || !hasNoFollowMouse)
                                g_pCompositor->focusWindow(pFoundWindow, foundSurface);
                        }
                    } else
                        g_pCompositor->focusSurface(foundSurface, pFoundWindow);
                }
            }*/
        }

        if (g_pSeatManager->m_state.keyboardFocus == nullptr)
            g_pCompositor->focusWindow(pFoundWindow, foundSurface);

        g_pInputManager->m_lastFocusOnLS = false;
    } else {
        if (*PRESIZEONBORDER && *PRESIZECURSORICON && g_pInputManager->m_borderIconDirection != BORDERICON_NONE) {
            g_pInputManager->m_borderIconDirection = BORDERICON_NONE;
            unsetCursorImage();
        }

        if (pFoundLayerSurface && (pFoundLayerSurface->m_layerSurface->m_current.interactivity != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) && FOLLOWMOUSE != 3 &&
            (allowKeyboardRefocus || pFoundLayerSurface->m_layerSurface->m_current.interactivity == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE)) {
            g_pCompositor->focusSurface(foundSurface);
        }

        if (pFoundLayerSurface)
            g_pInputManager->m_lastFocusOnLS = true;
    }

    g_pSeatManager->setPointerFocus(foundSurface, surfaceLocal);
    g_pSeatManager->sendPointerMotion(time, surfaceLocal);
}

void renderWorkspaceWindows(PHLMONITOR pMonitor, PHLWORKSPACE pWorkspace, const Time::steady_tp& time) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    PHLWINDOW lastWindow;

    EMIT_HOOK_EVENT("render", RENDER_PRE_WINDOWS);

    std::vector<PHLWINDOWREF> windows, fadingOut, pinned;
    windows.reserve(g_pCompositor->m_windows.size());

    // collect renderable windows
    for (auto const& w : g_pCompositor->m_windows) {
        if (w->isHidden() || (!w->m_isMapped && !w->m_fadingOut))
            continue;
        if (!g_pHyprRenderer->shouldRenderWindow(w, pMonitor))
            continue;

        windows.emplace_back(w);
    }

    // categorize + interleave
    for (auto& wref : windows) {
        auto w = wref.lock();
        if (!w)
            continue;

        // pinned go to separate pass (still above everything)
        if (w->m_pinned) {
            pinned.emplace_back(w);
            continue;
        }

        // some things may force us to ignore the special/not special disparity
        const bool IGNORE_SPECIAL_CHECK = w->m_monitorMovedFrom != -1 &&
                                          (w->m_workspace && !w->m_workspace->isVisible());

        if (!IGNORE_SPECIAL_CHECK && pWorkspace->m_isSpecialWorkspace != w->onSpecialWorkspace())
            continue;

        if (pWorkspace->m_isSpecialWorkspace && w->m_monitor != pWorkspace->m_monitor)
            continue; // special on another monitor drawn elsewhere

        // last window drawn after others
        if (w == g_pCompositor->m_lastWindow) {
            lastWindow = w;
            continue;
        }

        if (w->m_fadingOut) {
            fadingOut.emplace_back(w);
            continue;
        }

        // main pass (interleaved tiled/floating)
        g_pHyprRenderer->renderWindow(w, pMonitor, time, true, RENDER_PASS_MAIN);

        // popup directly after main
        g_pHyprRenderer->renderWindow(w, pMonitor, time, true, RENDER_PASS_POPUP);
    }

    // render last focused window after the rest
    if (lastWindow) {
        g_pHyprRenderer->renderWindow(lastWindow, pMonitor, time, true, RENDER_PASS_MAIN);
        g_pHyprRenderer->renderWindow(lastWindow, pMonitor, time, true, RENDER_PASS_POPUP);
    }

    // fading out (tiled or floating) — after main windows
    for (auto& wref : fadingOut) {
        auto w = wref.lock();
        if (w)
            g_pHyprRenderer->renderWindow(w, pMonitor, time, true, RENDER_PASS_MAIN);
    }

    // pinned last, above everything
    for (auto& wref : pinned) {
        auto w = wref.lock();
        if (w)
            g_pHyprRenderer->renderWindow(w, pMonitor, time, true, RENDER_PASS_ALL);
    }
}

// for interleaving tiled and floating windows
//void CInputManager::mouseMoveUnified(uint32_t time, bool refocus, bool mouse, std::optional<Vector2D> overridePos) {
//PHLWINDOW CCompositor::vectorToWindowUnified(const Vector2D& pos, uint8_t properties, PHLWINDOW pIgnoreWindow) {
//void CHyprRenderer::renderWorkspaceWindows(PHLMONITOR pMonitor, PHLWORKSPACE pWorkspace, const Time::steady_tp& time) {

inline CFunctionHook* g_pOnRenderWorkspaceWindows = nullptr;
typedef void (*origRenderWorkspaceWindows)(CHyprRenderer *, PHLMONITOR pMonitor, PHLWORKSPACE pWorkspace, const Time::steady_tp& time);
void hook_onRenderWorkspaceWindows(void* thisptr,  PHLMONITOR pMonitor, PHLWORKSPACE pWorkspace, const Time::steady_tp& time) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    //auto chr = (CHyprRenderer *) thisptr;
    //(*(origRenderWorkspaceWindows)g_pOnRenderWorkspaceWindows->m_original)(chr, pMonitor, pWorkspace, time);
    renderWorkspaceWindows(pMonitor, pWorkspace, time);
}

inline CFunctionHook* g_pOnVectorToWindowUnified = nullptr;
typedef void (*origVectorToWindowUnified)(CCompositor *, const Vector2D& pos, uint8_t properties, PHLWINDOW pIgnoreWindow);
PHLWINDOW hook_onVectorToWindowUnified(void* thisptr, const Vector2D& pos, uint8_t properties, PHLWINDOW pIgnoreWindow) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    return vectorToWindowUnified(pos, properties, pIgnoreWindow);
}

inline CFunctionHook* g_pOnMouseMoveUnified = nullptr;
typedef void (*origMouseMoveUnifiedd)(CInputManager *, uint32_t time, bool refocus, bool mouse, std::optional<Vector2D> overridePos);
void hook_onMouseMoveUnified(void* thisptr, uint32_t time, bool refocus, bool mouse, std::optional<Vector2D> overridePos) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    mouseMoveUnified(time, refocus, mouse, overridePos);
}

void interleave_floating_and_tiled_windows() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    return;
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "renderWorkspaceWindows");
        for (auto m : METHODS) {
            if (m.signature.find("_ZN13CHyprRenderer22renderWorkspaceWindowsEN9Hyprutils6Memory14CSharedPointerI8CMonitorEENS2_I10CWorkspaceEERKNSt6chrono10time_pointINS7_3_V212steady_clockENS7_8durationIlSt5ratioILl1ELl1000000000EEEEEE") != std::string::npos) {
                pRenderWorkspaceWindows = m.address;
                g_pOnRenderWorkspaceWindows = HyprlandAPI::createFunctionHook(globals->api, m.address, (void*)&hook_onRenderWorkspaceWindows);
                g_pOnRenderWorkspaceWindows->hook();
            }
        }
    }
    {
        static auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "renderWorkspaceWindowsFullscreen");
        pRenderWorkspaceWindowsFullscreen = METHODS[0].address;
    }    
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "vectorToWindowUnified");
        for (auto m : METHODS) {
            if (m.signature.find("CCompositor") != std::string::npos) {
                g_pOnVectorToWindowUnified = HyprlandAPI::createFunctionHook(globals->api, m.address, (void*)&hook_onVectorToWindowUnified);
                g_pOnVectorToWindowUnified->hook();
            }
        }
    }
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "mouseMoveUnified");
        for (auto m : METHODS) {
            if (m.signature.find("CInputManager") != std::string::npos) {
                g_pOnMouseMoveUnified = HyprlandAPI::createFunctionHook(globals->api, m.address, (void*)&hook_onMouseMoveUnified);
                g_pOnMouseMoveUnified->hook();
            }
        }
    }

    
}

static PHLWINDOWREF prev;

void HyprIso::all_lose_focus() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    prev = g_pCompositor->m_lastWindow;
    g_pSeatManager->setPointerFocus(nullptr, {});
    g_pCompositor->focusWindow(nullptr);
}

void HyprIso::all_gain_focus() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (auto p = prev.lock()) {
        //g_pSeatManager->setPointerFocus(p, {});
        g_pCompositor->focusWindow(p);        
    }
}

void HyprIso::set_corner_rendering_mask_for_window(int id, int mask) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hw: hyprwindows)
        if (hw->id == id)
            hw->cornermask = mask;
}

Bounds bounds_monitor(int id);
Bounds bounds_reserved_monitor(int id);

Bounds bounds_full_client(int wid) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == wid) {
            if (auto w = hyprwindow->w.get()) {
                return tobounds(w->getFullWindowBoundingBox());
            }
        }
    }    
    return {0, 0, 0, 0};
}

Bounds bounds_client(int wid) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == wid) {
            if (auto w = hyprwindow->w.get()) {
                //return w->getFullWindowBoundingBox();
                return tobounds(w->getWindowMainSurfaceBox());
            }
        }
    }    
    return {0, 0, 0, 0};
}

Bounds real_bounds_client(int wid) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == wid) {
            if (auto w = hyprwindow->w.get()) {
                //return w->getFullWindowBoundingBox();
                return tobounds({
                    w->m_realPosition->goal().x, w->m_realPosition->goal().y,
                    w->m_realSize->goal().x, w->m_realSize->goal().y
                });
            }
        }
    }    
    return {0, 0, 0, 0};
}

Bounds bounds_monitor(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hyprmonitor : hyprmonitors) {
        if (hyprmonitor->id == id) {
            if (auto m = hyprmonitor->m.get()) {
                return tobounds(m->logicalBox());
            }
        }
    }    
    return {0, 0, 0, 0};
}

Bounds bounds_reserved_monitor(int id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto hyprmonitor : hyprmonitors) {
        if (hyprmonitor->id == id) {
            if (auto m = hyprmonitor->m.get()) {
                auto b = m->logicalBox();
                b.x += m->m_reservedTopLeft.x;
                b.y += m->m_reservedTopLeft.y;
                b.w -= (m->m_reservedTopLeft.x + m->m_reservedBottomRight.x);
                b.h -= (m->m_reservedTopLeft.y + m->m_reservedBottomRight.y);
                return tobounds(b);
            }
        }
    }    
    return {0, 0, 0, 0};
}


