# Hyprscheme — Guile Scheme scripting for Hyprland, as a plugin.
#
#   make               build Hyprland (in place) as needed, then the plugin
#                      (scheme-plugin-guile.so; needs guile-3.0 dev headers)
#   make install       install the plugin, the .scm machinery and the matched
#                      compositor (as 'Hyprscheme'), plus a session entry.
#                      A SYSTEM install (/usr/local) — needs sudo
#   make install-local config only: appends the plugin load line to THIS
#                      user's ~/.config/hypr/hyprland.lua, for a system
#                      install done with `sudo make install` above. Installs
#                      no files; run without sudo; skips if already present
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
#   PREFIX        install destination (default /usr/local)
#   DESTDIR       staging prefix for packaging (invisible at runtime: it is
#                 copied out at package time, so it must NOT be baked into
#                 either the binary or the session entry)

# INSTALL_CONFIG=1 (set only by install-local) appends the plugin load line to
# ~/.config/hypr/hyprland.lua. Off by default: the system install must never
# write into a user's config, and a packager must never write into its own.
HYPRLAND_SRC    ?= $(CURDIR)/../Hyprland
PREFIX          ?= /usr/local
INSTALL_CONFIG  ?= 0
HYPR_COMMIT ?= c26dbf93

HYPRLAND_VERSION := $(shell git -C $(HYPRLAND_SRC) describe --tags --always 2>/dev/null || echo $(HYPR_COMMIT))
CXXFLAGS += -std=c++2b -g -O2 -fPIC -DHYPRLAND_VERSION='"$(HYPRLAND_VERSION)"'
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

# ---- hyprpm ----------------------------------------------------------------
#
# hyprpm clones Hyprland at the running commit, builds it, installs the headers
# into its own cache, and points PKG_CONFIG_PATH there — so inside this target
# `pkg-config --cflags hyprland` resolves to THOSE headers, and there is no
# Hyprland source tree to point HYPRLAND_SRC at (hyprpm already built one).
#
# Deliberately a SEPARATE target, not a mode of the normal build. The normal
# build's includes come from HYPRLAND_SRC, and on a developer's machine plain
# `pkg-config hyprland` resolves to the SYSTEM install (here: /usr/include,
# 0.56.2) while the tree is something else (v0.56.0-203). Compiling the plugin
# against the wrong headers is the mismatch this repo warns about — loud on a
# renamed symbol, silent on a changed class layout — so the two include sources
# must never be mixed by accident. Only this target uses pkg-config, and only
# because hyprpm has guaranteed which headers it will find.
#
# Output the SAME artifact name: hyprpm's manifest declares it, and it is what
# the compositor dlopens.
# the sources, spelled out. Deriving them from $(OBJ) cannot work: plugin-main.o
# sits at the repo ROOT while every other object is under src/, so a blanket
# .o→.cpp mapping produces a path that does not exist.
HYPRPM_SRCS := plugin-main.cpp $(filter-out src/plugin-main.cpp,$(COMMON_OBJS:.o=.cpp)) $(HOST_GUILE:.o=.cpp)
HYPRPM_OBJS := $(HYPRPM_SRCS:.cpp=.hyprpm.o)

.PHONY: hyprpm-plugin
hyprpm-plugin: hyprpm-clean
	@pc="$$(pkg-config --cflags hyprland 2>/dev/null)"; \
	if [ -z "$$pc" ]; then \
	    echo "error: pkg-config cannot find hyprland."; \
	    echo "       this target is for a hyprpm build, where hyprpm provides"; \
	    echo "       hyprland.pc via PKG_CONFIG_PATH. For a tree build use 'make'."; \
	    exit 1; \
	fi; \
	echo "building against: $$(pkg-config --modversion hyprland) ($$(pkg-config --variable=prefix hyprland))"; \
	for src in $(HYPRPM_SRCS); do \
	    $(CXX) $(CXXFLAGS) $$pc -Isrc/config/scheme \
	        `pkg-config --cflags pixman-1 libdrm pangocairo libinput libudev wayland-server xkbcommon hyprutils` \
	        `pkg-config --cflags guile-3.0` -c "$$src" -o "$${src%.cpp}.hyprpm.o" || exit 1; \
	done; \
	$(CXX) -shared -fPIC -o $(TARGET) $(HYPRPM_OBJS) $(LIBS) $(GUILIBS) || exit 1; \
	echo "built $(TARGET) for hyprpm"

# the hyprpm build's objects are removed afterwards: they are built from a
# DIFFERENT header set than the tree build's, so leaving them would let a later
# `make` mistake them for the tree's, or vice versa
.PHONY: hyprpm-clean
hyprpm-clean:
	@rm -f $(HYPRPM_OBJS)
	@rm -f $(TARGET).hyprpm

# ---- install ---------------------------------------------------------------

