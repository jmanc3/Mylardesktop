// wl_input.c
#include <cairo-deprecated.h>
#include <cstddef>
#include <wayland-client-core.h>
#define _POSIX_C_SOURCE 200809L
#include <poll.h>    // for POLLIN, POLLOUT, POLLERR, etc.
#include <errno.h>   // for EAGAIN, EINTR, and other errno constants
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <wayland-client.h>
#include <vector>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <wayland-client.h>
#include <cairo/cairo.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <climits>

extern "C" {
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
#include "xdg-shell-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
}

#include <xkbcommon/xkbcommon.h>

#include "../include/container.h"
#include "../include/events.h"
#include "../include/hypriso.h"

bool running = true;

struct wl_window;

bool wl_window_resize_buffer(struct wl_window *win, int new_width, int new_height);

struct wl_context {
    struct wl_display *display = nullptr;
    struct wl_registry *registry = nullptr;
    struct wl_compositor *compositor = nullptr;
    struct wl_shm *shm = nullptr;
    struct wl_seat *seat = nullptr;
    struct xdg_wm_base *wm_base = nullptr;
    struct zwlr_layer_shell_v1 *layer_shell = nullptr;
    struct wl_keyboard *keyboard = nullptr;
    struct wl_pointer *pointer = nullptr;
    struct xkb_context *xkb_ctx = nullptr;
    //struct wl_output *output;
    uint32_t shm_format;

    std::vector<wl_window *> windows;
};

struct wl_window {
    struct wl_context *ctx = nullptr;
    struct wl_surface *surface = nullptr;
    struct xdg_surface *xdg_surface = nullptr;
    struct xdg_toplevel *xdg_toplevel = nullptr;
    struct zwlr_layer_surface_v1 *layer_surface = nullptr;
    struct wl_output *output = nullptr;
    struct xkb_keymap *keymap = nullptr;
    struct xkb_state *xkb_state = nullptr;
    struct wl_shm_pool *pool = nullptr;
    wl_buffer *buffer = nullptr;

    Container *root = new Container;
    std::function<void(wl_window *)> on_render = nullptr;

    cairo_surface_t *cairo_surface = nullptr;
    cairo_t *cr = nullptr;
    
    int pending_width, pending_height; // recieved from configured event
    
    int width, height;
    void *data;
    size_t size;
    int stride;

    int cur_x = 0;
    int cur_y = 0;

    std::string title;
    bool has_pointer_focus = false;
    bool has_keyboard_focus = false;
    bool is_layer = true;
    bool marked_for_closing = false;
};

static struct wl_buffer *create_shm_buffer(struct wl_context *d, int width, int height);

static void handle_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    auto win = (wl_window *) data;
    win->marked_for_closing = true;
    printf("Compositor requested window close\n");
    //running = false;  // set your main loop flag to exit
}

static void handle_toplevel_configure(
    void *data,
    struct xdg_toplevel *toplevel,
    int32_t width,
    int32_t height,
    struct wl_array *states) 
{
    auto win = (wl_window *) data;

    // Save for later (don’t resize yet)
    if (width > 0) win->pending_width  = width;
    if (height > 0) win->pending_height = height;
    //wl_window_resize_buffer(win, width, height);
    //wl_window_draw(win);
    
    // Usually you’d handle resize here
    printf("size reconfigured\n");
}
static void handle_surface_configure(void *data,
			  struct xdg_surface *xdg_surface,
			  uint32_t serial) {
    struct wl_window *win = (struct wl_window *)data;
    xdg_surface_ack_configure(xdg_surface, serial);
    wl_surface_commit(win->surface);

    if (win->pending_width > 0 && win->pending_height > 0 &&
        (win->pending_width != win->width || win->pending_height != win->height)) {
        wl_window_resize_buffer(win, win->pending_width, win->pending_height);
    }
    // Now it’s legal to resize + draw
    //wl_window_resize_buffer(win, win->pending_width, win->pending_height);
    //wl_window_draw(win);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = handle_surface_configure, 	 
};

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = handle_toplevel_configure,
    .close = handle_toplevel_close,
};

