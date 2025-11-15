#include "dock.h"

#include "second.h"

#include "client/raw_windowing.h"
#include "client/windowing.h"

#include <cairo.h>
#include <chrono>
#include <thread>
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

Bounds draw_text(cairo_t *cr, Container *c, std::string text, bool draw = true) {
    auto layout = get_cached_pango_font(cr, "Segoe UI Variable", 10, PANGO_WEIGHT_NORMAL, false);
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
    auto cr = mylar->raw_window->cr;
    paint_button_bg(root, c);
    draw_text(cr, c, "Battery: 100%");
}

static std::string get_date() {
    using namespace std::chrono;

    auto now = system_clock::now();
    auto t = floor<seconds>(now);

    std::string s = std::format("{:%Y-%m-%d}\n{:%I:%M %p}", t, t);

    return s;
}

static void fill_root(Container *root) {
    root->when_paint = paint_root;
    
    {
        auto volume = root->child(40, FILL_SPACE);
        volume->when_paint = paint {
            auto mylar = (MylarWindow*)root->user_data;
            auto cr = mylar->raw_window->cr;
            paint_button_bg(root, c);
            draw_text(cr, c, "Volume: 100%");
        };
        volume->pre_layout = [](Container *root, Container *c, const Bounds &b) {
            auto mylar = (MylarWindow*)root->user_data;
            auto cr = mylar->raw_window->cr;
            auto bounds = draw_text(cr, c, "Volume: 100%", false);
            c->wanted_bounds.w = bounds.w + 20;
        };
        volume->when_clicked = paint {
            auto mylar = (MylarWindow*)root->user_data;
            windowing::close_window(mylar->raw_window);
        };
    }

    {
        auto battery = root->child(40, FILL_SPACE);
        battery->when_paint = paint_battery;
        battery->pre_layout = [](Container *root, Container *c, const Bounds &b) {
            auto mylar = (MylarWindow*)root->user_data;
            auto cr = mylar->raw_window->cr;
            auto bounds = draw_text(cr, c, "Battery: 100%", false);
            c->wanted_bounds.w = bounds.w + 20;
        };
        battery->when_clicked = paint {
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
            draw_text(cr, c, get_date());
        };
        date->pre_layout = [](Container *root, Container *c, const Bounds &b) {
            auto mylar = (MylarWindow*)root->user_data;
            auto cr = mylar->raw_window->cr;
            auto bounds = draw_text(cr, c, get_date(), false);
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

    notify("be");
    windowing::main_loop(dock_app);
    notify("asdf");
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

