/*
 *  The hyprland isolation file.
 *
 *  This file is the sole place where we anything hyprland specific is allowed to be included.
 *
 *  The purpose is to minimize our interaction surface so that our program stays as functional as possible on new updates, and we only need to fix up this file for new versions. 
 */

#include "hypriso.h"

#include "container.h"
#include "first.h"

#include <algorithm>
#include <any>

#include <hyprland/src/helpers/Color.hpp>
#include <kde-server-decoration.hpp>
//#include <hyprland/protocols/kde-server-decoration.hpp>

#include <hyprland/src/render/pass/ShadowPassElement.hpp>

#define private public
#include <hyprland/src/render/pass/SurfacePassElement.hpp>
#include <hyprland/src/protocols/ServerDecorationKDE.hpp>
#include <hyprland/src/protocols/XDGDecoration.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#undef private

#include <hyprland/src/xwayland/XWayland.hpp>
#include <hyprland/src/xwayland/XWM.hpp>

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <librsvg/rsvg.h>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Framebuffer.hpp>


HyprIso *hypriso = new HyprIso;

static int unique_id = 0;

void* pRenderWindow = nullptr;
void* pRenderLayer = nullptr;
void* pRenderMonitor = nullptr;
void* pRenderWorkspace = nullptr;
void* pRenderWorkspaceWindowsFullscreen = nullptr;
typedef void (*tRenderWindow)(void *, PHLWINDOW, PHLMONITOR, timespec *, bool decorate, eRenderPassMode, bool ignorePosition, bool standalone);
typedef void (*tRenderMonitor)(void *, PHLMONITOR pMonitor, bool commit);
typedef void (*tRenderWorkspace)(void *, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp &, const CBox &geom);
typedef void (*tRenderWorkspaceWindowsFullscreen)(void *, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp &);

struct HyprWindow {
    int id;  
    PHLWINDOW w;
    
    CFramebuffer *fb = nullptr;
    Bounds w_size; // 0 -> 1, percentage of fb taken up by the actual window used for drawing
    
    int cornermask = 0; // when rendering the surface, what corners should be rounded
    bool no_rounding = false;
};

static std::vector<HyprWindow *> hyprwindows;

struct HyprMonitor {
    int id;  
    PHLMONITOR m;
};

static std::vector<HyprMonitor *> hyprmonitors;

struct Texture {
    SP<CTexture> texture;
    TextureInfo info;
};

static std::vector<Texture *> hyprtextures;

class AnyPass : public IPassElement {
public:
    struct AnyData {
        std::function<void(AnyPass*)> draw = nullptr;
        //void (*draw)(AnyPass *pass) = nullptr;
    };

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
    virtual std::optional<CBox> boundingBox() {
        return {};
    }
    virtual CRegion opaqueRegion() {
        return {};
    }
    virtual const char* passName() {
        return "CAnyPassElement";
    }

    AnyData* m_data = nullptr;
};

