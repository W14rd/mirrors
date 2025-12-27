# mirrors
windows. X11

## Dependencies
Look up exact names for your package manager, but usually they end with prefixes -dev (except for cmake)
- cmake
- Xvfb
- libpthreadpool
- libX11
- libXext
- libXtst
- libXamage


## Build

```bash
git clone https://github.com/W14rd/mirrors.git
cd mirrors/
cmake -B build
cd build/
make
```

## Usage

Open a Terminal session (xfce4-terminal, gnome-terminal, xterm, kitty etc), fullscreen it (and F11). Afterwards, adjust font scale. Smaller font size = higher resolution.
Run:
```bash
./build/mirrors --help
./build/mirrors /bin/application
```

Wait some time, during which a WM session is launched, and the provided app opens. All apps opened by the provided app are also rendered in this WM. To adjust zoom, use Ctrl + mouse scroll. To change current view area (panning), Ctrl + drag. Afterwards, ^\\ to exit.
To adjust time it waits for the app (so it detects heavier ones), set -s <\int> flag. That should fix "window not found".
If you have performance issues, the -r flag probably won't help. Use --nomouse, --ansi (or --grey as last resort) and decrease font size.
