
all:
	$(CXX) -shared -fPIC --no-gnu-unique \
	-I./include \
	src/*.cpp \
	-o mylar-desktop.so \
	-g -O0 -std=c++2b \
	`pkg-config --cflags librsvg-2.0 pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon pangocairo cairo`

release:
	$(CXX) -shared -fPIC --no-gnu-unique \
	-I./include \
	src/*.cpp \
	-o mylar-desktop.so \
	-g -O3 -std=c++2b \
	`pkg-config --cflags librsvg-2.0 pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon pangocairo cairo`

clean:
	rm ./mylar-desktop.so

