#define _GNU_SOURCE

#include "input.h"
#include "render.h"
#include "secure.h"

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
        uint32_t format, int32_t fd, uint32_t size) {
    struct app_context *ctx = data;
    char *map_str;

    (void)keyboard;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    if (ctx->xkb_keymap) xkb_keymap_unref(ctx->xkb_keymap);
    if (ctx->xkb_state) xkb_state_unref(ctx->xkb_state);

    ctx->xkb_keymap = xkb_keymap_new_from_string(ctx->xkb_context, map_str,
            XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);

    munmap(map_str, size);
    close(fd);

    if (!ctx->xkb_keymap) return;

    ctx->xkb_state = xkb_state_new(ctx->xkb_keymap);
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface) {
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
        uint32_t mods_locked, uint32_t group) {
    struct app_context *ctx = data;

    (void)keyboard;
    (void)serial;

    if (!ctx->xkb_state) return;

    xkb_state_update_mask(ctx->xkb_state, mods_depressed, mods_latched,
            mods_locked, 0, 0, group);
}

static void keyboard_handle_repeat_info(void *data,
        struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
}

static size_t password_pop_codepoint(char *buffer, size_t len) {
    if (len == 0) return 0;

    len--;
    while (len > 0 && (((unsigned char)buffer[len] & 0xC0) == 0x80)) {
        len--;
    }

    buffer[len] = '\0';
    return len;
}

static void redraw_if_configured(struct app_context *ctx) {
    if (ctx->configured && ctx->width > 0 && ctx->height > 0) {
        render_frame(ctx);
    }
}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    struct app_context *ctx = data;
    uint32_t xkb_keycode;
    char utf8_buf[32] = {0};
    int utf8_len;

    (void)keyboard;
    (void)serial;
    (void)time;

    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;

    if (key == KEY_ESC) {
        app_cancel(ctx);
        return;
    }

    if (key == KEY_ENTER || key == KEY_KPENTER) {
        app_submit(ctx);
        return;
    }

    if (key == KEY_BACKSPACE) {
        ctx->password_len = password_pop_codepoint(ctx->password,
                ctx->password_len);
        redraw_if_configured(ctx);
        return;
    }

    if (!ctx->xkb_state) return;

    xkb_keycode = key + 8;
    utf8_len = xkb_state_key_get_utf8(ctx->xkb_state, xkb_keycode, utf8_buf,
            sizeof(utf8_buf));

    if (utf8_len <= 0 || utf8_buf[0] == '\0') return;
    if ((unsigned char)utf8_buf[0] < 0x20 || utf8_buf[0] == 0x7F) return;
    if ((ctx->password_len + (size_t)utf8_len) >= ctx->password_capacity) return;

    memcpy(ctx->password + ctx->password_len, utf8_buf, (size_t)utf8_len);
    ctx->password_len += (size_t)utf8_len;
    ctx->password[ctx->password_len] = '\0';

    redraw_if_configured(ctx);
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
        uint32_t capabilities) {
    struct app_context *ctx = data;

    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !ctx->keyboard) {
        ctx->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(ctx->keyboard, &keyboard_listener, ctx);
        return;
    }

    if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && ctx->keyboard) {
        wl_keyboard_destroy(ctx->keyboard);
        ctx->keyboard = NULL;
    }
}

static void seat_handle_name(void *data, struct wl_seat *seat,
        const char *name) {
    (void)data;
    (void)seat;
    (void)name;
}

const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

int input_init(struct app_context *ctx) {
    ctx->password_capacity = ASKPASS_PASSWORD_CAPACITY;
    ctx->password = calloc(ctx->password_capacity, sizeof(char));
    if (!ctx->password) return -1;

    ctx->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx->xkb_context) return -1;

    return 0;
}

void input_cleanup(struct app_context *ctx) {
    if (ctx->keyboard) wl_keyboard_destroy(ctx->keyboard);
    if (ctx->seat) wl_seat_destroy(ctx->seat);
    if (ctx->xkb_state) xkb_state_unref(ctx->xkb_state);
    if (ctx->xkb_keymap) xkb_keymap_unref(ctx->xkb_keymap);
    if (ctx->xkb_context) xkb_context_unref(ctx->xkb_context);
    if (ctx->password) {
        secure_clear_memory(ctx->password, ctx->password_capacity);
        free(ctx->password);
        ctx->password = NULL;
    }
}
