# Hyprscheme — Guile Scheme scripting for Hyprland, as a plugin.
#
#   make               build Hyprland (in place) as needed, then the plugin
#                      (scheme-plugin-guile.so; needs guile-3.0 dev headers)
#   make install       install plugin + .scm machinery locally
#   make install-compositor  also install the compositor as 'hyprland-scheme'
#   make clean         remove the plugin build products (not hyprland)
#
# The Chez backend is DEPRECATED: frozen in chez/ (see chez/README.md);
# this tree no longer builds it.
#
# The upstream tree, built IN PLACE:
#   HYPRLAND_SRC  Hyprland checkout (default ../Hyprland); built in place in
#                 <tree>/build. The plugin compiles against those headers
#                 and links with the compositor built from the SAME tree —
#                 plugin and compositor must always move together.
#   PREFIX        install destination (default ~/.local)

HYPRLAND_SRC ?= $(CURDIR)/../Hyprland
PREFIX ?= $(HOME)/.local
HYPR_COMMIT ?= c26dbf93

HYPRLAND_VERSION := $(shell git -C $(HYPRLAND_SRC) describe --tags --always 2>/dev/null || echo $(HYPR_COMMIT))
CXXFLAGS += -std=c++2b -g -O2 -fPIC -DHYPRLAND_VERSION='"$(HYPRLAND_VERSION)"' \
           -DSOURCE_DIR='"$(CURDIR)"'
INCLUDES = -I$(HYPRLAND_SRC) -I$(HYPRLAND_SRC)/src -I$(HYPRLAND_SRC)/protocols \
           -Isrc/config/scheme \
           `pkg-config --cflags pixman-1 libdrm pangocairo libinput libudev wayland-server xkbcommon hyprutils`
LIBS = -lpthread -lm -ldl -lrt -lcurses -llz4 -lz `pkg-config --libs lua55`

# the plugin artifact. The .scm machinery is installed next to it: the
# prelude (error plumbing, watchdog, fire trampolines, layout entry
# points) and the bootstrap (the API). The Chez-only defun machinery is
# frozen in chez/.
# the machinery as MODULES (the file paths are the module names: (hyprscheme)
# resolves to hyprscheme.scm, (hyprscheme kernel) to hyprscheme/kernel.scm)
SCM_FILES = src/config/scheme/hyprscheme.scm \
            src/config/scheme/hyprscheme/kernel.scm \
            src/config/scheme/hyprscheme/api.scm

# guile-3.0 cflags/libs: Guile.cpp, Handles.* and Bindings.hpp include
# libguile headers
INCLUDES   += `pkg-config --cflags guile-3.0`
GUILIBS     = `pkg-config --libs guile-3.0`

# one translation unit per object family; each owns its entry points and their
# registration (see src/config/scheme/SchemeInternals.hpp)
COMMON_OBJS = src/config/scheme/Host.o src/config/scheme/SchemeLayout.o \
              src/config/scheme/Layer.o src/config/scheme/Timer.o \
              src/config/scheme/Notification.o src/config/scheme/Gesture.o \
              src/config/scheme/Rule.o src/config/scheme/Bind.o \
              src/config/scheme/Config.o src/config/scheme/Workspace.o src/config/scheme/Monitor.o \
              src/config/scheme/Query.o src/config/scheme/Window.o src/config/scheme/Group.o \
              src/config/scheme/Exec.o src/config/scheme/Event.o \
              src/config/scheme/Handles.o src/plugin-main.o
HOST_GUILE  = src/config/scheme/Guile.o
OBJ         = $(COMMON_OBJS) $(HOST_GUILE)

TARGET       = scheme-plugin-guile.so

all: $(TARGET) check

# ---- Hyprland (in place) ---------------------------------------------------

$(HYPRLAND_SRC)/build/Hyprland:
	$(MAKE) hyprland

.PHONY: hyprland
hyprland:
	@if [ ! -d "$(HYPRLAND_SRC)" ]; then \
	    echo "cloning Hyprland at commit $(HYPR_COMMIT)..."; \
	    git clone https://github.com/hyprwm/Hyprland "$(HYPRLAND_SRC)"; \
	    git -C "$(HYPRLAND_SRC)" checkout -q $(HYPR_COMMIT); \
	    git -C "$(HYPRLAND_SRC)" submodule update --init; \
	fi
	cmake -B $(HYPRLAND_SRC)/build -S $(HYPRLAND_SRC) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(HYPRLAND_SRC)/build -j$$(nproc)