/* ---- helper: create a simple shm buffer so surface is mapped ---- */
static int create_shm_file(size_t size) {
    char temp[] = "/tmp/wl-shm-XXXXXX";
    int fd = mkstemp(temp);
    if (fd >= 0) {
        unlink(temp); // unlink so it is removed after close
        if (ftruncate(fd, size) < 0) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

static int create_anonymous_file(off_t size) {
    char path[] = "/dev/shm/wayland-shm-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        fprintf(stderr, "mkstemp failed: %s\n", strerror(errno));
        return -1;
    }
    unlink(path);
    if (ftruncate(fd, size) < 0) {
        fprintf(stderr, "ftruncate failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static void destroy_shm_buffer(struct wl_window *win) {
    if (win->cairo_surface) {
        cairo_surface_destroy(win->cairo_surface);
        win->cairo_surface = nullptr;
    }

    if (win->buffer) {
        wl_buffer_destroy(win->buffer);
        win->buffer = nullptr;
    }

    if (win->data) {
        const int stride = win->width * 4;
        const int size = stride * win->height;
        munmap(win->data, size);
        win->data = nullptr;
    }
}

void on_paint(struct wl_window *win) {
    auto cr = win->cr;
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, 0, 0, 0, .4);
    //cairo_rectangle(cr, 0, 0, win->width, win->height);
    cairo_rectangle(cr, 0, 0, win->width, win->height);
    cairo_fill(cr);
    
    //cairo_set_source_rgba(cr, 0, 0, 0, 1);
    //cairo_rectangle(cr, 10, 10, 10, 10);
    //cairo_fill(cr);
    
    //cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    //cairo_rectangle(cr, 0, 0, 10, 100);
    cairo_fill(cr);
 
    
    cairo_fill(cr);
/*
    cairo_set_source_rgba(cr, 1, 1, 1, .2);
    //cairo_rectangle(cr, 0, 0, win->width, win->height);
    cairo_rectangle(cr, 10, 10, win->width - 20, win->height - 20);
    cairo_fill(cr);
    
    cairo_set_source_rgba(cr, 0, 0, 0, 1);
    cairo_rectangle(cr, 10, 10, 10, 10);
    cairo_fill(cr);
    */

    // Attach new buffer immediately so compositor knows about new size
    wl_surface_attach(win->surface, win->buffer, 0, 0);
    wl_surface_damage_buffer(win->surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(win->surface);
}

void on_window_render(wl_window *win) {
    ///*
    //notify(std::to_string(win->width) + " " + std::to_string(win->height));
    win->root->wanted_bounds = Bounds(0, 0, FILL_SPACE, FILL_SPACE);
    win->root->real_bounds = Bounds(0, 0, win->width, win->height);
    ::layout(win->root, win->root, win->root->real_bounds);
    
    if (win->root)
        paint_outline(win->root, win->root);
    
    wl_surface_attach(win->surface, win->buffer, 0, 0);
    wl_surface_damage_buffer(win->surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(win->surface);
    //*/
    //on_paint(win);
}

bool wl_window_resize_buffer(struct wl_window *win, int new_width, int new_height) {
    destroy_shm_buffer(win);

    win->width = new_width;
    win->height = new_height;

    const int stride = new_width * 4;
    const int size = stride * new_height;

    int fd = create_anonymous_file(size);
    if (fd < 0) {
        fprintf(stderr, "Failed to create shm file\n");
        return false;
    }

    void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        close(fd);
        return false;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(win->ctx->shm, fd, size);
    struct wl_buffer *buffer =
        wl_shm_pool_create_buffer(pool, 0, new_width, new_height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    if (!buffer) {
        fprintf(stderr, "Failed to create wl_buffer\n");
        munmap(data, size);
        return false;
    }

    win->buffer = buffer;
    win->data = data;

    win->cairo_surface = cairo_image_surface_create_for_data(
        (unsigned char*)data,
        CAIRO_FORMAT_ARGB32,
        new_width,
        new_height,
        stride
    );

    if (cairo_surface_status(win->cairo_surface) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to create cairo surface\n");
        destroy_shm_buffer(win);
        return false;
    }

    win->cr = cairo_create(win->cairo_surface);

    auto cr = win->cr;

    win->on_render = on_window_render;
    win->on_render(win);

    return true;
}

struct wl_window *wl_window_create(struct wl_context *ctx,
                                   int width, int height,
                                   const char *title)
{
    struct wl_window *win = new wl_window;
    win->ctx = ctx;
    win->width = width;
    win->height = height;
    win->title = title;
    win->pending_width = width;
    win->pending_height = height;
    win->buffer = NULL;
    win->pool = NULL;
    win->data = NULL;

    // 1️⃣ Create surface
    win->surface = wl_compositor_create_surface(ctx->compositor);
    if (!win->surface) {
        fprintf(stderr, "Failed to create wl_surface\n");
        delete win;
        return nullptr;
    }

    // 2️⃣ Get xdg_surface
    win->xdg_surface = xdg_wm_base_get_xdg_surface(ctx->wm_base, win->surface);
    if (!win->xdg_surface) {
        fprintf(stderr, "Failed to get xdg_surface\n");
        wl_surface_destroy(win->surface);
        delete win;
        return nullptr;
    }

    // 3️⃣ Add surface listener BEFORE committing
    xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);

    // 4️⃣ Create toplevel
    win->xdg_toplevel = xdg_surface_get_toplevel(win->xdg_surface);
    if (!win->xdg_toplevel) {
        fprintf(stderr, "Failed to get xdg_toplevel\n");
        xdg_surface_destroy(win->xdg_surface);
        wl_surface_destroy(win->surface);
        delete win;
        return nullptr;
    }

    // 5️⃣ Add toplevel listener
    xdg_toplevel_add_listener(win->xdg_toplevel, &xdg_toplevel_listener, win);

    // 6️⃣ Set metadata
    xdg_toplevel_set_title(win->xdg_toplevel, title ? title : "Wayland Window");

    // 7️⃣ Single initial commit
    wl_surface_commit(win->surface);

    // 8️⃣ Wait for initial configure (some compositors require this)
    wl_display_roundtrip(ctx->display);

    // create shm so that window will be mapped
    if (!wl_window_resize_buffer(win, width, height)) {
        fprintf(stderr, "Failed to create shm buffer\n");
        // cleanup code
        return nullptr;
    }
    wl_display_roundtrip(ctx->display);

    ctx->windows.push_back(win);
    return win;
}

static void configure_layer_shell(void *data,
                        		  struct zwlr_layer_surface_v1 *surf,
                        		  uint32_t serial,
                        		  uint32_t width,
                            	  uint32_t height) {
    struct wl_window *win = (struct wl_window *)data;
    zwlr_layer_surface_v1_ack_configure(surf, serial); 
    
    if (width == 0) width = win->width;
    if (height == 0) height = win->height;
    
    wl_window_resize_buffer(win, width, height); // create shm buffer
    wl_surface_attach(win->surface, win->buffer, 0, 0);
    wl_surface_commit(win->surface);
}

static const struct zwlr_layer_surface_v1_listener layer_shell_listener = {
    .configure = configure_layer_shell,
    .closed = nullptr
};

struct wl_window *wl_layer_window_create(struct wl_context *ctx, int width, int height,
                                         zwlr_layer_shell_v1_layer layer, const char *title, bool exclusive_zone = true)
{
    struct wl_window *win = new wl_window;
    win->ctx = ctx;
    win->width = width;
    win->height = height;
    win->title = title;

    win->surface = wl_compositor_create_surface(ctx->compositor);
    win->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ctx->layer_shell, win->surface, NULL, layer, title);

    zwlr_layer_surface_v1_set_anchor(win->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(win->layer_surface, width, height);
    zwlr_layer_surface_v1_set_keyboard_interactivity(win->layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    if (exclusive_zone)
        zwlr_layer_surface_v1_set_exclusive_zone(win->layer_surface, height);

    zwlr_layer_surface_v1_add_listener(win->layer_surface, &layer_shell_listener, win);

    wl_surface_commit(win->surface);
    wl_display_roundtrip(ctx->display);

    // DO NOT create shm buffer yet — wait for configure event to tell us the real size.

    ctx->windows.push_back(win);
    return win;
}

/*
struct wl_window *wl_layer_window_create(struct wl_context *ctx, int width, int height,
                                         zwlr_layer_shell_v1_layer layer, const char *title, bool exclusive_zone = true) {
    struct wl_window *win = new wl_window;
    win->title = title;
    win->ctx = ctx;
    win->width = width;
    win->height = height;
    win->title = title;
    win->pending_width = width;
    win->pending_height = height;
    win->buffer = NULL;
    win->pool = NULL;
    win->data = NULL;

    win->surface = wl_compositor_create_surface(ctx->compositor);
    win->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ctx->layer_shell, win->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, title);

    zwlr_layer_surface_v1_set_size(win->layer_surface, width, height); // width auto, height 50
    zwlr_layer_surface_v1_set_anchor(win->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_keyboard_interactivity(win->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    if (exclusive_zone)
        zwlr_layer_surface_v1_set_margin(win->layer_surface, 0, 0, 10, 0); // width auto, height 50
    if (exclusive_zone)
        zwlr_layer_surface_v1_set_exclusive_zone(win->layer_surface, height);

    zwlr_layer_surface_v1_add_listener(win->layer_surface, &layer_shell_listener, win);

    wl_surface_commit(win->surface);

    // create and attach a tiny shm buffer so our surface becomes mapped
    wl_display_roundtrip(ctx->display);

    // create shm so that window will be mapped
    if (!wl_window_resize_buffer(win, width, height)) {
        fprintf(stderr, "Failed to create shm buffer\n");
        // cleanup code
        return nullptr;
    }
    wl_display_roundtrip(ctx->display);


    ctx->windows.push_back(win);
    return win;
}
*/

static struct wl_buffer *create_shm_buffer(struct wl_context *ctx, int width, int height) {
    int stride = width * 4;
    size_t size = stride * height;
    int fd = create_shm_file(size);
    if (fd < 0) return NULL;
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    // fill with transparent black
    memset(data, 0, size);
        // Fill with white (255,255,255) at 60% transparency (A=153)
    uint8_t *pixels = (uint8_t *) data;
    const uint8_t alpha = (255.f * .2);
    const uint8_t value = 255 * alpha / 255; // premultiplied: 153
    for (size_t i = 0; i < size; i += 4) {
        pixels[i + 0] = value; // B
        pixels[i + 1] = value; // G
        pixels[i + 2] = value; // R
        pixels[i + 3] = alpha; // A
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(ctx->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
                                                         width, height,
                                                         stride, ctx->shm_format);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    return buffer;
}

struct wl_buffer *create_shm_buffer_with_cairo(struct wl_context *ctx,
                                               int width, int height,
                                               void (**out_unmap)(void*, size_t),
                                               void **out_data,
                                               size_t *out_size)
{
    int stride = width * 4;
    size_t size = stride * height;
    int fd = create_shm_file(size);
    if (fd < 0) return NULL;

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    // Create shm pool + buffer
    struct wl_shm_pool *pool = wl_shm_create_pool(ctx->shm, fd, size);
    struct wl_buffer *buffer =
        wl_shm_pool_create_buffer(pool, 0, width, height, stride, ctx->shm_format);
    wl_shm_pool_destroy(pool);
    close(fd);

    //if (out_unmap) *out_unmap = munmap;
    if (out_data) *out_data = data;
    if (out_size) *out_size = size;
    return buffer;
}

/* ---- pointer callbacks ---- */
static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
                                 uint32_t serial, struct wl_surface *surface,
                                 wl_fixed_t sx, wl_fixed_t sy) {
    double dx = wl_fixed_to_double(sx);
    double dy = wl_fixed_to_double(sy);
    printf("pointer: enter at %.2f, %.2f\n", dx, dy);
    auto ctx = (wl_context *) data;
    for (auto w : ctx->windows) {
        if (w->surface == surface) {
            printf("pointer: enter at %.2f, %.2f for %s\n", dx, dy, w->title.data());
            w->has_pointer_focus = true;
            Event event(sx, sy);
            w->cur_x = sx;
            w->cur_y = sy;
            mouse_entered(w->root, event);
            if (w->on_render)
                w->on_render(w);
        }
    }
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
                                 uint32_t serial, struct wl_surface *surface) {
    printf("pointer: leave\n");
    auto ctx = (wl_context *) data;
    for (auto w : ctx->windows) {
        if (w->surface == surface) {
            w->has_pointer_focus = false;
            Event event;
            mouse_left(w->root, event);
            if (w->on_render)
                w->on_render(w);
        }
    }
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    double dx = wl_fixed_to_double(sx);
    double dy = wl_fixed_to_double(sy);
    auto ctx = (wl_context *) data;
    for (auto w : ctx->windows) {
        if (w->has_pointer_focus) {
            printf("pointer: motion at %.2f, %.2f for %s\n", dx, dy, w->title.data());
            Event event(sx, sy);
            w->cur_x = sx;
            w->cur_y = sy;
            move_event(w->root, event);
            if (w->on_render)
                w->on_render(w);
        }
    }
    //running = false;
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t serial, uint32_t time,
                                  uint32_t button, uint32_t state) {
    const char *st = (state == WL_POINTER_BUTTON_STATE_PRESSED) ? "pressed" : "released";
    printf("pointer: button %u %s\n", button, st);
    //win->marked_for_closing = true;
    auto ctx = (wl_context *) data;
    for (auto w : ctx->windows) {
        if (w->has_pointer_focus) {
            printf("pointer: handle button\n");
            Event event(w->cur_x, w->cur_y, button, state);
            mouse_event(w->root, event);
            if (w->on_render)
                w->on_render(w);
        }
    }

    //running = false;
    //stop_dock();
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
                                uint32_t time, uint32_t axis, wl_fixed_t value) {
    (void) wl_pointer; (void) time; (void) axis;
    double v = wl_fixed_to_double(value);
    printf("pointer: axis %u value %.2f\n", axis, v);
    auto ctx = (wl_context *) data;
    for (auto w : ctx->windows) {
        if (w->has_pointer_focus) {
            printf("pointer: handle scroll\n");
            Event event;
            event.x = w->cur_x;
            event.y = w->cur_y;
            event.scroll = true;
            event.axis = axis;
            //event.direction = direction;
            event.delta = value;
            //event.descrete = discrete;
            //event.from_mouse = from_mouse;
            mouse_event(w->root, event);
            if (w->on_render)
                w->on_render(w);
        }
    }
}

static void pointer_handle_frame(void *data,
	      struct wl_pointer *wl_pointer) {
}

static void pointer_handle_axis_source(void *data,
	    struct wl_pointer *wl_pointer,
	    uint32_t axis_source) {
}

static void pointer_handle_axis_stop(void *data,
		  struct wl_pointer *wl_pointer,
		  uint32_t time,
		  uint32_t axis) {
}

static void pointer_handle_axis_discrete(void *data,
		      struct wl_pointer *wl_pointer,
		      uint32_t axis,
		      int32_t discrete) {
}

static void pointer_handle_axis_value120(void *data,
		      struct wl_pointer *wl_pointer,
		      uint32_t axis,
		      int32_t value120) {
}

static void pointer_handle_axis_relative_direction(void *data,
				struct wl_pointer *wl_pointer,
				uint32_t axis,
				uint32_t direction) {
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
    .axis_value120 = pointer_handle_axis_value120,
    .axis_relative_direction = pointer_handle_axis_relative_direction,
};

/* ---- keyboard callbacks ---- */
static void keyboard_handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
                                   uint32_t format, int fd, uint32_t size) {
    wl_context *ctx = (wl_context *) data;
    wl_window *win = nullptr;
    for (auto w : ctx->windows)
        if (w->has_keyboard_focus)
            win = w;
    if (!win) {
        for (auto w : ctx->windows)
            if (w->has_pointer_focus)
                win = w;
    }
    if (!win)
        return;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char *map_shm = (char *) mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_shm == MAP_FAILED) {
        close(fd);
        return;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_string(ctx->xkb_ctx,
                                                           map_shm,
                                                           XKB_KEYMAP_FORMAT_TEXT_V1,
                                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        printf("Failed to compile xkb keymap\n");
    } else {
        if (win->keymap) xkb_keymap_unref(win->keymap);
        if (win->xkb_state) xkb_state_unref(win->xkb_state);
        win->keymap = keymap;
        win->xkb_state = xkb_state_new(win->keymap);
    }

    munmap(map_shm, size);
    close(fd);
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *wl_keyboard,
                                 uint32_t serial, struct wl_surface *surface,
                                 struct wl_array *keys) {
    //(void) wl_keyboard; (void) serial; (void) surface; (void) keys;
    auto ctx = (wl_context *) data;
    printf("keyboard: enter (focus)\n");
    for (auto w : ctx->windows) {
        if (w->surface == surface) {
            w->has_keyboard_focus = true;
            if (w->on_render)
                w->on_render(w);
        }
    }
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *wl_keyboard,
                                 uint32_t serial, struct wl_surface *surface) {
    //(void) wl_keyboard; (void) serial; (void) surface;
    printf("keyboard: leave (lost focus)\n");
    auto ctx = (wl_context *) data;
    for (auto w : ctx->windows) {
        if (w->surface == surface) {
            w->has_keyboard_focus = false;
            if (w->on_render)
                w->on_render(w);
        }
    }
}


static void keyboard_handle_key(void *data, struct wl_keyboard *wl_keyboard,
                                uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    wl_context *ctx = (wl_context *) data;
    wl_window *win = nullptr;
    for (auto w : ctx->windows)
        if (w->has_keyboard_focus)
            win = w;
    if (!win) {
        for (auto w : ctx->windows)
            if (w->has_pointer_focus)
                win = w;
    }
    if (!win)
        return;
                                    
    //display *d = (display *) data;
    const char *st = (state == WL_KEYBOARD_KEY_STATE_PRESSED) ? "pressed" : "released";
    printf("keyboard: key %u %s for %s", key, st, win->title.data());

    if (win->xkb_state) {
        // Wayland keycodes are +8 from evdev
        xkb_keysym_t sym = xkb_state_key_get_one_sym(win->xkb_state, key + 8);
        char buf[64];
        int n = xkb_keysym_get_name(sym, buf, sizeof(buf));
        if (n > 0) {
            printf(" -> %s", buf);
        } else {
            // try printable UTF-8
            char utf8[64];
            int len = xkb_keysym_to_utf8(sym, utf8, sizeof(utf8));
            if (len > 0) {
                printf(" -> '%s'", utf8);
            }
        }
    }
    printf("\n");

    if (win->on_render)
        win->on_render(win);
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *wl_keyboard,
                                      uint32_t serial, uint32_t mods_depressed,
                                      uint32_t mods_latched, uint32_t mods_locked,
                                      uint32_t group) {
    (void)data; (void)wl_keyboard; (void)serial;
    (void)mods_depressed; (void)mods_latched; (void)mods_locked; (void)group;
    // Not printing modifiers in this minimal example
    // 
}

static void keyboard_handle_repeat_info(void *data,
                            		    struct wl_keyboard *wl_keyboard,
                            		    int32_t rate,
                            		    int32_t delay) {
                    		   
}


static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info,
};

/* ---- seat listener ---- */
static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    wl_context *d = (wl_context *) data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !d->pointer) {
        d->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(d->pointer, &pointer_listener, d);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && d->pointer) {
        wl_pointer_destroy(d->pointer);
        d->pointer = NULL;
    }

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !d->keyboard) {
        d->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(d->keyboard, &keyboard_listener, d);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && d->keyboard) {
        wl_keyboard_destroy(d->keyboard);
        d->keyboard = NULL;
    }
}

