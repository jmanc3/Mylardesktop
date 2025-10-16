// wl_input.c
#define _POSIX_C_SOURCE 200809L
#include <poll.h>    // for POLLIN, POLLOUT, POLLERR, etc.
#include <errno.h>   // for EAGAIN, EINTR, and other errno constants
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h" // generated via wayland-scanner

#include <xkbcommon/xkbcommon.h>

struct display {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;
    struct xkb_context *xkb_ctx;
    struct xkb_keymap *keymap;
    struct xkb_state *xkb_state;
    uint32_t shm_format;
    int width, height;
};

static struct display g = {0};
bool running = true;

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

static struct wl_buffer *create_shm_buffer(struct display *d, int width, int height) {
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

    struct wl_shm_pool *pool = wl_shm_create_pool(d->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
                                                         width, height,
                                                         stride, d->shm_format);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    return buffer;
}

/* ---- pointer callbacks ---- */
static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
                                 uint32_t serial, struct wl_surface *surface,
                                 wl_fixed_t sx, wl_fixed_t sy) {
    (void) data; (void) wl_pointer; (void) serial; (void) surface;
    double dx = wl_fixed_to_double(sx);
    double dy = wl_fixed_to_double(sy);
    printf("pointer: enter at %.2f, %.2f\n", dx, dy);
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
                                 uint32_t serial, struct wl_surface *surface) {
    (void) data; (void) wl_pointer; (void) serial; (void) surface;
    printf("pointer: leave\n");
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    (void) data; (void) wl_pointer; (void) time;
    double dx = wl_fixed_to_double(sx);
    double dy = wl_fixed_to_double(sy);
    printf("pointer: motion at %.2f, %.2f\n", dx, dy);
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t serial, uint32_t time,
                                  uint32_t button, uint32_t state) {
    (void) data; (void) wl_pointer; (void) serial; (void) time;
    const char *st = (state == WL_POINTER_BUTTON_STATE_PRESSED) ? "pressed" : "released";
    printf("pointer: button %u %s\n", button, st);
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
                                uint32_t time, uint32_t axis, wl_fixed_t value) {
    (void) data; (void) wl_pointer; (void) time; (void) axis;
    double v = wl_fixed_to_double(value);
    printf("pointer: axis %u value %.2f\n", axis, v);
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
    display *d = (display *) data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char *map_shm = (char *) mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_shm == MAP_FAILED) {
        close(fd);
        return;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_string(d->xkb_ctx,
                                                           map_shm,
                                                           XKB_KEYMAP_FORMAT_TEXT_V1,
                                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        printf("Failed to compile xkb keymap\n");
    } else {
        if (d->keymap) xkb_keymap_unref(d->keymap);
        if (d->xkb_state) xkb_state_unref(d->xkb_state);
        d->keymap = keymap;
        d->xkb_state = xkb_state_new(d->keymap);
    }

    munmap(map_shm, size);
    close(fd);
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *wl_keyboard,
                                 uint32_t serial, struct wl_surface *surface,
                                 struct wl_array *keys) {
    (void) wl_keyboard; (void) serial; (void) surface; (void) keys;
    printf("keyboard: enter (focus)\n");
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *wl_keyboard,
                                 uint32_t serial, struct wl_surface *surface) {
    (void) data; (void) wl_keyboard; (void) serial; (void) surface;
    printf("keyboard: leave (lost focus)\n");
}

static void keyboard_handle_key(void *data, struct wl_keyboard *wl_keyboard,
                                uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    display *d = (display *) data;
    const char *st = (state == WL_KEYBOARD_KEY_STATE_PRESSED) ? "pressed" : "released";
    printf("keyboard: key %u %s", key, st);

    if (d->xkb_state) {
        // Wayland keycodes are +8 from evdev
        xkb_keysym_t sym = xkb_state_key_get_one_sym(d->xkb_state, key + 8);
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
    display *d = (display *) data;
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
    display *d = (display *)data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        d->compositor = (wl_compositor *) wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        d->shm = (wl_shm *) wl_registry_bind(registry, id, &wl_shm_interface, 1);
        d->shm_format = WL_SHM_FORMAT_XRGB8888;
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        d->seat = (wl_seat *) wl_registry_bind(registry, id, &wl_seat_interface, 5);
        wl_seat_add_listener(d->seat, &seat_listener, d);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        d->wm_base = (xdg_wm_base *) wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(d->wm_base, &wm_base_listener, d);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
    (void)data; (void)registry; (void)id;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove
};

static void handle_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    printf("Compositor requested window close\n");
    running = false;  // set your main loop flag to exit
}

