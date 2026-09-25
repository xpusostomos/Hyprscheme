# Hyprscheme — Chez Scheme scripting for Hyprland, as a plugin.
#
#   make               build Chez (PIC, in place) and Hyprland (in place) as
#                      needed, then the plugin (scheme-plugin.so, Chez)
#   make guile         also build the Guile artifact (scheme-plugin-guile.so;
#                      needs guile-3.0 dev headers)
#   make install       install plugin + boot files locally
#   make install-compositor  also install the compositor as 'hyprland-scheme'
#   make clean         remove the plugin build products (not chez/hyprland)
#
# The two upstream trees, built IN PLACE:
#   CHEZ_DIR      Chez Scheme checkout (default ../ChezScheme). Built with
#                 CFLAGS=-fPIC (or the upstream --pic flag when present);
#                 the plugin consumes its workarea objects + boot files
#                 directly — no copies, no staging.
#   HYPRLAND_SRC  Hyprland checkout (default ../Hyprland); built in place in
#                 <tree>/build. The plugin compiles against those headers
#                 and links with the compositor built from the SAME tree —
#                 plugin and compositor must always move together.
#   PREFIX        install destination (default ~/.local)

CHEZ_DIR ?= $(CURDIR)/../ChezScheme
HYPRLAND_SRC ?= $(CURDIR)/../Hyprland
PREFIX ?= $(HOME)/.local
HYPR_COMMIT ?= c26dbf93

# Chez workarea + boot dir (in the Chez tree, where `make` leaves them)
CHEZ_WORK ?= $(CHEZ_DIR)/ta6le
CHEZ_BOOT ?= $(CHEZ_WORK)/boot/ta6le
CHEZ_KERNEL = $(CHEZ_BOOT)/libkernel.a

HYPRLAND_VERSION := $(shell git -C $(HYPRLAND_SRC) describe --tags --always 2>/dev/null || echo $(HYPR_COMMIT))
CXXFLAGS += -std=c++2b -g -O2 -fPIC -DHYPRLAND_VERSION='"$(HYPRLAND_VERSION)"' \
           -DSOURCE_DIR='"$(CURDIR)"'
INCLUDES = -I$(HYPRLAND_SRC) -I$(HYPRLAND_SRC)/src -I$(HYPRLAND_SRC)/protocols \
           -I$(CHEZ_BOOT) -Isrc/config/scheme \
           `pkg-config --cflags pixman-1 libdrm pangocairo libinput libudev wayland-server xkbcommon hyprutils`
LIBS = -lpthread -lm -ldl -lrt -lcurses -llz4 -lz `pkg-config --libs lua55`

# the plugin artifacts: scheme-plugin.so (Chez, the default name) and
# scheme-plugin-guile.so. Both can be installed side by side — the .scm
# machinery is SHARED (prelude/bootstrap/defun run on both backends;
# hyprscheme-compat-guile.scm is only ever loaded by the Guile host) —
# and the compositor loads whichever .so the config points at.
SCM_FILES = src/config/scheme/hyprscheme-prelude.scm \
            src/config/scheme/hyprscheme-bootstrap.scm \
            src/config/scheme/hyprscheme-defun.scm \
            src/config/scheme/hyprscheme-compat-guile.scm

# guile-3.0 cflags are needed to compile SchemeHostGuile.cpp; harmless for
# the rest (only that file includes libguile headers). Its LIBS stay out of
# the Chez artifact's link line.
INCLUDES   += `pkg-config --cflags guile-3.0`
GUILIBS     = `pkg-config --libs guile-3.0`

COMMON_OBJS = src/config/scheme/SchemeManager.o src/config/scheme/SchemeLayout.o \
              src/plugin-main.o
HOST_CHEZ   = src/config/scheme/SchemeHostChez.o
HOST_GUILE  = src/config/scheme/SchemeHostGuile.o
OBJ         = $(COMMON_OBJS) $(HOST_CHEZ) $(HOST_GUILE)

TARGET       = scheme-plugin.so
TARGET_GUILE = scheme-plugin-guile.so

all: $(TARGET)

# the Guile artifact (needs guile-3.0 dev headers)
.PHONY: guile
guile: $(TARGET_GUILE)

# ---- Chez (in place) -------------------------------------------------------
# The workarea's libkernel.a + boot files are the plugin's inputs. chez's own
# make handles incremental rebuilds; we just depend on its outputs.

$(CHEZ_KERNEL):
	$(MAKE) -C $(CHEZ_DIR) kernel

$(CHEZ_BOOT)/petite.boot $(CHEZ_BOOT)/scheme.boot: $(CHEZ_KERNEL)
	$(MAKE) -C $(CHEZ_DIR)

.PHONY: chez
chez:
	$(MAKE) -C $(CHEZ_DIR)

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
# the compositor binary is a prerequisite of both artifacts: a rebuilt
# Hyprland forces a relink (the plugins resolve Hyprland symbols at load time)
$(TARGET): $(COMMON_OBJS) $(HOST_CHEZ) $(CHEZ_KERNEL) $(HYPRLAND_SRC)/build/Hyprland | $(CHEZ_BOOT)/petite.boot
	$(CXX) -shared -fPIC -o $@ $(filter %.o,$^) $(CHEZ_KERNEL) \
	    $(CHEZ_WORK)/lz4/lib/liblz4.a $(LIBS)

# the Guile artifact: no Chez kernel, libguile from pkg-config
$(TARGET_GUILE): $(COMMON_OBJS) $(HOST_GUILE) $(HYPRLAND_SRC)/build/Hyprland
	$(CXX) -shared -fPIC -o $@ $(filter %.o,$^) $(LIBS) $(GUILIBS)

src/plugin-main.o: plugin-main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ---- install ---------------------------------------------------------------

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/lib/hyprscheme
	install -m 644 $(TARGET) $(DESTDIR)$(PREFIX)/lib/hyprscheme/
	install -m 644 $(SCM_FILES) $(DESTDIR)$(PREFIX)/lib/hyprscheme/
	install -m 644 $(CHEZ_BOOT)/petite.boot $(CHEZ_BOOT)/scheme.boot $(DESTDIR)$(PREFIX)/lib/hyprscheme/
	@if [ -f $(TARGET_GUILE) ]; then \
	    install -m 644 $(TARGET_GUILE) $(DESTDIR)$(PREFIX)/lib/hyprscheme/; \
	    echo "Guile artifact installed: $(TARGET_GUILE)"; \
	fi
	@echo ""
	@echo "Plugin installed: $(DESTDIR)$(PREFIX)/lib/hyprscheme/scheme-plugin.so"
	@echo "Load into a compositor built from $(HYPRLAND_SRC):"
	@echo "    hyprctl plugin load $(DESTDIR)$(PREFIX)/lib/hyprscheme/scheme-plugin.so"
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

clean:
	rm -f $(OBJ) $(TARGET) $(TARGET_GUILE)

distclean: clean
	rm -rf build

.PHONY: all chez hyprland guile install install-compositor clean distclean
