#define _GNU_SOURCE

#include "render.h"

#include <cairo.h>
#include <fcntl.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int allocate_shm_file(size_t size) {
    int fd = memfd_create("wayland-ssh-askpass-buffer", MFD_CLOEXEC);

    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void buffer_release(void *data, struct wl_buffer *buffer) {
    struct app_context *ctx = data;

    if (ctx && ctx->buffer == buffer) {
        ctx->buffer = NULL;
    }

    wl_buffer_destroy(buffer);
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static char *mask_password(size_t len) {
    char *mask = calloc(len + 1, sizeof(char));

    if (!mask) return NULL;

    memset(mask, '*', len);
    return mask;
}

void render_frame(struct app_context *ctx) {
    const int border_radius = 18;
    const int padding = 24;
    const int input_height = 54;
    const int prompt_width = ctx->width - (padding * 2);
    const int stride = ctx->width * 4;
    const int size = stride * ctx->height;
    int fd;
    uint32_t *data;
    cairo_surface_t *surface;
    cairo_t *cr;
    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;
    PangoLayout *layout;
    PangoFontDescription *prompt_font;
    PangoFontDescription *input_font;
    char *masked;
    int prompt_height;
    int input_y;
    double mask_width = 0.0;

    if (ctx->width <= 0 || ctx->height <= 0 || !ctx->surface || !ctx->shm) return;

    fd = allocate_shm_file((size_t)size);
    if (fd < 0) return;

    data = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return;
    }

    surface = cairo_image_surface_create_for_data((unsigned char *)data,
            CAIRO_FORMAT_ARGB32, ctx->width, ctx->height, stride);
    cr = cairo_create(surface);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    cairo_new_path(cr);
    cairo_arc(cr, ctx->width - border_radius, border_radius, border_radius,
            -1.57079632679, 0.0);
    cairo_arc(cr, ctx->width - border_radius, ctx->height - border_radius,
            border_radius, 0.0, 1.57079632679);
    cairo_arc(cr, border_radius, ctx->height - border_radius, border_radius,
            1.57079632679, 3.14159265359);
    cairo_arc(cr, border_radius, border_radius, border_radius,
            3.14159265359, 4.71238898038);
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, 0.086, 0.094, 0.118, 0.96);
    cairo_fill_preserve(cr);

    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgba(cr, 0.376, 0.490, 0.984, 0.90);
    cairo_stroke(cr);

    layout = pango_cairo_create_layout(cr);
    prompt_font = pango_font_description_from_string("Sans 13");
    input_font = pango_font_description_from_string("Sans Bold 14");

    pango_layout_set_width(layout, prompt_width * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_font_description(layout, prompt_font);
    pango_layout_set_text(layout, ctx->prompt, -1);
    pango_layout_get_pixel_size(layout, NULL, &prompt_height);

    cairo_set_source_rgb(cr, 0.925, 0.941, 0.969);
    cairo_move_to(cr, padding, padding);
    pango_cairo_show_layout(cr, layout);

    input_y = padding + prompt_height + 18;

    cairo_new_path(cr);
    cairo_arc(cr, ctx->width - padding - 10, input_y + 10, 10,
            -1.57079632679, 0.0);
    cairo_arc(cr, ctx->width - padding - 10, input_y + input_height - 10, 10,
            0.0, 1.57079632679);
    cairo_arc(cr, padding + 10, input_y + input_height - 10, 10,
            1.57079632679, 3.14159265359);
    cairo_arc(cr, padding + 10, input_y + 10, 10,
            3.14159265359, 4.71238898038);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.125, 0.137, 0.169, 1.0);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 1.5);
    cairo_set_source_rgba(cr, 0.376, 0.490, 0.984, 0.75);
    cairo_stroke(cr);

    masked = mask_password(ctx->password_len);
    pango_layout_set_font_description(layout, input_font);
    pango_layout_set_width(layout, -1);
    pango_layout_set_wrap(layout, PANGO_WRAP_CHAR);
    pango_layout_set_text(layout, masked ? masked : "", -1);

    cairo_set_source_rgb(cr, 0.980, 0.984, 0.992);
    cairo_move_to(cr, padding + 18, input_y + 15);
    pango_cairo_show_layout(cr, layout);

    if (masked) {
        PangoRectangle ink_rect;
        PangoRectangle logical_rect;

        pango_layout_get_pixel_extents(layout, &ink_rect, &logical_rect);
        mask_width = (double)logical_rect.width;
    }

    cairo_set_source_rgb(cr, 0.376, 0.490, 0.984);
    cairo_rectangle(cr, padding + 18 + mask_width + 2, input_y + 12, 2,
            input_height - 24);
    cairo_fill(cr);

    pango_font_description_free(input_font);
    pango_font_description_free(prompt_font);
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    free(masked);

    pool = wl_shm_create_pool(ctx->shm, fd, size);
    buffer = wl_shm_pool_create_buffer(pool, 0, ctx->width, ctx->height,
            stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_buffer_add_listener(buffer, &buffer_listener, ctx);
    ctx->buffer = buffer;

    wl_surface_attach(ctx->surface, buffer, 0, 0);
    wl_surface_damage_buffer(ctx->surface, 0, 0, ctx->width, ctx->height);
    wl_surface_commit(ctx->surface);

    munmap(data, (size_t)size);
}
