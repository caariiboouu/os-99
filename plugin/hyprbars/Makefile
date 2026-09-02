# Else exist specifically for clang
ifeq ($(CXX),g++)
    EXTRA_FLAGS = --no-gnu-unique
else
    EXTRA_FLAGS =
endif

CXXFLAGS ?= -O2
CXXFLAGS += -shared -fPIC -std=c++2b -Wno-c++11-narrowing
INCLUDES = `pkg-config --cflags pixman-1 libdrm hyprland libinput libudev wayland-server xkbcommon`
LIBS =

# GLOB, do not list. Upstream's Makefile named three files explicitly while its
# CMakeLists used file(GLOB_RECURSE "*.cpp"), so the two build systems disagreed
# the moment this fork added ninePatch.cpp. Building with cmake worked and
# building with make produced a .so that dlopen rejected:
#
#   Hyprland: symbol lookup error: libhyprbars-os99-*.so:
#     undefined symbol: _Z12parseBorders...
#
# An undefined symbol aborts the compositor at load with no core and no usable
# backtrace, so it does not look like a build problem at all. A wildcard cannot
# drift out of step with the directory.
SRC = $(wildcard *.cpp)
TARGET = hyprbars.so

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(INCLUDES) $^ $> -o $@ $(LIBS)

clean:
	rm -f ./$(TARGET)

meson-build:
	mkdir -p build
	cd build && meson .. && ninja

.PHONY: all meson-build clean
