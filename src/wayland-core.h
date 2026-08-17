#ifndef WAYLAND_CORE_H
#define WAYLAND_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define ASKPASS_TIMEOUT_SECONDS 30
#define ASKPASS_PASSWORD_CAPACITY 1024
#define ASKPASS_DEFAULT_WIDTH 560
#define ASKPASS_DEFAULT_HEIGHT 180

struct app_context {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct xdg_wm_base *xdg_wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct wl_buffer *buffer;
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    const char *prompt;
    char *password;
    size_t password_len;
    size_t password_capacity;
    uint64_t deadline_ms;
    int running;
    int configured;
    int submitted;
    int cancelled;
    int timed_out;
    int width;
    int height;
    int requested_width;
    int requested_height;
    int use_layer_shell;
};

int app_context_init(struct app_context *ctx, const char *prompt);
void app_context_cleanup(struct app_context *ctx);
int app_dispatch_events(struct app_context *ctx, int timeout_ms);
int app_remaining_timeout_ms(const struct app_context *ctx);
void app_cancel(struct app_context *ctx);
void app_submit(struct app_context *ctx);

#endif