install: $(TARGET)
	@if [ -z "$(DESTDIR)" ]; then \
	    d="$(PREFIX)"; \
	    while [ ! -e "$$d" ] && [ "$$d" != "/" ]; do d=$$(dirname "$$d"); done; \
	    if [ ! -w "$$d" ]; then \
	        echo "error: $$d is not writable."; \
	        echo "       run:  sudo make install   # system install (the default)"; \
	        echo "       or:   make install PREFIX=\$$HOME/.local  # files only, per-user"; \
	        exit 1; \
	    fi; \
	fi
	install -d $(DESTDIR)$(PREFIX)/lib/hyprscheme/hyprscheme
	install -m 644 $(TARGET) $(DESTDIR)$(PREFIX)/lib/hyprscheme/
	install -m 644 src/config/scheme/hyprscheme.scm $(DESTDIR)$(PREFIX)/lib/hyprscheme/
	install -m 644 src/config/scheme/hyprscheme/*.scm $(DESTDIR)$(PREFIX)/lib/hyprscheme/hyprscheme/
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(HYPRLAND_SRC)/build/Hyprland $(DESTDIR)$(PREFIX)/bin/Hyprscheme
	install -m 755 $(HYPRLAND_SRC)/build/start/start-hyprland $(DESTDIR)$(PREFIX)/bin/start-hyprscheme
	@if [ -f "$(HYPRLAND_SRC)/systemd/hyprland-uwsm.desktop" ]; then \
	    install -d $(DESTDIR)$(PREFIX)/share/wayland-sessions; \
	    sed -e 's|Name=Hyprland (uwsm-managed)|Name=Hyprscheme|' \
	        -e 's|^Exec=.*|Exec=uwsm start -g -1 -e -D Hyprscheme -- start-hyprscheme --path $(PREFIX)/bin/Hyprscheme|' \
	        -e 's|^DesktopNames=.*|DesktopNames=Hyprscheme|' \
	        $(HYPRLAND_SRC)/systemd/hyprland-uwsm.desktop \
	        > $(DESTDIR)$(PREFIX)/share/wayland-sessions/Hyprscheme.desktop; \
	    echo "Session entry installed."; \
	fi
	@if [ "$(INSTALL_CONFIG)" = "1" ]; then \
	    echo ""; \
	    echo "load the plugin in your config with:"; \
	    echo "    hl.plugin.load(\"$(PREFIX)/lib/hyprscheme/$(TARGET)\")"; \
	fi
	@echo ""
	@echo "Hyprscheme installed at: $(DESTDIR)$(PREFIX)/bin/Hyprscheme"
	@echo "Session entry: $(DESTDIR)$(PREFIX)/share/wayland-sessions/Hyprscheme.desktop"
	@echo "Log out and pick 'Hyprscheme' at the login screen (or: uwsm start -e Hyprscheme)"

# ---- install-local ---------------------------------------------------------
#
# CONFIG ONLY — installs no files. The program is a system install (`sudo make
# install`); what is per-user is the one line that loads it, because
# ~/.config/hypr/hyprland.lua belongs to this user and nothing else can write
# it for them.
#
# It cannot be folded into `install`: that runs under sudo, where $HOME is
# root's, so it would edit /root/.config/hypr/hyprland.lua and never find the
# real one. Some guess at the invoking user would always be a guess. Run as
# the user instead, $(HOME) IS the user, and the file that gets edited is the
# right one by construction.
#
# NOTE: it must NOT build, and specifically must not depend on $(TARGET).
# Depending on it would drag in $(HYPRLAND_SRC)/build/Hyprland, and a plain
# user has no business reconfiguring the root-owned Hyprland tree.
.PHONY: install-local
install-local:
	@cfg="$(HOME)/.config/hypr/hyprland.lua"; \
	lib="$(PREFIX)/lib/hyprscheme/$(TARGET)"; \
	block="$$(printf -- '-- Hyprscheme: load the Scheme plugin (added by make install-local)\nif (os.getenv("DESKTOP_SESSION") or "") == "Hyprscheme" then\n  hl.plugin.load("%s")\nend' "$$lib")"; \
	if [ ! -f "$$cfg" ]; then \
	    echo "no $$cfg — nothing to edit. Add this to your Hyprland config:"; \
	    printf '%s\n' "$$block"; \
	elif grep -qF "$$lib" "$$cfg"; then \
	    echo "$$cfg already loads $$lib — left alone."; \
	elif existing=$$(grep -o 'hl\.plugin\.load([^)]*)' "$$cfg" | head -1) && [ -n "$$existing" ]; then \
	    echo "$$cfg already loads a plugin:"; \
	    echo "    $$existing"; \
	    echo "NOT appending a second one. Two hl.plugin.load calls for the same"; \
	    echo "plugin (different paths — e.g. ~/.local vs /usr/local) both load, and"; \
	    echo "the duplicate IPC registration breaks 'hyprctl scheme' until the"; \
	    echo "compositor restarts. Replace that line by hand if the path is wrong."; \
	    exit 1; \
	else \
	    printf '\n%s\n' "$$block" >> "$$cfg"; \
	    echo "added the plugin load to $$cfg"; \
	    echo "it is guarded by DESKTOP_SESSION, so it is inert in any other session"; \
	fi

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
	@echo "== doc-audit";    python3 tools/doc-audit.py
	@echo "== doc-exercise"; guile --no-auto-compile -s tools/doc-exercise.scm

clean:
	rm -f $(OBJ) $(TARGET) $(HYPRPM_OBJS)

distclean: clean
	rm -rf build

.PHONY: all hyprland install install-local check clean distclean hyprpm-plugin hyprpm-clean
