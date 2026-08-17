#include "wayland-core.h"

#include "input.h"
#include "render.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_monotonic_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;

    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base,
        uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void layer_surface_configure(void *data,
        struct zwlr_layer_surface_v1 *layer_surface, uint32_t serial,
        uint32_t width, uint32_t height) {
    struct app_context *ctx = data;

    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    ctx->width = width > 0 ? (int)width : ctx->requested_width;
    ctx->height = height > 0 ? (int)height : ctx->requested_height;
    ctx->configured = 1;
    render_frame(ctx);
}

static void layer_surface_closed(void *data,
        struct zwlr_layer_surface_v1 *layer_surface) {
    (void)layer_surface;
    app_cancel(data);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
        uint32_t serial) {
    struct app_context *ctx = data;

    xdg_surface_ack_configure(xdg_surface, serial);

    if (ctx->width <= 0) ctx->width = ctx->requested_width;
    if (ctx->height <= 0) ctx->height = ctx->requested_height;

    ctx->configured = 1;
    render_frame(ctx);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data,
        struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height,
        struct wl_array *states) {
    struct app_context *ctx = data;

    (void)xdg_toplevel;
    (void)states;

    if (width > 0) ctx->width = width;
    if (height > 0) ctx->height = height;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    (void)xdg_toplevel;
    app_cancel(data);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t id, const char *interface, uint32_t version) {
    struct app_context *ctx = data;

    if (!ctx) return;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        uint32_t bind_ver = version < 4 ? version : 4;
        ctx->compositor = wl_registry_bind(registry, id,
                &wl_compositor_interface, bind_ver);
    }
    else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        uint32_t bind_ver = version < 4 ? version : 4;
        ctx->layer_shell = wl_registry_bind(registry, id,
                &zwlr_layer_shell_v1_interface, bind_ver);
    }
    else if (strcmp(interface, wl_shm_interface.name) == 0) {
        ctx->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    }
    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        ctx->seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
        wl_seat_add_listener(ctx->seat, &seat_listener, ctx);
    }
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        ctx->xdg_wm_base = wl_registry_bind(registry, id,
                &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(ctx->xdg_wm_base, &xdg_wm_base_listener, ctx);
    }
}