static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

/* ---- xdg_wm_base ping handler ---- */
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping
};

/* ---- registry ---- */
static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t id, const char *interface, uint32_t version) {
    wl_context *d = (wl_context *)data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        d->compositor = (wl_compositor *) wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        d->shm = (wl_shm *) wl_registry_bind(registry, id, &wl_shm_interface, 1);
        d->shm_format = WL_SHM_FORMAT_ARGB8888;
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        d->seat = (wl_seat *) wl_registry_bind(registry, id, &wl_seat_interface, 5);
        wl_seat_add_listener(d->seat, &seat_listener, d);
        printf("here\n");
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        d->wm_base = (xdg_wm_base *) wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(d->wm_base, &wm_base_listener, d);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        d->layer_shell = (zwlr_layer_shell_v1 *) wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, 5);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        //d->output = (wl_output *) wl_registry_bind(registry, id, &wl_output_interface, 3);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
    (void)data; (void)registry; (void)id;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove
};

struct wl_context *wl_context_create(void) {
    wl_context *ctx = new wl_context;
    ctx->display = wl_display_connect(NULL);
    if (!ctx->display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        free(ctx);
        return NULL;
    }

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display); // populate globals

    ctx->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    ctx->shm_format = WL_SHM_FORMAT_ARGB8888; // default; discover if needed
    return ctx;
}