# ---- the plugin ------------------------------------------------------------

# the compositor binary is a prerequisite: a rebuilt Hyprland forces a
# plugin relink (it resolves Hyprland symbols at load time and must stay
# in sync with the tree it was compiled against)
$(TARGET): $(COMMON_OBJS) $(HOST_GUILE) $(HYPRLAND_SRC)/build/Hyprland
	$(CXX) -shared -fPIC -o $@ $(filter %.o,$^) $(LIBS) $(GUILIBS)

src/plugin-main.o: plugin-main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ---- install ---------------------------------------------------------------

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/lib/hyprscheme/hyprscheme
	install -m 644 $(TARGET) $(DESTDIR)$(PREFIX)/lib/hyprscheme/
	install -m 644 src/config/scheme/hyprscheme.scm $(DESTDIR)$(PREFIX)/lib/hyprscheme/
	install -m 644 src/config/scheme/hyprscheme/*.scm $(DESTDIR)$(PREFIX)/lib/hyprscheme/hyprscheme/
	@echo ""
	@echo "Plugin installed: $(DESTDIR)$(PREFIX)/lib/hyprscheme/$(TARGET)"
	@echo "Load into a compositor built from $(HYPRLAND_SRC):"
	@echo "    hyprctl plugin load $(DESTDIR)$(PREFIX)/lib/hyprscheme/$(TARGET)"
	@echo "or install a matching compositor: make install-compositor"

# installs the in-place-built compositor binary as 'hyprland-scheme' next to
# the plugin, so plugin+compositor move together
install-compositor: $(TARGET) $(HYPRLAND_SRC)/build/Hyprland
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(HYPRLAND_SRC)/build/Hyprland $(DESTDIR)$(PREFIX)/bin/hyprland-scheme
	@if [ -f "$(HYPRLAND_SRC)/systemd/hyprland-uwsm.desktop" ]; then \
	    install -d $(DESTDIR)$(PREFIX)/share/wayland-sessions; \
	    sed -e 's|Name=Hyprland (uwsm-managed)|Name=Hyprland (Scheme)|' \
	        -e 's|^Exec=.*|Exec=$(abspath $(DESTDIR)$(PREFIX))/bin/hyprland-scheme|' \
	        -e '/^TryExec=/d' \
	        $(HYPRLAND_SRC)/systemd/hyprland-uwsm.desktop \
	        > $(DESTDIR)$(PREFIX)/share/wayland-sessions/hyprland-scheme.desktop; \
	    echo "Session entry installed."; \
	fi
	@echo ""
	@echo "hyprland-scheme installed at: $(DESTDIR)$(PREFIX)/bin/hyprland-scheme"

# ---- the lints -------------------------------------------------------------
#
# Part of `make`, not an optional extra: every one of these catches a class of
# fault that reached a running compositor during development.
#   syntax-check  Guile's own reader — the paren/quote/docstring class, at the
#                 edit site, instead of a "prelude failed to load" at startup
#   module-audit  the module rule: a family imports only the kernel and core and
#                 never calls another family
#   load-check    stubs the C entry points and loads the machinery in a plain
#                 guile: catches a load-time error with no compositor
#   bind-audit    every hl--c-* the machinery calls is registered, none twice,
#                 none dead — and nothing is left UNDEFINED IN THE BUILT .so,
#                 which is how a family object missing from COMMON_OBJS shows up
#                 (it links fine and fails only when the compositor loads it)
SCM_SRC = $(wildcard src/config/scheme/*.scm src/config/scheme/hyprscheme/*.scm)

.PHONY: check
check: $(TARGET)
	@echo "== syntax-check"; guile -s tools/syntax-check.scm $(SCM_SRC)
	@echo "== module-audit"; python3 tools/module-audit.py
	@echo "== load-check";   guile --no-auto-compile -L src/config/scheme -s tools/load-check.scm >/dev/null 2>&1 \
	  && echo "ok: the machinery loads" || { echo "load-check FAILED"; exit 1; }
	@echo "== bind-audit";   python3 tools/bind-audit.py

clean:
	rm -f $(OBJ) $(TARGET)

distclean: clean
	rm -rf build

.PHONY: all hyprland install install-compositor clean distclean