static void registry_handle_global_remove(void *data,
        struct wl_registry *registry, uint32_t id) {
    (void)data;
    (void)registry;
    (void)id;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static int create_surface(struct app_context *ctx) {
    ctx->surface = wl_compositor_create_surface(ctx->compositor);
    if (!ctx->surface) return -1;

    if (ctx->layer_shell) {
        uint32_t anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;

        ctx->use_layer_shell = 1;
        ctx->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
                ctx->layer_shell, ctx->surface, NULL,
                ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wayland-ssh-askpass");
        if (!ctx->layer_surface) return -1;

        zwlr_layer_surface_v1_set_anchor(ctx->layer_surface, anchors);
        zwlr_layer_surface_v1_set_size(ctx->layer_surface,
                (uint32_t)ctx->requested_width,
                (uint32_t)ctx->requested_height);
        zwlr_layer_surface_v1_set_exclusive_zone(ctx->layer_surface, 0);
        zwlr_layer_surface_v1_set_keyboard_interactivity(ctx->layer_surface,
                ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
        zwlr_layer_surface_v1_add_listener(ctx->layer_surface,
                &layer_surface_listener, ctx);
    }
    else if (ctx->xdg_wm_base) {
        ctx->xdg_surface = xdg_wm_base_get_xdg_surface(ctx->xdg_wm_base,
                ctx->surface);
        if (!ctx->xdg_surface) return -1;

        ctx->xdg_toplevel = xdg_surface_get_toplevel(ctx->xdg_surface);
        if (!ctx->xdg_toplevel) return -1;

        xdg_surface_add_listener(ctx->xdg_surface, &xdg_surface_listener, ctx);
        xdg_toplevel_add_listener(ctx->xdg_toplevel,
                &xdg_toplevel_listener, ctx);
        xdg_toplevel_set_title(ctx->xdg_toplevel, "SSH Askpass");
        xdg_toplevel_set_app_id(ctx->xdg_toplevel, "wayland-ssh-askpass");
        xdg_toplevel_set_min_size(ctx->xdg_toplevel, ctx->requested_width,
                ctx->requested_height);
        xdg_toplevel_set_max_size(ctx->xdg_toplevel, ctx->requested_width,
                ctx->requested_height);
    }
    else {
        fprintf(stderr, "wayland-ssh-askpass: no layer-shell or xdg-shell support available\n");
        return -1;
    }

    wl_surface_commit(ctx->surface);
    wl_display_flush(ctx->display);
    return 0;
}

int app_context_init(struct app_context *ctx, const char *prompt) {
    memset(ctx, 0, sizeof(*ctx));

    ctx->prompt = prompt;
    ctx->running = 1;
    ctx->requested_width = ASKPASS_DEFAULT_WIDTH;
    ctx->requested_height = ASKPASS_DEFAULT_HEIGHT;
    ctx->width = ctx->requested_width;
    ctx->height = ctx->requested_height;
    ctx->deadline_ms = now_monotonic_ms() + (ASKPASS_TIMEOUT_SECONDS * 1000ULL);

    if (input_init(ctx) != 0) return -1;

    ctx->display = wl_display_connect(NULL);
    if (!ctx->display) {
        fprintf(stderr, "wayland-ssh-askpass: failed to connect to Wayland display\n");
        return -1;
    }

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display);
    wl_display_roundtrip(ctx->display);

    if (!ctx->compositor || !ctx->shm || (!ctx->layer_shell && !ctx->xdg_wm_base)) {
        fprintf(stderr, "wayland-ssh-askpass: required Wayland interfaces are unavailable\n");
        return -1;
    }

    return create_surface(ctx);
}

int app_remaining_timeout_ms(const struct app_context *ctx) {
    uint64_t now_ms = now_monotonic_ms();

    if (now_ms >= ctx->deadline_ms) return 0;

    return (int)(ctx->deadline_ms - now_ms);
}

int app_dispatch_events(struct app_context *ctx, int timeout_ms) {
    struct pollfd pfd = {
        .fd = wl_display_get_fd(ctx->display),
        .events = POLLIN,
        .revents = 0,
    };
    int ret;

    while (wl_display_prepare_read(ctx->display) != 0) {
        if (wl_display_dispatch_pending(ctx->display) < 0) return -1;
    }

    if (wl_display_flush(ctx->display) < 0) {
        wl_display_cancel_read(ctx->display);
        return -1;
    }

    ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
        wl_display_cancel_read(ctx->display);
        if (errno == EINTR) return 0;
        return -1;
    }

    if (ret == 0) {
        wl_display_cancel_read(ctx->display);
        return 0;
    }

    if (pfd.revents & POLLIN) {
        if (wl_display_read_events(ctx->display) < 0) return -1;
        if (wl_display_dispatch_pending(ctx->display) < 0) return -1;
    }
    else {
        wl_display_cancel_read(ctx->display);
    }

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;

    return 1;
}

void app_cancel(struct app_context *ctx) {
    ctx->cancelled = 1;
    ctx->running = 0;
}

void app_submit(struct app_context *ctx) {
    ctx->submitted = 1;
    ctx->running = 0;
}

void app_context_cleanup(struct app_context *ctx) {
    if (ctx->surface) {
        wl_surface_attach(ctx->surface, NULL, 0, 0);
        wl_surface_commit(ctx->surface);
    }

    if (ctx->display) wl_display_flush(ctx->display);
    if (ctx->display) wl_display_roundtrip(ctx->display);
    if (ctx->layer_surface) zwlr_layer_surface_v1_destroy(ctx->layer_surface);
    if (ctx->xdg_toplevel) xdg_toplevel_destroy(ctx->xdg_toplevel);
    if (ctx->xdg_surface) xdg_surface_destroy(ctx->xdg_surface);
    if (ctx->surface) wl_surface_destroy(ctx->surface);
    if (ctx->buffer) wl_buffer_destroy(ctx->buffer);
    if (ctx->xdg_wm_base) xdg_wm_base_destroy(ctx->xdg_wm_base);
    input_cleanup(ctx);
    if (ctx->layer_shell) zwlr_layer_shell_v1_destroy(ctx->layer_shell);
    if (ctx->shm) wl_shm_destroy(ctx->shm);
    if (ctx->compositor) wl_compositor_destroy(ctx->compositor);
    if (ctx->registry) wl_registry_destroy(ctx->registry);
    if (ctx->display) wl_display_disconnect(ctx->display);
}