void set_rounding(int mask) {
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

void on_open_window(PHLWINDOW w) {
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
    if (w->m_X11DoesntWantBorders)
        return;

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

inline CFunctionHook* g_pOnSurfacePassDraw = nullptr;
typedef void (*origSurfacePassDraw)(void*, const CRegion& damage);
void hook_onSurfacePassDraw(void* thisptr, const CRegion& damage) {
    auto  spe = (CSurfacePassElement *) thisptr;
    auto window = spe->m_data.pWindow;
    int cornermask = 0;
    for (auto hw: hyprwindows)
        if (hw->w == window)
            cornermask = hw->cornermask;
    set_rounding(cornermask); // only top rounding
    (*(origSurfacePassDraw)g_pOnSurfacePassDraw->m_original)(thisptr, damage);
    set_rounding(0);
}

void fix_window_corner_rendering() {
    static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "draw");
    for (auto m : METHODS) {
        if (m.signature.find("SurfacePassElement") != std::string::npos) {
            g_pOnSurfacePassDraw = HyprlandAPI::createFunctionHook(globals->api, m.address, (void*)&hook_onSurfacePassDraw);
            g_pOnSurfacePassDraw->hook();
        }
    }
}


inline CFunctionHook* g_pOnReadProp = nullptr;
typedef void (*origOnReadProp)(void*, SP<CXWaylandSurface> XSURF, uint32_t atom, xcb_get_property_reply_t* reply);
    //void CXWM::readProp(SP<CXWaylandSurface> XSURF, uint32_t atom, xcb_get_property_reply_t* reply) {
void hook_OnReadProp(void* thisptr, SP<CXWaylandSurface> XSURF, uint32_t atom, xcb_get_property_reply_t* reply) {
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

void recheck_csd_for_all_wayland_windows() {
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
    recheck_csd_for_all_wayland_windows();
    return (*(origOnKDECSD)g_pOnKDECSD->m_original)(thisptr);
}

inline CFunctionHook* g_pOnXDGCSD = nullptr;
typedef zxdgToplevelDecorationV1Mode (*origOnXDGCSD)(void*);
zxdgToplevelDecorationV1Mode hook_OnXDGCSD(void* thisptr) {
    recheck_csd_for_all_wayland_windows();
    return (*(origOnXDGCSD)g_pOnXDGCSD->m_original)(thisptr);
}

void detect_csd_request_change() {
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
typedef float (*origWindowRoundingFunc)(void*);
float hook_WindowRounding(void* thisptr) {
    float result = (*(origWindowRoundingFunc)g_pWindowRoundingHook->m_original)(thisptr);
    return result;
}

inline CFunctionHook* g_pWindowRoundingPowerHook = nullptr;
typedef float (*origWindowRoundingPowerFunc)(void*);
float hook_WindowRoundingPower(void* thisptr) {
    float result = (*(origWindowRoundingPowerFunc)g_pWindowRoundingPowerHook->m_original)(thisptr);
    return result;
}

void hook_render_functions() {
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "rounding");
        g_pWindowRoundingHook       = HyprlandAPI::createFunctionHook(globals->api, METHODS[0].address, (void*)&hook_WindowRounding);
        g_pWindowRoundingHook->hook();
    }
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "roundingPower");
        g_pWindowRoundingPowerHook       = HyprlandAPI::createFunctionHook(globals->api, METHODS[0].address, (void*)&hook_WindowRoundingPower);
        g_pWindowRoundingPowerHook->hook();
    }
 
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
        static auto METHODS         = HyprlandAPI::findFunctionsByName(globals->api, "renderWorkspaceWindowsFullscreen");
        pRenderWorkspaceWindowsFullscreen = METHODS[0].address;
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
    {
        static const auto METHODS = HyprlandAPI::findFunctionsByName(globals->api, "circleNext");
        g_pOnCircleNextHook       = HyprlandAPI::createFunctionHook(globals->api, METHODS[0].address, (void*)&hook_onCircleNext);
        g_pOnCircleNextHook->hook();
    }
}

void set_window_corner_mask(int id, int cornermask) {
    for (auto hw: hyprwindows)
        if (hw->id == id)
            hw->cornermask = cornermask;
}

