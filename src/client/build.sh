cc test.cpp xdg-shell-protocol.c \
   -o prog \
   $(pkg-config --cflags --libs wayland-client xkbcommon)

