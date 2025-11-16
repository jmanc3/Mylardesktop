#include "dock.h"

#include "second.h"

#include "client/raw_windowing.h"
#include "client/windowing.h"
#include "process.hpp"

#include <cairo.h>
#include "process.hpp"
#include <thread>
#include <memory>
#include <pango/pangocairo.h>

struct CachedFont {
    std::string name;
    int size;
    int used_count;
    bool italic = false;
    PangoWeight weight;
    PangoLayout *layout;
    cairo_t *cr; // Creator
    
    ~CachedFont() { g_object_unref(layout); }
};

static std::vector<CachedFont *> cached_fonts;

struct BatteryData : UserData {
    float battery_level = 100;
    float brightness_level = 100;
};

struct VolumeData : UserData {
    float value = 50;
};

struct BrightnessData : UserData {
    float value = 50;
};

PangoLayout *
get_cached_pango_font(cairo_t *cr, std::string name, int pixel_height, PangoWeight weight, bool italic) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // Look for a matching font in the cache (including italic style)
    for (int i = cached_fonts.size() - 1; i >= 0; i--) {
        auto font = cached_fonts[i];
        if (font->name == name &&
            font->size == pixel_height &&
            font->weight == weight &&
            font->cr == cr &&
            font->italic == italic) { // New italic check
            pango_layout_set_attributes(font->layout, nullptr);
            font->used_count++;
            if (font->used_count < 512) {
//            printf("returned: %p\n", font->layout);
            	return font->layout;
            } else {
				delete font;
				cached_fonts.erase(cached_fonts.begin() + i);
            }
        }
    }
    
    // Create a new CachedFont entry
    auto *font = new CachedFont;
    assert(font);
    font->name = name;
    font->size = pixel_height;
    font->weight = weight;
    font->cr = cr;
    font->italic = italic; // Save the italic setting
    font->used_count = 0;
    
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc = pango_font_description_new();
    
    pango_font_description_set_size(desc, pixel_height * PANGO_SCALE);
    pango_font_description_set_family_static(desc, name.c_str());
    pango_font_description_set_weight(desc, weight);
    // Set the style to italic or normal based on the parameter
    pango_font_description_set_style(desc, italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    pango_layout_set_attributes(layout, nullptr);
    
    assert(layout);
    
    font->layout = layout;
    //printf("new: %p\n", font->layout);
    
    cached_fonts.push_back(font);
    
    assert(font->layout);
    
    return font->layout;
}

void cleanup_cached_fonts() {
    for (auto font: cached_fonts) {
        delete font;
    }
    cached_fonts.clear();
    cached_fonts.shrink_to_fit();
}

void remove_cached_fonts(cairo_t *cr) {
    for (int i = cached_fonts.size() - 1; i >= 0; --i) {
        if (cached_fonts[i]->cr == cr) {
            delete cached_fonts[i];
            cached_fonts.erase(cached_fonts.begin() + i);
        }
    }
}


static RawApp *dock_app = nullptr;
static MylarWindow *mylar_window = nullptr;

static void paint_root(Container *root, Container *c) {
    auto mylar = (MylarWindow *) root->user_data;
    auto cr = mylar->raw_window->cr;
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_rectangle(cr, root->real_bounds.x, root->real_bounds.y, root->real_bounds.w, root->real_bounds.h);
    cairo_set_source_rgba(cr, 0, 0, 0, .5);
    cairo_fill(cr);    
}

Bounds draw_text(cairo_t *cr, Container *c, std::string text, int size = 10, bool draw = true) {
    auto layout = get_cached_pango_font(cr, "Segoe UI Variable", size, PANGO_WEIGHT_NORMAL, false);
    //pango_layout_set_text(layout, "\uE7E7", strlen("\uE83F"));
    pango_layout_set_text(layout, text.data(), text.size());
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, &ink, &logical);
    if (draw) {
        cairo_move_to(cr, center_x(c, logical.width), center_y(c, logical.height));
        pango_cairo_show_layout(cr, layout);
    }
    return Bounds(ink.width, ink.height, logical.width, logical.height);
}

