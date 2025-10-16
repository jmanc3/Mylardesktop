PLUGIN_NAME = mylar-desktop
MAKEFLAGS += -j16

# Find all .cpp source files recursively under src/
SOURCE_FILES := $(wildcard ./src/*.cpp ./src/*/*.cpp)
OBJECT_FILES := $(patsubst ./src/%.cpp, out/%.o, $(SOURCE_FILES))

# Common include and pkg-config flags
PKG_FLAGS := $(shell pkg-config --cflags librsvg-2.0 pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon pangocairo cairo)
INCLUDE_FLAGS := -I./include

# Compiler flags
COMMON_FLAGS := --no-gnu-unique -fPIC -std=c++2b $(INCLUDE_FLAGS) $(PKG_FLAGS)
#COMMON_FLAGS := -Wall --no-gnu-unique -fPIC -std=c++2b $(INCLUDE_FLAGS) $(PKG_FLAGS)

# Build types
DEBUG_FLAGS := -g -O0
RELEASE_FLAGS := -g -O3

OUTPUT = out/$(PLUGIN_NAME).so

.PHONY: all debug release clean load unload

# Default target (debug)
all: debug

# Debug build
debug: CXX_FLAGS := $(COMMON_FLAGS) $(DEBUG_FLAGS)
debug: $(OUTPUT)

# Release build
release: CXX_FLAGS := $(COMMON_FLAGS) $(RELEASE_FLAGS)
release: $(OUTPUT)

# Link step
$(OUTPUT): $(OBJECT_FILES)
	$(CXX) -shared $^ -o $@

# Compile step
out/%.o: ./src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXX_FLAGS) -c $< -o $@

clean:
	$(RM) $(OUTPUT) $(OBJECT_FILES)

load: all unload
	hyprctl plugin load ${PWD}/$(OUTPUT)

unload:
	hyprctl plugin unload ${PWD}/$(OUTPUT)

