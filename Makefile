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

HYPRLAND_SRC ?= /tmp/hl-clean
PREFIX ?= $(HOME)/.local
CHEZ_OUT ?= $(CURDIR)/build/chez

CXXFLAGS += -std=c++2b -g -O2
INCLUDES = -I$(HYPRLAND_SRC) -I$(HYPRLAND_SRC)/src -I$(HYPRLAND_SRC)/protocols \
           -I$(CHEZ_OUT) -Isrc/config/scheme \
           `pkg-config --cflags pixman-1 libdrm pangocairo libinput libudev wayland-server xkbcommon hyprutils`
LIBS = -lpthread -lm -ldl -lrt -lcurses -llz4 -lz

SRC = src/config/scheme/SchemeManager.cpp src/config/scheme/SchemeLayout.cpp
OBJ = $(SRC:.cpp=.o) src/plugin-main.o
TARGET = scheme-plugin.so

all: $(TARGET)

build-chez:
	./build-chez.sh

$(TARGET): $(OBJ)
	$(CXX) -shared -fPIC -o $@ $^ /home/chris/GITE/ChezScheme/ta6le/lz4/lib/lz4.o $(LIBS)

src/plugin-main.o: plugin-main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/lib/hyprscheme
	install -m 644 $(TARGET) $(DESTDIR)$(PREFIX)/lib/hyprscheme/
	install -m 644 $(CHEZ_OUT)/petite.boot $(CHEZ_OUT)/scheme.boot $(DESTDIR)$(PREFIX)/lib/hyprscheme/
	@echo ""
	@echo "Installed. Add to your Hyprland config (before other plugins load):"
	@echo "    plugin = $(DESTDIR)$(PREFIX)/lib/hyprscheme/scheme-plugin.so"
	@echo "then reload Hyprland."

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all build-chez install clean