static void paint_button_bg(Container *root, Container *c) {
    auto mylar = (MylarWindow*)root->user_data;
    auto cr = mylar->raw_window->cr;
    if (c->state.mouse_pressing) {
        cairo_rectangle(cr, c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h);
        cairo_set_source_rgba(cr, 1, 1, 1, .2);
        cairo_fill(cr);
    } else if (c->state.mouse_hovering) {
        cairo_rectangle(cr, c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h);
        cairo_set_source_rgba(cr, 1, 1, 1, .3);
        cairo_fill(cr);
    }
}

static void paint_battery(Container *root, Container *c) {
    auto mylar = (MylarWindow*)root->user_data;
    auto battery_data = (BatteryData *) c->user_data;
    
    auto cr = mylar->raw_window->cr;
    paint_button_bg(root, c);
    draw_text(cr, c, fz("Battery: {}%", (int) std::round(battery_data->battery_level)), 10 * mylar->raw_window->dpi);
}

static std::string get_date() {
    using namespace std::chrono;

    auto now = system_clock::now();
    auto t = floor<seconds>(now);

    std::string s = std::format("{:%Y-%m-%d}\n{:%I:%M:%S %p}", t, t);

    return s;
}

static float get_brightness() {
    float current = 50;
    float max = 100;
    {
        auto process = std::make_shared<TinyProcessLib::Process>("brightnessctl max", "", [&max](const char *bytes, size_t n) {
            std::string text(bytes, n);
            try {
                max = std::atoi(text.c_str());
            } catch (...) {
                
            }
        });
    }
    {
        auto process = std::make_shared<TinyProcessLib::Process>("brightnessctl get", "", [&current](const char *bytes, size_t n) {
            std::string text(bytes, n);
            try {
                current = std::atoi(text.c_str());
            } catch (...) {
                
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return (current / max) * 100;
}

static float get_battery_level() {
    int value = 50;
    auto process = std::make_shared<TinyProcessLib::Process>("upower -i /org/freedesktop/UPower/devices/DisplayDevice | grep percentage | rg --only-matching '[0-9]*' | xargs", "", [&value](const char *bytes, size_t n) {
        std::string text(bytes, n);
        try {
            value = std::atoi(text.c_str());
        } catch (...) {
            
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return value;
}

static void set_brightness(float amount) {
    static bool queued = false;
    static bool latest = amount;
    latest = amount;
    if (queued)
        return;
    //queued = true;
    
    //windowing::add_fb(dock_app, 31);
    
    auto process = std::make_shared<TinyProcessLib::Process>(fz("brightnessctl set {}", (int) std::round(amount)));
}

static void fill_root(Container *root) {
    root->when_paint = paint_root;
    
    {
        auto volume = root->child(40, FILL_SPACE);
        auto volume_data = new VolumeData;
        volume->when_fine_scrolled = [](Container* root, Container* c, int scroll_x, int scroll_y, bool came_from_touchpad) {
            //notify(fz("fine scrolled {} {}", ((double) scroll_y) * .001, came_from_touchpad));
            auto mylar = (MylarWindow*)root->user_data;
            auto volume_data = (VolumeData *) c->user_data;
            volume_data->value += ((double) scroll_y) * .001;
            if (volume_data->value > 100) {
               volume_data->value = 100;
            }
            if (volume_data->value < 0) {
               volume_data->value = 0;
            }
        };
        volume->user_data = volume_data;
        volume->when_paint = paint {
            auto mylar = (MylarWindow*)root->user_data;
            auto volume_data = (VolumeData *) c->user_data;
            auto cr = mylar->raw_window->cr;
            paint_button_bg(root, c);
            draw_text(cr, c, fz("Volume: {}%", (int) std::round(volume_data->value)), 10 * mylar->raw_window->dpi);
        };
        volume->pre_layout = [](Container *root, Container *c, const Bounds &b) {
            auto mylar = (MylarWindow*)root->user_data;
            auto cr = mylar->raw_window->cr;
            auto bounds = draw_text(cr, c, "Volume: 100%", 10 * mylar->raw_window->dpi, false);
            c->wanted_bounds.w = bounds.w + 20;
        };
        volume->when_clicked = paint {
            auto mylar = (MylarWindow*)root->user_data;
            windowing::close_window(mylar->raw_window);
        };
    }

    {
        auto battery = root->child(40, FILL_SPACE);
        auto battery_data = new BatteryData;
        battery->user_data = battery_data;
        battery->when_paint = paint_battery;
        battery->pre_layout = [](Container *root, Container *c, const Bounds &b) {
            auto mylar = (MylarWindow*)root->user_data;
            auto cr = mylar->raw_window->cr;
            auto bounds = draw_text(cr, c, "Battery: 100%", 10 * mylar->raw_window->dpi, false);
            c->wanted_bounds.w = bounds.w + 20;
        };
        battery->when_clicked = paint {
            auto mylar = (MylarWindow*)root->user_data;
            windowing::close_window(mylar->raw_window);
        };
    }
    
    {
        auto brightness = root->child(40, FILL_SPACE);
        auto brightness_data = new BrightnessData;
        brightness_data->value = get_brightness();

        brightness->when_fine_scrolled = [](Container* root, Container* c, int scroll_x, int scroll_y, bool came_from_touchpad) {
            //notify(fz("fine scrolled {} {}", ((double) scroll_y) * .001, came_from_touchpad));
            auto mylar = (MylarWindow*)root->user_data;
            auto brightness_data = (BrightnessData *) c->user_data;
            brightness_data->value += ((double) scroll_y) * .001;
            if (brightness_data->value > 100) {
               brightness_data->value = 100;
            }
            if (brightness_data->value < 0) {
               brightness_data->value = 0;
            }
            set_brightness(brightness_data->value);
        };
        brightness->user_data = brightness_data;
        brightness->when_paint = paint {
            auto mylar = (MylarWindow*)root->user_data;
            auto brightness_data = (BrightnessData *) c->user_data;
            auto cr = mylar->raw_window->cr;
            paint_button_bg(root, c);
            draw_text(cr, c, fz("Brightness: {}%", (int) std::round(brightness_data->value)), 10 * mylar->raw_window->dpi);
        };
        brightness->pre_layout = [](Container *root, Container *c, const Bounds &b) {
            auto mylar = (MylarWindow*)root->user_data;
            auto cr = mylar->raw_window->cr;
            auto bounds = draw_text(cr, c, "Brightness: 100%", 10 * mylar->raw_window->dpi, false);
            c->wanted_bounds.w = bounds.w + 20;
        };
        brightness->when_clicked = paint {
            auto mylar = (MylarWindow*)root->user_data;
            windowing::close_window(mylar->raw_window);
        };
    }

    {
        auto date = root->child(40, FILL_SPACE);
        date->when_paint = paint {
            auto mylar = (MylarWindow*)root->user_data;
            auto cr = mylar->raw_window->cr;
            paint_button_bg(root, c);
            draw_text(cr, c, get_date(), 10 * mylar->raw_window->dpi);
        };
        date->pre_layout = [](Container *root, Container *c, const Bounds &b) {
            auto mylar = (MylarWindow*)root->user_data;
            auto cr = mylar->raw_window->cr;
            auto bounds = draw_text(cr, c, get_date(), 10 * mylar->raw_window->dpi, false);
            c->wanted_bounds.w = bounds.w + 20;
        };
        date->when_clicked = paint {
            auto mylar = (MylarWindow*)root->user_data;
            windowing::close_window(mylar->raw_window);
        };
    }
};

void dock_start() {
    dock_app = windowing::open_app();
    RawWindowSettings settings;
    settings.pos.w = 0;
    settings.pos.h = 40;
    settings.name = "Dock";
    auto mylar = open_mylar_window(dock_app, WindowType::DOCK, settings);
    mylar_window = mylar;
    mylar->root->user_data = mylar;
    mylar->root->alignment = ALIGN_RIGHT;
    fill_root(mylar->root);

    //notify("be");
    windowing::main_loop(dock_app);
    //notify("asdf");
}

void dock::start() {
    //return;
    notify("dock start");
    std::thread t(dock_start);
    t.detach();
}

void dock::stop() {
    //return;
    windowing::close_app(dock_app);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

