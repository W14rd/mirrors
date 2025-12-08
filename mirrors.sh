#!/bin/bash
# mirrors-sh: Robust Single-Desktop Mode with Isolation

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_BINARY="${SCRIPT_DIR}/build/mirrors"
WM_BINARY="${SCRIPT_DIR}/build/mirrors-wm"

# Configuration
REFRESH_RATE=30
SCREEN_WIDTH=1920
SCREEN_HEIGHT=1080
BINARY_PATH=""
BINARY_ARGS=()
CELL_CHAR=""
RENDER_MODE_FLAG="--rgb" # Default is now --rgb

# --- 1. Help Function ---
show_help() {
    echo "Before use, fullscreen, max unzoom and F11 for full kino experience."
    echo "Usage: $(basename "$0") [options] <command> [args...]"
    echo ""
    echo "Options:"
    echo "  -r, --refresh-rate <fps>   Set target FPS (default: 30)"
    echo "  -w, --width <pixels>       Set virtual screen width (default: 1920)"
    echo "  -h, --height <pixels>      Set virtual screen height (default: 1080)"
    echo "  --cell <char>              Use a specific character for rendering"
    echo "  --rgb                      Enable TrueColor (24-bit) rendering (Default)"
    echo "  --ansi                     Disable --rgb (Standard ANSI colors)"
    echo "  --grey                     Enable Grayscale rendering (faster)"
    echo "  --help                     Show this help message"
    echo "Deps: C++17, libX11, libXext, libXtst, SIMD"
    echo "  --help                     Show this help message"
    echo "SIMD"
}

# --- 2. Dependency Checks ---
if [ ! -x "$CPP_BINARY" ]; then
    echo "Error: C++ binary not found at $CPP_BINARY"
    exit 1
fi
if [ ! -x "$WM_BINARY" ]; then
    echo "Error: WM binary not found at $WM_BINARY"
    exit 1
fi
if ! command -v Xvfb >/dev/null 2>&1; then echo "Error: Xvfb not found"; exit 1; fi
if ! command -v xdotool >/dev/null 2>&1; then echo "Error: xdotool not found"; exit 1; fi

# --- 3. Argument Parsing ---
# (Moved before Trap setup to prevent premature cleanup on --help)
while [ $# -gt 0 ]; do
    case "$1" in
        -r|--refresh-rate) REFRESH_RATE="$2"; shift 2 ;;
        -w|--width) SCREEN_WIDTH="$2"; shift 2 ;;
        -h|--height) SCREEN_HEIGHT="$2"; shift 2 ;;
        --cell) CELL_CHAR="$2"; shift 2 ;;
        --ansi) RENDER_MODE_FLAG="--ansi"; shift ;;
        --rgb) RENDER_MODE_FLAG=""; shift ;;
        --grey|--gray) RENDER_MODE_FLAG="--grey"; shift ;;
        --help) show_help; exit 0 ;; # Exits cleanly without triggering cleanup
        *)
            if [ -z "$BINARY_PATH" ]; then BINARY_PATH="$1"; else BINARY_ARGS+=("$1"); fi
            shift
            ;;
    esac
done

if [ -z "$BINARY_PATH" ]; then
    echo "Error: No command provided." >&2
    show_help
    exit 1
fi

if ! command -v "$BINARY_PATH" >/dev/null 2>&1; then
    echo "Error: Command '$BINARY_PATH' not found." >&2
    exit 1
fi

# --- 4. Cleanup Logic (Trap Setup) ---
# We enable this ONLY after argument validation succeeded
cleanup() {
    if [ -n "$CPP_PID" ]; then kill $CPP_PID 2>/dev/null; fi
    if [ -n "$APP_PID" ]; then kill $APP_PID 2>/dev/null; fi 
    if [ -n "$WM_PID" ]; then kill $WM_PID 2>/dev/null; fi
    if [ -n "$XVFB_PID" ]; then kill $XVFB_PID 2>/dev/null; fi
    tput reset
}
trap cleanup EXIT INT TERM

# --- 5. Environment Setup ---
detect_and_start_wm() {
    # Start custom WM
    "$WM_BINARY" >/dev/null 2>&1 &
    WM_PID=$!
    sleep 0.5
}

start_display_environment() {
    local w=$1; local h=$2
    local disp=99
    while [ -e "/tmp/.X${disp}-lock" ]; do disp=$((disp+1)); done
    export DISPLAY=":${disp}"
    
    Xvfb "${DISPLAY}" -screen 0 "${w}x${h}x24" +extension RANDR >/dev/null 2>&1 &
    XVFB_PID=$!
    sleep 1
    
    if ! kill -0 $XVFB_PID 2>/dev/null; then echo "Error: Xvfb failed."; exit 1; fi
    detect_and_start_wm
}

# --- 6. Execution ---
start_display_environment "$SCREEN_WIDTH" "$SCREEN_HEIGHT"

# Isolation
ISOLATION_CMD=""
if command -v dbus-run-session >/dev/null 2>&1; then ISOLATION_CMD="dbus-run-session";
elif command -v dbus-launch >/dev/null 2>&1; then ISOLATION_CMD="dbus-launch --exit-with-session";
else unset DBUS_SESSION_BUS_ADDRESS; fi

# Launch App
$ISOLATION_CMD "$BINARY_PATH" "${BINARY_ARGS[@]}" >/dev/null 2>&1 &
APP_PID=$!

# Find Window (Robust)
echo "Waiting for visible windows..." >&2
COUNTER=0
while [ $COUNTER -lt 50 ]; do
    WIDS=$(xdotool search --onlyvisible ".*" 2>/dev/null)
    for wid in $WIDS; do
        GEOM=$(xdotool getwindowgeometry "$wid")
        W=$(echo "$GEOM" | awk '/Geometry/ {print $2}' | cut -d'x' -f1)
        H=$(echo "$GEOM" | awk '/Geometry/ {print $2}' | cut -d'x' -f2)
        if [ "$W" -gt 50 ] && [ "$H" -gt 50 ]; then WINDOW_ID="$wid"; break 2; fi
    done
    sleep 0.2
    COUNTER=$((COUNTER+1))
done

if [ -z "$WINDOW_ID" ]; then echo "Error: No visible window found."; exit 1; fi

xdotool windowstate --add MAXIMIZED_VERT,MAXIMIZED_HORZ "$WINDOW_ID" 2>/dev/null || true
xdotool windowsize "$WINDOW_ID" "$SCREEN_WIDTH" "$SCREEN_HEIGHT" 2>/dev/null || true
xdotool windowmove "$WINDOW_ID" 0 0 2>/dev/null || true

# Capture Root
ROOT_WINDOW=$(xwininfo -root -display "$DISPLAY" | grep "Window id:" | awk '{print $4}')
TERM_COLS=$(tput cols)
TERM_LINES=$(tput lines)

# Run Renderer with optional flags
CMD=("$CPP_BINARY" "$DISPLAY" "$ROOT_WINDOW" "$REFRESH_RATE" "$TERM_COLS" "$TERM_LINES" "$SCREEN_WIDTH" "$SCREEN_HEIGHT")
if [ -n "$CELL_CHAR" ]; then CMD+=("--cell" "$CELL_CHAR"); fi
if [ -n "$RENDER_MODE_FLAG" ]; then CMD+=("$RENDER_MODE_FLAG"); fi

"${CMD[@]}"