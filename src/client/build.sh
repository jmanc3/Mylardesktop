wayland-scanner client-header wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1-client-protocol.h
wayland-scanner private-code wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1-protocol.c

#hyprwayland-scanner client ./protocols/wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1

# Compile C files with gcc
gcc -c xdg-shell-protocol.c wlr-layer-shell-unstable-v1-protocol.c

# Compile C++ file with g++
g++ -c test.cpp

# Link everything with g++
g++ test.o xdg-shell-protocol.o wlr-layer-shell-unstable-v1-protocol.o \
    -o prog \
    $(pkg-config --cflags --libs wayland-client xkbcommon)


#g++ test.cpp xdg-shell-protocol.c wlr-layer-shell-unstable-v1-protocol.c \
   #-o prog \
   #$(pkg-config --cflags --libs wayland-client xkbcommon) \
   #-e open_dock