void wl_window_destroy(struct wl_window *win) {
    if (!win) return;

    if (win->xdg_toplevel) xdg_toplevel_destroy(win->xdg_toplevel);
    if (win->xdg_surface) xdg_surface_destroy(win->xdg_surface);
    if (win->layer_surface) zwlr_layer_surface_v1_destroy(win->layer_surface);

    if (win->surface) wl_surface_destroy(win->surface);

    if (win->xkb_state) xkb_state_unref(win->xkb_state);
    if (win->keymap) xkb_keymap_unref(win->keymap);

    free(win);
}

void wl_context_destroy(struct wl_context *ctx) {
    if (!ctx) return;

    for (auto w : ctx->windows)
        wl_window_destroy(w); 

    if (ctx->keyboard) wl_keyboard_release(ctx->keyboard);
    if (ctx->pointer) wl_pointer_release(ctx->pointer);
    if (ctx->seat) wl_seat_release(ctx->seat);
    if (ctx->shm) wl_shm_destroy(ctx->shm);
    if (ctx->compositor) wl_compositor_destroy(ctx->compositor);
    if (ctx->wm_base) xdg_wm_base_destroy(ctx->wm_base);
    if (ctx->layer_shell) zwlr_layer_shell_v1_destroy(ctx->layer_shell);
    if (ctx->xkb_ctx) xkb_context_unref(ctx->xkb_ctx);

    wl_registry_destroy(ctx->registry);
    wl_display_disconnect(ctx->display);
    free(ctx);
}