static void handle_toplevel_configure(
    void *data,
    struct xdg_toplevel *toplevel,
    int32_t width,
    int32_t height,
    struct wl_array *states) 
{
    // Usually you’d handle resize here
    printf("size reconfigured\n");
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = handle_toplevel_configure,
    .close = handle_toplevel_close,
};

int open_space() {
    struct display *d = &g;
    d->display = wl_display_connect(NULL);
    if (!d->display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return 1;
    }

    d->registry = wl_display_get_registry(d->display);
    wl_registry_add_listener(d->registry, &registry_listener, d);
    wl_display_roundtrip(d->display);

    if (!d->compositor || !d->wm_base || !d->shm) {
        fprintf(stderr, "Wayland compositor did not provide required globals (compositor, shm, xdg_wm_base)\n");
        return 1;
    }

    d->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    d->width = 320; d->height = 240;

    d->surface = wl_compositor_create_surface(d->compositor);
    d->xdg_surface = xdg_wm_base_get_xdg_surface(d->wm_base, d->surface);
    d->xdg_toplevel = xdg_surface_get_toplevel(d->xdg_surface);
    xdg_toplevel_add_listener(d->xdg_toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(d->xdg_toplevel, "wl-input - minimal");

    wl_surface_commit(d->surface);

    // create and attach a tiny shm buffer so our surface becomes mapped
    wl_display_roundtrip(d->display);
    struct wl_buffer *buf = create_shm_buffer(d, d->width, d->height);
    if (!buf) {
        fprintf(stderr, "Failed to create shm buffer\n");
        return 1;
    }
    wl_surface_attach(d->surface, buf, 0, 0);
    wl_surface_damage(d->surface, 0, 0, d->width, d->height);
    wl_surface_commit(d->surface);

    // enter main loop: listen for events and print pointer/keyboard events
    printf("Running. Move mouse over window or press keys. Ctrl+C to quit.\n");

    int fd = wl_display_get_fd(d->display);
    while (running) {
        if (wl_display_flush(d->display) < 0 && errno != EAGAIN) {
            perror("wl_display_flush");
            break;
        }

        struct pollfd pfd = { .fd = fd, .events = POLLIN | POLLOUT | POLLERR };
        if (poll(&pfd, 1, -1) < 0) {
            perror("poll");
            break;
        }

        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(d->display) < 0) break;
        } else {
            wl_display_dispatch_pending(d->display);
        }
    }



    //while (wl_display_dispatch(d->display) != -1) {
        // dispatch returns when events processed; loop continues until error
    //}

    // cleanup (not strictly necessary if process is exiting)
    if (d->pointer) wl_pointer_destroy(d->pointer);
    if (d->keyboard) wl_keyboard_destroy(d->keyboard);
    if (d->seat) wl_seat_destroy(d->seat);
    if (d->xdg_toplevel) xdg_toplevel_destroy(d->xdg_toplevel);
    if (d->xdg_surface) xdg_surface_destroy(d->xdg_surface);
    if (d->surface) wl_surface_destroy(d->surface);
    if (d->shm) wl_shm_destroy(d->shm);
    if (d->compositor) wl_compositor_destroy(d->compositor);
    if (d->registry) wl_registry_destroy(d->registry);
    if (d->display) wl_display_disconnect(d->display);

    if (d->xkb_state) xkb_state_unref(d->xkb_state);
    if (d->keymap) xkb_keymap_unref(d->keymap);
    if (d->xkb_ctx) xkb_context_unref(d->xkb_ctx);

    return 0;
}

void start_test() {
    open_space();     
}
