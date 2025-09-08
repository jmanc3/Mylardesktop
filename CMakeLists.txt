cmake_minimum_required(VERSION 3.27)

project(Mylardesktop
    DESCRIPTION "Mylar desktop"
    VERSION 0.1
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE HEAD "src/*.h")
file(GLOB_RECURSE SRC "src/*.cpp")

add_library(Mylardesktop SHARED ${HEAD} ${SRC})

add_definitions(-DQT_NO_VERSION_TAGGING)

#src/first.cpp \
#src/hyprland_api_isolation/stable_hypr_api.cpp \
#src/code/our_state.cpp \
#-o mylar-desktop.so \
#-g -O0 -std=c++2b \
#`pkg-config --cflags
#pixman-1 libdrm hyprland pangocairo
#libinput libudev wayland-server xkbcommon pangocairo cairo`

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
	librsvg-2.0
    pixman-1
    libdrm
    hyprland
    pangocairo
    libinput
    libudev
    wayland-server
    xkbcommon
    cairo
    Qt5Widgets
)
target_link_libraries(Mylardesktop PRIVATE rt PkgConfig::deps)
target_include_directories(Mylardesktop PUBLIC ${CMAKE_SOURCE_DIR}/include)

install(TARGETS Mylardesktop)


