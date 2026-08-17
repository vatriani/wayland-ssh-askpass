#ifndef INPUT_H
#define INPUT_H

#include "wayland-core.h"

extern const struct wl_seat_listener seat_listener;

int input_init(struct app_context *ctx);
void input_cleanup(struct app_context *ctx);

#endif
