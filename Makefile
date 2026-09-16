# Hyprscheme — Chez Scheme scripting for Hyprland, as a plugin.
#
#   make build-chez    build a PIC Chez kernel locally (recommended)
#   make               build the plugin .so
#   make install       install plugin + boot files locally
#   make clean
#
# Selectable inputs:
#   CHEZ_OUT       where a PIC Chez build already lives (skips build-chez)
#   HYPRLAND_SRC   Hyprland source tree to compile the plugin against
#                  (must match the running compositor's version)
#   PREFIX         install destination (default ~/.local)

HYPRLAND_SRC ?= $(CURDIR)/build/hyprland-src
PREFIX ?= $(HOME)/.local
CHEZ_OUT ?= $(CURDIR)/build/chez

CXXFLAGS += -std=c++2b -g -O2 -fPIC -fvisibility=hidden
INCLUDES = -I$(HYPRLAND_SRC) -I$(HYPRLAND_SRC)/src -I$(HYPRLAND_SRC)/protocols \
           -I$(CHEZ_OUT) -Isrc/config/scheme \
           `pkg-config --cflags pixman-1 libdrm pangocairo libinput libudev wayland-server xkbcommon hyprutils`
LIBS = -lpthread -lm -ldl -lrt -lcurses -llz4 -lz

SRC = src/config/scheme/SchemeManager.cpp src/config/scheme/SchemeLayout.cpp
OBJ = $(SRC:.cpp=.o) src/plugin-main.o
TARGET = scheme-plugin.so

all: $(TARGET)

# auto-build PIC Chez if not present yet
$(CHEZ_OUT)/petite.boot:
	./build-chez.sh

$(OBJ) $(TARGET): | $(CHEZ_OUT)/petite.boot
build-chez: $(CHEZ_OUT)/petite.boot

$(TARGET): $(OBJ)
	$(CXX) -shared -fPIC -o $@ $^ $(CHEZ_OUT)/libchez-pic.a $(LIBS)

src/plugin-main.o: plugin-main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/lib/hyprscheme
	install -m 644 $(TARGET) $(DESTDIR)$(PREFIX)/lib/hyprscheme/
	install -m 644 $(CHEZ_OUT)/petite.boot $(CHEZ_OUT)/scheme.boot $(DESTDIR)$(PREFIX)/lib/hyprscheme/
	@echo ""
	@echo "Plugin installed:"
	@echo "    $(DESTDIR)$(PREFIX)/lib/hyprscheme/scheme-plugin.so"
	@echo "Load it from a compositor built from the same Hyprland tree:"
	@echo "    hyprctl plugin load $(DESTDIR)$(PREFIX)/lib/hyprscheme/scheme-plugin.so"
	@echo "or build+install a matching compositor: make install-compositor"

# builds the pinned Hyprland tree (HYPRLAND_SRC) and installs its binary as
# 'hyprland-scheme' next to the plugin, so plugin+compositor move together.
# First run takes a while (full compositor build).
HYPR_COMMIT ?= c26dbf93

install-compositor: $(TARGET)
	@test -n "$(HYPRLAND_SRC)" || { echo "HYPRLAND_SRC not set"; exit 1; }
	@if [ ! -d "$(HYPRLAND_SRC)" ]; then \
	    echo "cloning Hyprland at commit $(HYPR_COMMIT)..."; \
	    git clone https://github.com/hyprwm/Hyprland "$(HYPRLAND_SRC)"; \
	    git -C "$(HYPRLAND_SRC)" checkout -q $(HYPR_COMMIT); \
	    git -C "$(HYPRLAND_SRC)" submodule update --init; \
	fi
	if [ ! -x "$(HYPRLAND_SRC)/build/Hyprland" ]; then \
	    echo "building the compositor in $(HYPRLAND_SRC)/build (this takes a while)..."; \
	    PKG_CONFIG_PATH="$$PKG_CONFIG_PATH" cmake -B "$(HYPRLAND_SRC)/build" -S "$(HYPRLAND_SRC)" -DCMAKE_BUILD_TYPE=Release; \
	    cmake --build "$(HYPRLAND_SRC)/build" -j$$(nproc); \
	fi
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(HYPRLAND_SRC)/build/Hyprland $(DESTDIR)$(PREFIX)/bin/hyprland-scheme
	@if [ -f "$(HYPRLAND_SRC)/systemd/hyprland-uwsm.desktop" ]; then \
	    install -d $(DESTDIR)$(PREFIX)/share/wayland-sessions; \
	    sed -e 's|Name=Hyprland (uwsm-managed)|Name=Hyprland (Scheme)|' \
	        -e 's|^Exec=.*|Exec=$(abspath $(DESTDIR)$(PREFIX))/bin/hyprland-scheme|' \
	        -e '/^TryExec=/d' \
	        $(HYPRLAND_SRC)/systemd/hyprland-uwsm.desktop \
	        > $(DESTDIR)$(PREFIX)/share/wayland-sessions/hyprland-scheme.desktop; \
	    echo "Session entry installed (display managers will offer Hyprland (Scheme))."; \
	fi
	@echo ""
	@echo "hyprland-scheme installed at: $(DESTDIR)$(PREFIX)/bin/hyprland-scheme"
	@echo "Its config loads the plugin from: $(DESTDIR)$(PREFIX)/lib/hyprscheme/scheme-plugin.so"

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all build-chez install clean
