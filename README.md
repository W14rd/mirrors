# mirrors
windows
## Build

```bash
git clone https://github.com/W14rd/mirrors.git
cd mirrors
cmake -B build
make -j$(nproc) # or just make
```

## Usage

Open a Terminal session (xfce4-terminal, gnome-terminal, xterm, kitty etc), fullscreen it (and F11). Afterwards, adjust font scale. Lesser font size = higher resolution.
Run:
```bash
./mirrors.sh --help # you might want to read flags
./mirrors.sh /bin/application
```

Or directly:
```bash
./build/mirrors :99 <window_id>
```

Here is what happens then:
- A black screen might pop up, wait ~15 seconds
- A separate DBus and WM session is launched, the app opens in a window 
- All apps opened by this initial app are also rendered in this Terminal
- To adjust zoom of the app, use Ctrl + mouse scroll. To change current view area (panning), Ctrl + drag
- Don't try ^C or ^Z, which would sigterm everything. Also, don't use Ctrl + - or =, which breaks the grid
- Close opened windows and ^C

## Dependencies
- libX11
- libXext (XShm)
- libXtst (XTest)
- libXamage (XDamage)
- C++17 compiler with SSE2 support
- SIMD