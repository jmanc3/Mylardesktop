# Mylar Desktop

Mylar is a smooth and beautiful wayland desktop, written on Hyprland

## Features

- Physics based spring animations everywhere, like we're living in the 21st century
- Integrated dock
  - wifi control with NetworkManager, or wpa_supplicant
  - audio control with pulseaudio, or alsa
  - bluetooth when bluetoothd running
  - minimize animations
  - brightness control with brightnessctl
  - sleep cpu windows and wake them up for games
  - pin windows to above or below
- Rofi like quick launcher, for applications, or to send text to other programs and accelarete your workflow
- Alt kakoune menu to accelarete your workflow
- Desktop folders
  - Selection that is like soap
- Nightlight
- Mica effect
- Animated snap preview
  - Snap groups
  - Snap groups in alt tab menu 
- Wallpaper engine
- Screenshot tool
  - Edit with, features that sorts most recently used
- Seamless transition into tiling mode (powered by Hyprland)
- Alt tab menu
- Hints for shortcuts
- Screen lock
- Overview mode of windows
- Top workspace preview mover
- Pixel perfect zoom

## Installation

You can install Mylar wherever there is a stable Hyprland package for your system.

```bash
yay -S mylardesktop
```

Or, if you already have Hyprand, you can install it as a [plugin]() instead.

In your login manager you should now see an option `Mylardesktop via USWM`. Select that and login.

## Manually building

```
git clone https://github.com/jmanc3/Mylardesktop
cd Mylardesktop
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ../
make -j 12
sudo make install
```

## Information for packagers

https://github.com/*