void HyprIso::create_hooks_and_callbacks() {
    static auto mouseMove = HyprlandAPI::registerCallbackDynamic(globals->api, "mouseMove", 
        [](void* self, SCallbackInfo& info, std::any data) {
            auto consume = false;
            if (hypriso->on_mouse_move) {
                auto mouse = g_pInputManager->getMouseCoordsInternal();
                auto m = g_pCompositor->getMonitorFromCursor();
                consume = hypriso->on_mouse_move(0, mouse.x * m->m_scale, mouse.y * m->m_scale);
            }
            info.cancelled = consume;
        });
    
    static auto mouseButton = HyprlandAPI::registerCallbackDynamic(globals->api, "mouseButton", 
        [](void* self, SCallbackInfo& info, std::any data) {
            auto e = std::any_cast<IPointer::SButtonEvent>(data);
            auto consume = false;
            if (hypriso->on_mouse_press) {
                auto mouse = g_pInputManager->getMouseCoordsInternal();
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
    static auto configReloaded = HyprlandAPI::registerCallbackDynamic(globals->api, "configReloaded", 
    [](void *self, SCallbackInfo& info, std::any data) {
        if (hypriso->on_config_reload) {
            hypriso->on_config_reload();
        }
    });
    

    for (auto m : g_pCompositor->m_monitors) {
        on_open_monitor(m);
    }
    for (auto w : g_pCompositor->m_windows) {
        on_open_window(w);
    }

    fix_window_corner_rendering();
    detect_csd_request_change();
    detect_move_resize_requests();    
    disable_default_alt_tab_behaviour();
    hook_render_functions();
}

void HyprIso::end() {
    g_pHyprRenderer->m_renderPass.removeAllOfType("CRectPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CTexPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CAnyPassElement");
    remove_request_listeners();
}

CBox tocbox(Bounds b) {
    return {b.x, b.y, b.w, b.h};
}

Bounds tobounds(CBox box) {
    return {box.x, box.y, box.w, box.h};
}

void rect(Bounds box, RGBA color, int cornermask, float round, float roundingPower, bool blur, float blurA) {
    if (box.h <= 0 || box.w <= 0)
        return;
    AnyPass::AnyData anydata([box, color, cornermask, round, roundingPower, blur, blurA](AnyPass* pass) {
        CHyprOpenGLImpl::SRectRenderData rectdata;
        rectdata.blur          = blur;
        rectdata.blurA         = blurA;
        rectdata.round         = std::round(round);
        rectdata.roundingPower = roundingPower;
        rectdata.xray = false;
        set_rounding(cornermask); // only top side
        g_pHyprOpenGL->renderRect(tocbox(box), CHyprColor(color.r, color.g, color.b, color.a), rectdata);
        set_rounding(0);
    });
    g_pHyprRenderer->m_renderPass.add(makeUnique<AnyPass>(std::move(anydata)));
}

void border(Bounds box, RGBA color, float size, int cornermask, float round, float roundingPower, bool blur, float blurA) {
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
    AnyPass::AnyData anydata([box, color, rounding, roundingPower, size](AnyPass* pass) {
        g_pHyprOpenGL->m_renderData.currentWindow = g_pCompositor->m_lastWindow;
        g_pHyprOpenGL->renderRoundedShadow(tocbox(box), rounding, roundingPower, size, CHyprColor(color.r, color.g, color.b, color.a), 1.0);
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

Bounds bounds_full(ThinClient *w) {
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == w->id) {
            if (auto w = hyprwindow->w.get()) {
                return tobounds(w->getFullWindowBoundingBox());
            }
        }
    }    
    return {0, 0, 0, 0};
}

Bounds bounds(ThinClient *w) {
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == w->id) {
            if (auto w = hyprwindow->w.get()) {
                //return w->getFullWindowBoundingBox();
                return tobounds(w->getWindowMainSurfaceBox());
            }
        }
    }    
    return {0, 0, 0, 0};
}

Bounds real_bounds(ThinClient *w) {
    for (auto hyprwindow : hyprwindows) {
        if (hyprwindow->id == w->id) {
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

Bounds bounds(ThinMonitor *m) {
    for (auto hyprmonitor : hyprmonitors) {
        if (hyprmonitor->id == m->id) {
            if (auto m = hyprmonitor->m.get()) {
                return tobounds(m->logicalBox());
            }
        }
    }    
    return {0, 0, 0, 0};
}

Bounds bounds_reserved(ThinMonitor *m) {
    for (auto hyprmonitor : hyprmonitors) {
        if (hyprmonitor->id == m->id) {
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

void notify(std::string text) {
    HyprlandAPI::addNotification(globals->api, text, {1, 1, 1, 1}, 4000);
}

int current_rendering_monitor() {
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
    for (auto hyprmonitor : hyprmonitors) {
        if (hyprmonitor->id == id) {
            return hyprmonitor->m->m_scale;
        }
    }
    return 1.0;
}


std::vector<int> get_window_stacking_order() {
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
    auto m = g_pCompositor->getMonitorFromCursor();
    for (auto c : hyprwindows) {
        if (c->id == id) {
            c->w->m_realPosition->setValueAndWarp({x, y});
        }
    }
}

void HyprIso::move_resize(int id, int x, int y, int w, int h) {
    auto m = g_pCompositor->getMonitorFromCursor();
    for (auto c : hyprwindows) {
        if (c->id == id) {
            c->w->m_realPosition->setValueAndWarp({x, y});
            c->w->m_realSize->setValueAndWarp({w, h});
            c->w->sendWindowSize(true);
            c->w->updateWindowDecos();
        }
    }
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
    for (auto t : hyprtextures) {
       if (t->info.id == info.id) {
            CTexPassElement::SRenderData data;
            data.tex = t->texture;
            data.box = {(float) x, (float) y, data.tex->m_size.x, data.tex->m_size.y};
            data.box.x = x;
            data.box.round();
            data.clipBox = data.box;
            if (clip_w != 0.0) {
                data.clipBox.w = clip_w;
            }
            data.a = 1.0 * a;
            g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(std::move(data)));
       }
    }
}

void setCursorImageUntilUnset(std::string cursor) {
    g_pInputManager->setCursorImageUntilUnset(cursor);

}

void unsetCursorImage() {
    g_pInputManager->unsetCursorImage();
}

int get_monitor(int client) {
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
    auto mouse = g_pInputManager->getMouseCoordsInternal();
    return {mouse.x, mouse.y, mouse.x, mouse.y};
}

void close_window(int id) {
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            g_pCompositor->closeWindow(hw->w);
        }
    }
}

int HyprIso::monitor_from_cursor() {
    auto m = g_pCompositor->getMonitorFromCursor();
    for (auto hm : hyprmonitors) {
        if (hm->m == m) {
            return hm->id;   
        }
    }
    return -1;
}

void HyprIso::iconify(int id, bool state) {
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            hw->w->updateWindowDecos();
            hw->w->setHidden(state);
        }
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

void HyprIso::bring_to_front(int id) {
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            g_pCompositor->focusWindow(hw->w);
            g_pCompositor->changeWindowZOrder(hw->w, true);
        }
    }
}

Bounds HyprIso::min_size(int id) {
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            auto s = hw->w->requestedMinSize();
            return {s.x, s.y, s.x, s.y};
        }
    }
    return {20, 20, 20, 20};
}

bool HyprIso::has_decorations(int id) {
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
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            return hw->w->m_isX11;
        }
    }
    return false;
}

