/*
 *  The hyprland isolation file.
 *
 *  This file is the sole place where we anything hyprland specific is allowed to be included.
 *
 *  The purpose is to minimize our interaction surface so that our program stays as functional as possible on new updates, and we only need to fix up this file for new versions. 
 */

#include "hypriso.h"

#include "first.h"

#include <hyprland/src/helpers/Color.hpp>

#define private public
#include <hyprland/src/render/pass/SurfacePassElement.hpp>
#undef private

#include <any>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <librsvg/rsvg.h>



HyprIso *hypriso = new HyprIso;

static int unique_id = 0;

struct HyprWindow {
    int id;  
    PHLWINDOW w;
    int cornermask = 0; // when rendering the surface, what corners should be rounded
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

    for (auto m : g_pCompositor->m_monitors) {
        on_open_monitor(m);
    }
    for (auto w : g_pCompositor->m_windows) {
        on_open_window(w);
    }

    fix_window_corner_rendering();
}

void HyprIso::end() {
    g_pHyprRenderer->m_renderPass.removeAllOfType("CRectPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CTexPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CAnyPassElement");
}

CBox tocbox(Bounds b) {
    return {b.x, b.y, b.w, b.h};
}

Bounds tobounds(CBox box) {
    return {box.x, box.y, box.w, box.h};
}

void rect(Bounds box, RGBA color, int cornermask, float round, float roundingPower, bool blur, float blurA) {
    AnyPass::AnyData anydata([box, color, cornermask, round, roundingPower, blur, blurA](AnyPass* pass) {
        CHyprOpenGLImpl::SRectRenderData rectdata;
        rectdata.blur          = blur;
        rectdata.blurA         = blurA;
        rectdata.round         = std::round(round);
        rectdata.roundingPower = roundingPower;
        set_rounding(cornermask); // only top side
        g_pHyprOpenGL->renderRect(tocbox(box), CHyprColor(color.r, color.g, color.b, color.a), rectdata);
        set_rounding(0);
    });
    g_pHyprRenderer->m_renderPass.add(makeUnique<AnyPass>(std::move(anydata)));
}

void border(Bounds box, RGBA color, float size, int cornermask, float round, float roundingPower, bool blur, float blurA) {
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

void move(int id, int x, int y) {
    auto m = g_pCompositor->getMonitorFromCursor();
    for (auto c : hyprwindows) {
        if (c->id == id) {
            float xs = x * (1.0 / m->m_scale);
            float ys = y * (1.0 / m->m_scale);
            c->w->m_realPosition->setValueAndWarp({xs, ys});
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
    if (!icon) {
        return missingTexure(target_size);
    }
    // we can darken

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
        return t->info;
    }
    return {};
}

void draw_texture(TextureInfo info, int x, int y, float a) {
    for (auto t : hyprtextures) {
       if (t->info.id == info.id) {
            CTexPassElement::SRenderData data;
            data.tex = t->texture;
            data.box = {(float) x, (float) y, data.tex->m_size.x, data.tex->m_size.y};
            data.box.x = x;
            data.box.round();
            data.clipBox = data.box;
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


 
