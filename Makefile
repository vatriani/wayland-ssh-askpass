CC = gcc
CFLAGS = -O2 -fdata-sections -ffunction-sections -pedantic-errors -std=gnu11 -march=native -mtune=native -Wall -Wextra -fstack-protector-strong -fPIE -flto $(shell pkg-config --cflags wayland-client cairo pango pangocairo xkbcommon)
LIBS = $(shell pkg-config --libs wayland-client cairo pango pangocairo xkbcommon) -flto -Wl,--gc-sections,-z,relro,-z,now


ifneq ($(shell pkg-config --exists wlr-protocols && echo yes),yes)
    PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
    LAYER_SHELL_XML = $(PROTOCOLS_DIR)/ext/wlr-layer-shell-v1.xml
else
    PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wlr-protocols)
    LAYER_SHELL_XML = $(PROTOCOLS_DIR)/unstable/wlr-layer-shell-unstable-v1.xml
endif

ifeq ($(wildcard $(LAYER_SHELL_XML)),)
    LAYER_SHELL_XML = protocols/wlr-layer-shell-unstable-v1.xml
endif

WAYLAND_PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
XDG_SHELL_XML = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml

TARGET = wayland-ssh-askpass

SRC = src/main.c src/wayland-core.c src/input.c src/render.c src/secure.c src/wlr-layer-shell-unstable-v1.c src/xdg-shell.c
GEN_FILES = src/wlr-layer-shell-unstable-v1-client-protocol.h src/wlr-layer-shell-unstable-v1.c src/xdg-shell-client-protocol.h src/xdg-shell.c
OBJS = $(SRC:.c=.o)

all: $(GEN_FILES) $(TARGET)

$(GEN_FILES):
	@mkdir -p src
	wayland-scanner client-header $(LAYER_SHELL_XML) src/wlr-layer-shell-unstable-v1-client-protocol.h
	wayland-scanner private-code $(LAYER_SHELL_XML) src/wlr-layer-shell-unstable-v1.c
	wayland-scanner client-header $(XDG_SHELL_XML) src/xdg-shell-client-protocol.h
	wayland-scanner private-code $(XDG_SHELL_XML) src/xdg-shell.c

src/%.o: src/%.c $(GEN_FILES)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET) src/wlr-layer* src/xdg-shell* src/*.o

.PHONY: all clean

install: $(TARGET)
	cp $(TARGET) /usr/bin/$(TARGET)
	chmod go+rx /usr/bin/$(TARGET)

uninstall:
	rm /usr/bin/$(TARGET)
