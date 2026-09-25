# Hyprscheme — Chez Scheme scripting for Hyprland, as a plugin.
#
#   make               build Chez (PIC, in place) and Hyprland (in place) as
#                      needed, then the plugin — everything where it lives
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

# the Scheme backend: SchemeHostChez.cpp (default) or SchemeHostGuile.cpp
BACKEND ?= chez
ifeq ($(BACKEND),guile)
SCHEME_HOST = src/config/scheme/SchemeHostGuile.cpp
INCLUDES   += `pkg-config --cflags guile-3.0`
LIBS       += `pkg-config --libs guile-3.0`
SCM_FILES  += src/config/scheme/hyprscheme-compat-guile.scm
else
SCHEME_HOST = src/config/scheme/SchemeHostChez.cpp
endif

SRC = $(SCHEME_HOST) \
      src/config/scheme/SchemeManager.cpp src/config/scheme/SchemeLayout.cpp
OBJ = $(SRC:.cpp=.o) src/plugin-main.o
TARGET = scheme-plugin.so

all: $(TARGET)

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
ifeq ($(BACKEND),guile)
# the Guile backend: no Chez kernel, libguile from pkg-config
$(TARGET): $(OBJ) $(HYPRLAND_SRC)/build/Hyprland
	$(CXX) -shared -fPIC -o $@ $(filter %.o,$^) $(LIBS)
else
$(TARGET): $(OBJ) $(CHEZ_KERNEL) $(HYPRLAND_SRC)/build/Hyprland | $(CHEZ_BOOT)/petite.boot
	$(CXX) -shared -fPIC -o $@ $(filter %.o,$^) $(CHEZ_KERNEL) \
	    $(CHEZ_WORK)/lz4/lib/liblz4.a $(LIBS)
endif

src/plugin-main.o: plugin-main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ---- install ---------------------------------------------------------------

SCM_FILES = src/config/scheme/hyprscheme-prelude.scm \
            src/config/scheme/hyprscheme-bootstrap.scm \
            src/config/scheme/hyprscheme-defun.scm

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/lib/hyprscheme
	install -m 644 $(TARGET) $(DESTDIR)$(PREFIX)/lib/hyprscheme/
	install -m 644 $(SCM_FILES) $(DESTDIR)$(PREFIX)/lib/hyprscheme/
ifeq ($(BACKEND),chez)
	install -m 644 $(CHEZ_BOOT)/petite.boot $(CHEZ_BOOT)/scheme.boot $(DESTDIR)$(PREFIX)/lib/hyprscheme/
endif
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
	rm -f $(OBJ) $(TARGET)

distclean: clean
	rm -rf build

.PHONY: all chez hyprland install install-compositor clean distclean