int wake_pipe[2];

int open_dock() {
    pipe2(wake_pipe, O_CLOEXEC | O_NONBLOCK);
    
    struct wl_context *ctx = wl_context_create();
    if (!ctx) {
        fprintf(stderr, "Failed to initialize Wayland context\n");
        return 1;
    }

/*
    auto dock = wl_layer_window_create(ctx, 0, 48, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "mylardesktop:dock");
    dock->root->user_data = dock;
    dock->root->when_paint = [](Container *root, Container *c) {
        auto dock = (wl_window *) root->user_data;
        auto cr = dock->cr;
        cairo_set_source_rgba(cr, 1, 1, 1, 1); 
        cairo_rectangle(cr, c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h); 
        cairo_fill(cr);
    };
    */
    
    //auto dock = wl_layer_window_create(ctx, 500, 500, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "quickshell:dock", false);
    auto dock = wl_layer_window_create(ctx, 0, 48, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "quickshell:dock");
    
    dock->root->user_data = dock;
    dock->root->when_paint = [](Container *root, Container *c) {
        auto dock = (wl_window *) root->user_data;
        auto cr = dock->cr;
        cairo_set_source_rgba(cr, 1, 1, 1, 1); 
        //notify(std::to_string(c->real_bounds.w));
        cairo_rectangle(cr, c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h); 
        cairo_fill(cr);
        //notify("here");
    };
    
    dock->on_render(dock);

/*
    auto settings = wl_window_create(ctx, 800, 600, "Settings");
    settings->root->user_data = settings;
    settings->root->when_paint = [](Container *root, Container *c) {
        auto settings = (wl_window *) root->user_data;
        auto cr = settings->cr;
        cairo_set_source_rgba(cr, 1, 0, 1, 1); 
        //notify(std::to_string(c->real_bounds.w));
        cairo_rectangle(cr, c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h); 
        cairo_fill(cr);
        //notify("here");
    };
    settings->on_render(settings);
    */
    
    
    //wl_window_create(ctx, 800, 600, "Onboarding");
    
    printf("Window created. Entering main loop.\n");

    bool need_flush = false;
    int fd = wl_display_get_fd(ctx->display);

    int x = 0;
    while (running) {
        // Dispatch any already-queued events before blocking
        wl_display_dispatch_pending(ctx->display);
        
        /*
        // 2. REDRAW phase — right here
        for (auto win : ctx->windows) {
            if (win->needs_redraw) {
                wl_window_draw(win);        // your Cairo/OpenGL draw
                wl_surface_commit(win->surface);
                win->needs_redraw = false;
            }
        }
        */

        // Try to flush before waiting
        if (wl_display_flush(ctx->display) < 0 && errno == EAGAIN)
            need_flush = true;

        short events = POLLIN | POLLERR;
        if (need_flush)
            events |= POLLOUT;

        struct pollfd pfds[2] = {
            { .fd = fd, .events = events },
            { .fd = wake_pipe[0], .events = POLLIN },
        };

        if (poll(pfds, 2, -1) < 0)
            break;

        if (pfds[1].revents & POLLIN) {
            char buf[64];
            read(wake_pipe[0], buf, sizeof buf);
            continue;
        }

        if (pfds[0].revents & POLLIN) {
            if (wl_display_prepare_read(ctx->display) == 0) {
                wl_display_read_events(ctx->display);
                wl_display_dispatch_pending(ctx->display);
            } else {
                wl_display_dispatch_pending(ctx->display);
            }
        }

        if (pfds[0].revents & POLLOUT) {
            if (wl_display_flush(ctx->display) == 0)
                need_flush = false;
        }

        // Now handle your app-level window cleanup
        for (int i = ctx->windows.size() - 1; i >= 0; i--) {
            auto win = ctx->windows[i];
            if (win->marked_for_closing) {
                wl_window_destroy(win);
                ctx->windows.erase(ctx->windows.begin() + i);
            }
        }
    }
    

/*
    int x = 0;
    while (running) {
        //printf("x: %d\n", x++);
        short events = POLLIN | POLLERR;
        if (need_flush)
            events |= POLLOUT;

        struct pollfd pfds[2] = {
            { .fd = fd, .events = events },
            { .fd = wake_pipe[0], .events = POLLIN },
        };

        if (poll(pfds, 2, -1) < 0)
            break;

        if (pfds[1].revents & POLLIN) {
            char buf[64];
            read(wake_pipe[0], buf, sizeof buf);
            continue;
        }

        if (pfds[0].revents & POLLIN) {
            if (wl_display_dispatch(ctx->display) < 0)
                break;
        }

        if (pfds[0].revents & POLLOUT) {
            if (wl_display_flush(ctx->display) == 0)
                need_flush = false;
        }

        if (wl_display_flush(ctx->display) < 0 && errno == EAGAIN)
            need_flush = true;


        bool removed = false;
        for (int i = ctx->windows.size() - 1; i >= 0; i--) {
            auto win = ctx->windows[i];
            if (win->marked_for_closing) {
                removed = true;
                wl_window_destroy(win); 
                ctx->windows.erase(ctx->windows.begin() + i);
                
            }
        }
        if (removed)
            wl_display_roundtrip(ctx->display);
    }
    */


   
    // 4. Cleanup
    wl_context_destroy(ctx); 

    return 0;
}

void start_dock() {
    open_dock();     
}

void wake_display_loop() {
    write(wake_pipe[1], "x", 1); // wakes poll
}

void stop_dock() {
    running = false;
    wake_display_loop();    
}

/*int main() {
    start_dock();   
    return 0;
}*/


