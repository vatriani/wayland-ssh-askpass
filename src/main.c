#define _GNU_SOURCE

#include "wayland-core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *build_prompt(int argc, char **argv) {
    const char *fallback = "Enter SSH credential:";
    size_t total_len = 0;
    char *prompt;
    int index;

    if (argc <= 1) return strdup(fallback);

    for (index = 1; index < argc; ++index) {
        total_len += strlen(argv[index]) + 1;
    }

    prompt = calloc(total_len + 1, sizeof(char));
    if (!prompt) return NULL;

    for (index = 1; index < argc; ++index) {
        strcat(prompt, argv[index]);
        if (index + 1 < argc) strcat(prompt, " ");
    }

    return prompt;
}

int main(int argc, char **argv) {
    struct app_context ctx;
    char *prompt = build_prompt(argc, argv);
    int exit_code = 1;

    if (!prompt) return 1;
    if (app_context_init(&ctx, prompt) != 0) {
        app_context_cleanup(&ctx);
        free(prompt);
        return 1;
    }

    while (ctx.running) {
        int timeout_ms = app_remaining_timeout_ms(&ctx);

        if (timeout_ms <= 0) {
            ctx.timed_out = 1;
            ctx.running = 0;
            break;
        }

        if (app_dispatch_events(&ctx, timeout_ms) < 0) {
            break;
        }
    }

    if (ctx.submitted && ctx.password) {
        if (fputs(ctx.password, stdout) >= 0 && fputc('\n', stdout) != EOF) {
            fflush(stdout);
            exit_code = 0;
        }
    }

    app_context_cleanup(&ctx);
    free(prompt);
    return exit_code;
}