void HyprIso::send_key(uint32_t key) {
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
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            return hw->w->isFullscreen();
        }
    }
    return false;
}

void HyprIso::should_round(int id, bool state) {
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            hw->no_rounding = !state;
        }
    }
}

void HyprIso::damage_entire(int monitor) {
    for (auto hm : hyprmonitors) {
        if (hm->id == monitor) {
            g_pHyprRenderer->damageMonitor(hm->m);
        }
    }
}

int later_action(void* user_data) {
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
    auto timer    = new Timer;
    timer->func   = fn;
    timer->data   = data;
    timer->delay  = time_ms;
    timer->source = wl_event_loop_add_timer(g_pCompositor->m_wlEventLoop, &later_action, timer);
    wl_event_source_timer_update(timer->source, time_ms);
    return timer;
}

Timer* later(float time_ms, const std::function<void(Timer*)>& fn) {
    auto timer    = new Timer;
    timer->func   = fn;
    timer->delay  = time_ms;
    timer->source = wl_event_loop_add_timer(g_pCompositor->m_wlEventLoop, &later_action, timer);
    wl_event_source_timer_update(timer->source, time_ms);
    return timer;
}

void screenshot_monitor(CFramebuffer* buffer, PHLMONITOR m) {
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

void screenshot_workspace(CFramebuffer* buffer, PHLWORKSPACEREF w, PHLMONITOR m, bool include_cursor) {
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
    const auto NOW                       = Time::steadyNow();
    // bg
    (*(tRenderWorkspace)pRenderWorkspace)(g_pHyprRenderer.get(), m, w.lock(), NOW, CBox(0, 0, (int)m->m_pixelSize.x, (int)m->m_pixelSize.y));
    // clients
    (*(tRenderWorkspaceWindowsFullscreen)pRenderWorkspaceWindowsFullscreen)(g_pHyprRenderer.get(), m, w.lock(), NOW);
    g_pHyprRenderer->endRender();
}

void screenshot_window_with_decos(CFramebuffer* buffer, PHLWINDOW w) {
    if (!buffer || !pRenderWindow)
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

    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    auto fo = w->m_floatingOffset;
    w->m_floatingOffset.x -= w->m_realPosition->value().x - ex.topLeft.x;
    w->m_floatingOffset.y -= w->m_realPosition->value().y - ex.topLeft.y;
    (*(tRenderWindow)pRenderWindow)(g_pHyprRenderer.get(), w, m, &now, true, RENDER_PASS_ALL, false, false);
    w->m_floatingOffset = fo;

    g_pHyprRenderer->endRender();

    g_pHyprRenderer->m_bRenderingSnapshot = false;
}

void screenshot_window(HyprWindow *hw, PHLWINDOW w, bool include_decorations) {
    if (!hw->fb|| !pRenderWindow)
        return;
    if (include_decorations) {
        screenshot_window_with_decos(hw->fb, w);
        return;
    }
    const auto m = w->m_monitor.lock();
    if (!m || !m->m_output || m->m_pixelSize.x <= 0 || m->m_pixelSize.y <= 0)
        return;

    // we need to "damage" the entire monitor
    // so that we render the entire window
    // this is temporary, doesnt mess with the actual damage
    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    g_pHyprRenderer->makeEGLCurrent();
    hw->fb->alloc(m->m_pixelSize.x, m->m_pixelSize.y, DRM_FORMAT_ABGR8888);
    g_pHyprRenderer->beginRender(m, fakeDamage, RENDER_MODE_FULL_FAKE, nullptr, hw->fb);
    g_pHyprRenderer->m_bRenderingSnapshot = true;
    g_pHyprOpenGL->clear(CHyprColor(0, 0, 0, 0)); // JIC
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    (*(tRenderWindow)pRenderWindow)(g_pHyprRenderer.get(), w, m, &now, false, RENDER_PASS_MAIN, true, true);
    g_pHyprRenderer->endRender();
    g_pHyprRenderer->m_bRenderingSnapshot = false;

    hw->w_size = Bounds(0, 0, (w->m_realSize->value().x * m->m_scale), (w->m_realSize->value().y * m->m_scale));
}

void HyprIso::screenshot_all() {
    for (auto w : g_pCompositor->m_windows) {
        bool has_mylar_bar = false;
        for (const auto &decos : w->m_windowDecorations) 
            if (decos->getDisplayName() == "MylarBar")
                has_mylar_bar = true;
            
        if (has_mylar_bar) {
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

// Will stretch the thumbnail if the aspect ratio passed in is different from thumbnail
void HyprIso::draw_thumbnail(int id, Bounds b, int rounding, float roundingPower, int cornermask) {
    for (auto hw : hyprwindows) {
        if (hw->id == id) {
            if (hw->fb) {
                AnyPass::AnyData anydata([id, b, hw, rounding, roundingPower, cornermask](AnyPass* pass) {
                    auto tex = hw->fb->getTexture();
                    auto box = tocbox(b);
                    CHyprOpenGLImpl::STextureRenderData data;
                    data.allowCustomUV = true;
                    data.round = rounding;
                    data.roundingPower = roundingPower;
                    g_pHyprOpenGL->m_renderData.primarySurfaceUVTopLeft     = Vector2D(0, 0);
                    g_pHyprOpenGL->m_renderData.primarySurfaceUVBottomRight = Vector2D(
                        std::min(hw->w_size.w / hw->fb->m_size.x, 1.0), 
                        std::min(hw->w_size.h / hw->fb->m_size.y, 1.0) 
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

void HyprIso::set_zoom_factor(float amount) {
    Hyprlang::CConfigValue* val = g_pConfigManager->getHyprlangConfigValuePtr("cursor:zoom_factor");
    auto zoom_amount = (Hyprlang::FLOAT*)val->dataPtr();
    *zoom_amount = amount;
    
    for (auto const& m : g_pCompositor->m_monitors) {
        *(m->m_cursorZoom) = amount;
        g_pLayoutManager->getCurrentLayout()->recalculateMonitor(m->m_id);
    }    
}

int HyprIso::parent(int id) {
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
