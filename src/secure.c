#include "secure.h"

void secure_clear_memory(void *ptr, size_t len) {
    volatile unsigned char *bytes = ptr;

    if (!bytes) return;

    while (len-- > 0) {
        *bytes++ = 0;
    }
}
