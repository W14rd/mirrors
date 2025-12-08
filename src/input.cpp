#include "input.h"
#include "renderer.h"
#include <X11/keysym.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>

InputHandler::InputHandler() 
    : display(nullptr), target_window(0), term_cols(0), term_lines(0),
      button_state(0), last_mouse_x(0), last_mouse_y(0),
      potential_pan(false), panning_active(false), pan_start_x(0), pan_start_y(0),
      renderer(nullptr) {
}

InputHandler::~InputHandler() {
    cleanup();
}

void InputHandler::initKeyMappings() {
    // Special keys
    key_mapping["\r"] = XK_Return;
    key_mapping["\n"] = XK_Return;
    key_mapping[" "] = XK_space;
    key_mapping["\t"] = XK_Tab;
    key_mapping["\177"] = XK_BackSpace;
    key_mapping["\033[3~"] = XK_Delete;
    key_mapping["\033[Z"] = XK_ISO_Left_Tab; // Shift+Tab
    
    // Shift+Enter / Backspace variants
    key_mapping["\033[13;2u"] = XK_Return;
    key_mapping["\033\177"] = XK_BackSpace;
    key_mapping["\033[7;5~"] = XK_BackSpace;
    key_mapping["\010"] = XK_BackSpace;
    key_mapping["\033[3;5~"] = XK_Delete;

    // Standard Arrow keys
    key_mapping["\033[A"] = XK_Up;
    key_mapping["\033[B"] = XK_Down;
    key_mapping["\033[C"] = XK_Right;
    key_mapping["\033[D"] = XK_Left;
    
    // Rxvt Style Shift + Arrows (Common cause of "Shift+Arrows" failure)
    key_mapping["\033[a"] = XK_Up;    // Rxvt Shift Up
    key_mapping["\033[b"] = XK_Down;  // Rxvt Shift Down
    key_mapping["\033[c"] = XK_Right; // Rxvt Shift Right
    key_mapping["\033[d"] = XK_Left;  // Rxvt Shift Left

    // Xterm Style Shift + Arrows
    key_mapping["\033[1;2A"] = XK_Up;
    key_mapping["\033[1;2B"] = XK_Down;
    key_mapping["\033[1;2C"] = XK_Right;
    key_mapping["\033[1;2D"] = XK_Left;
    
    // Ctrl + Arrows  
    key_mapping["\033[1;5A"] = XK_Up;
    key_mapping["\033[1;5B"] = XK_Down;
    key_mapping["\033[1;5C"] = XK_Right;
    key_mapping["\033[1;5D"] = XK_Left;
    
    // Ctrl + Shift + Arrows (Standard Xterm)
    key_mapping["\033[1;6A"] = XK_Up;
    key_mapping["\033[1;6B"] = XK_Down;
    key_mapping["\033[1;6C"] = XK_Right;
    key_mapping["\033[1;6D"] = XK_Left;
    
    // Shift + Home/End
    key_mapping["\033[1;2H"] = XK_Home;
    key_mapping["\033[1;2F"] = XK_End;
    
    // Shift + Page Up/Down (Xterm)
    key_mapping["\033[5;2~"] = XK_Page_Up;
    key_mapping["\033[6;2~"] = XK_Page_Down;
    
    // Navigation keys (Home/End/PageUp/PageDown)
    key_mapping["\033[H"] = XK_Home;
    key_mapping["\033[1~"] = XK_Home;
    key_mapping["\033[F"] = XK_End;
    key_mapping["\033[4~"] = XK_End;
    key_mapping["\033[2~"] = XK_Insert;
    key_mapping["\033[3~"] = XK_Delete;
    key_mapping["\033[5~"] = XK_Page_Up;
    key_mapping["\033[6~"] = XK_Page_Down;
    
    // Function keys
    key_mapping["\033OP"] = XK_F1;
    key_mapping["\033OQ"] = XK_F2;
    key_mapping["\033OR"] = XK_F3;
    key_mapping["\033OS"] = XK_F4;
    key_mapping["\033[15~"] = XK_F5;
    key_mapping["\033[17~"] = XK_F6;
    key_mapping["\033[18~"] = XK_F7;
    key_mapping["\033[19~"] = XK_F8;
    key_mapping["\033[20~"] = XK_F9;
    key_mapping["\033[21~"] = XK_F10;
    key_mapping["\033[23~"] = XK_F11;
    key_mapping["\033[24~"] = XK_F12;
}

bool InputHandler::init(const char* display_name, Window win,
                       int win_w, int win_h, int t_cols, int t_lines) {
    display = XOpenDisplay(display_name);
    if (!display) return false;
    
    target_window = win;
    window_width = win_w;
    window_height = win_h;
    term_cols = t_cols;
    term_lines = t_lines;
    
    initKeyMappings();
    
    // Set stdin to non-blocking
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    
    // Enable mouse reporting (will be enabled by shell wrapper)
    // write(STDOUT_FILENO, "\033[?1006h\033[?1002h\033[?1000h", 27);
    
    return true;
}

void InputHandler::processInput() {
    char buf[256];
    int n = read(STDIN_FILENO, buf, sizeof(buf));
    
    if (n <= 0) return;
    
    int pos = 0;
    while (pos < n) {
        int consumed = 0;
        
        // Try to parse escape sequence
        if (buf[pos] == '\033') {
            if (parseEscapeSequence(buf + pos, n - pos, consumed) ||
                parseSGRMouse(buf + pos, n - pos, consumed)) {
                pos += consumed;
                continue;
            }
        }
        
        // Ctrl+C (handled separately for exit)
        if (buf[pos] == 0x03) {
            std::cerr << "Ctrl+C detected, exiting" << std::endl;
            exit(0);
        }
        
        bool handled = false;
        
        // Check single char mappings first (Enter, Tab, Backspace, Space, etc.)
        std::string single_char(1, buf[pos]);
        auto it = key_mapping.find(single_char);
        if (it != key_mapping.end()) {
             KeyCode kc = XKeysymToKeycode(display, it->second);
             if (kc != 0) {
                 XTestFakeKeyEvent(display, kc, True, 0);
                 XTestFakeKeyEvent(display, kc, False, 0);
                 XFlush(display);
             }
             handled = true;
        } else if (buf[pos] > 0 && buf[pos] <= 26) {
            // Generic Ctrl+Letter
            KeySym ks = XK_a + (buf[pos] - 1);
            KeyCode kc = XKeysymToKeycode(display, ks);
            KeyCode ctrl_l = XKeysymToKeycode(display, XK_Control_L);
            
            if (kc != 0) {
                XTestFakeKeyEvent(display, ctrl_l, True, 0);
                XTestFakeKeyEvent(display, kc, True, 0);
                XTestFakeKeyEvent(display, kc, False, 0);
                XTestFakeKeyEvent(display, ctrl_l, False, 0);
                XFlush(display);
            }
            handled = true;
        }
        
        if (!handled) {
            // Regular character - use proper X11 keysym mapping
            char c = buf[pos];
            KeySym ks = NoSymbol;
            KeyCode kc = 0;
            bool need_shift = false;
            
            // For printable ASCII, convert character to keysym+modifier
            if (c >= 32 && c <= 126) {
                // Map characters to their base keysyms
                switch (c) {
                    // Shifted number row
                    case '!': ks = XK_1; need_shift = true; break;
                    case '@': ks = XK_2; need_shift = true; break;
                    case '#': ks = XK_3; need_shift = true; break;
                    case '$': ks = XK_4; need_shift = true; break;
                    case '%': ks = XK_5; need_shift = true; break;
                    case '^': ks = XK_6; need_shift = true; break;
                    case '&': ks = XK_7; need_shift = true; break;
                    case '*': ks = XK_8; need_shift = true; break;
                    case '(': ks = XK_9; need_shift = true; break;
                    case ')': ks = XK_0; need_shift = true; break;
                    
                    // Shifted punctuation (correct X11 keysym names)
                    case '_': ks = XK_minus; need_shift = true; break;
                    case '+': ks = XK_equal; need_shift = true; break;
                    case '{': ks = XK_bracketleft; need_shift = true; break;
                    case '}': ks = XK_bracketright; need_shift = true; break;
                    case '|': ks = XK_backslash; need_shift = true; break;
                    case ':': ks = XK_semicolon; need_shift = true; break;
                    case '"': ks = XK_apostrophe; need_shift = true; break;
                    case '<': ks = XK_comma; need_shift = true; break;
                    case '>': ks = XK_period; need_shift = true; break;
                    case '?': ks = XK_slash; need_shift = true; break;
                    case '~': ks = XK_grave; need_shift = true; break;
                    
                    // Uppercase letters
                    case 'A'...'Z':
                        ks = XK_a + (c - 'A');
                        need_shift = true;
                        break;
                    
                    // Lowercase letters
                    case 'a'...'z':
                        ks = XK_a + (c - 'a');
                        break;
                    
                    // Numbers (unshifted)
                    case '0'...'9':
                        ks = XK_0 + (c - '0');
                        break;
                    
                    // Unshifted punctuation - direct mapping
                    case ' ': ks = XK_space; break;
                    case '-': ks = XK_minus; break;
                    case '=': ks = XK_equal; break;
                    case '[': ks = XK_bracketleft; break;
                    case ']': ks = XK_bracketright; break;
                    case '\\': ks = XK_backslash; break;
                    case ';': ks = XK_semicolon; break;
                    case '\'': ks = XK_apostrophe; break;
                    case ',': ks = XK_comma; break;
                    case '.': ks = XK_period; break;
                    case '/': ks = XK_slash; break;
                    case '`': ks = XK_grave; break;
                    
                    default:
                        // This shouldn't happen for printable ASCII
                        break;
                }
                
                if (ks != NoSymbol) {
                    kc = XKeysymToKeycode(display, ks);
                }
            }
            
            if (kc != 0) {
                KeyCode shift_l = XKeysymToKeycode(display, XK_Shift_L);
                
                if (need_shift) XTestFakeKeyEvent(display, shift_l, True, 0);
                XTestFakeKeyEvent(display, kc, True, 0);
                XTestFakeKeyEvent(display, kc, False, 0);
                if (need_shift) XTestFakeKeyEvent(display, shift_l, False, 0);
                
                XFlush(display);
            }
        }
        
        pos++;
    }
}

bool InputHandler::parseEscapeSequence(const char* buf, int len, int& consumed) {
    // Try to match longest escape sequence
    for (int i = std::min(len, 10); i >= 2; --i) {
        std::string seq(buf, i);
        auto it = key_mapping.find(seq);
        if (it != key_mapping.end()) {
            KeyCode kc = XKeysymToKeycode(display, it->second);
            if (kc != 0) {
                // Check for modifiers in the sequence string (Xterm style)
                bool shift = (seq.find(";2") != std::string::npos) || 
                             (seq.find(";4") != std::string::npos) ||
                             (seq.find(";6") != std::string::npos) ||
                             (seq.find(";8") != std::string::npos);
                             
                bool ctrl = (seq.find(";5") != std::string::npos) || 
                            (seq.find(";6") != std::string::npos) ||
                            (seq.find(";7") != std::string::npos) ||
                            (seq.find(";8") != std::string::npos);
                            
                bool alt = (seq.find(";3") != std::string::npos) || 
                           (seq.find(";4") != std::string::npos) ||
                           (seq.find(";7") != std::string::npos) ||
                           (seq.find(";8") != std::string::npos);

                // --- NEW: Handle Rxvt implicit Shift-Arrows ---
                // These codes (\033[a etc) imply Shift but don't have ";2" in them
                if (seq == "\033[a" || seq == "\033[b" || seq == "\033[c" || seq == "\033[d") {
                    shift = true;
                }
                
                KeyCode shift_l = XKeysymToKeycode(display, XK_Shift_L);
                KeyCode ctrl_l = XKeysymToKeycode(display, XK_Control_L);
                KeyCode alt_l = XKeysymToKeycode(display, XK_Alt_L);
                
                if (shift) XTestFakeKeyEvent(display, shift_l, True, 0);
                if (ctrl) XTestFakeKeyEvent(display, ctrl_l, True, 0);
                if (alt) XTestFakeKeyEvent(display, alt_l, True, 0);
                
                XTestFakeKeyEvent(display, kc, True, 0);
                XTestFakeKeyEvent(display, kc, False, 0);
                
                if (alt) XTestFakeKeyEvent(display, alt_l, False, 0);
                if (ctrl) XTestFakeKeyEvent(display, ctrl_l, False, 0);
                if (shift) XTestFakeKeyEvent(display, shift_l, False, 0);
                
                XFlush(display);
            }
            consumed = i;
            return true;
        }
    }
    return false;
}

bool InputHandler::parseSGRMouse(const char* buf, int len, int& consumed) {
    // SGR mouse format: \033[<B;X;YM (press) or \033[<B;X;Ym (release)
    if (len < 9 || buf[0] != '\033' || buf[1] != '[' || buf[2] != '<') {
        return false;
    }
    
    int button, x, y;
    char event_type;
    int parsed = sscanf(buf + 3, "%d;%d;%d%c", &button, &x, &y, &event_type);
    
    if (parsed != 4 || (event_type != 'M' && event_type != 'm')) {
        return false;
    }
    
    // Calculate consumed length
    const char* end = strchr(buf + 3, event_type);
    if (!end) return false;
    consumed = (end - buf) + 1;
    
    // Convert terminal coords to window coords
    int win_x, win_y;
    
    if (renderer) {
        // Use renderer's mapping which accounts for zoom and viewport
        renderer->mapTermToImage(x - 1, y - 1, win_x, win_y);
    } else {
        // Fallback (shouldn't happen if renderer is set)
        float scale_x = (float)window_width / term_cols;
        float scale_y = (float)window_height / term_lines;
        win_x = (int)((x - 1) * scale_x);
        win_y = (int)((y - 1) * scale_y);
    }
    
    // Map button
    int xbutton = 0;
    bool is_drag = false;
    
    if (button & 64) {
        // Wheel event (Bit 6 set)
        int wheel_code = button & 3;
        bool ctrl_held = (button & 16) != 0;
        
        if (ctrl_held && renderer) {
            // Digital Zoom
            static float current_zoom = 1.0f;
            if (wheel_code == 0) { // Scroll Up -> Zoom In
                current_zoom += 0.5f;
                if (current_zoom > 10.0f) current_zoom = 10.0f;
                renderer->setZoom(current_zoom, x - 1, y - 1);
            } else if (wheel_code == 1) { // Scroll Down -> Zoom Out
                current_zoom -= 0.5f;
                if (current_zoom < 1.0f) current_zoom = 1.0f;
                renderer->setZoom(current_zoom, x - 1, y - 1);
            }
            return true;
        }
        
        if (wheel_code == 0) xbutton = 4;      // Scroll Up
        else if (wheel_code == 1) xbutton = 5; // Scroll Down
        else if (wheel_code == 2) xbutton = 6; // Scroll Left
        else if (wheel_code == 3) xbutton = 7; // Scroll Right
    } else {
        // Regular buttons
        int btn_code = button & 3;
        is_drag = (button & 32) != 0;
        bool ctrl_held = (button & 16) != 0;
        
        // Panning Logic: Ctrl + Left Button
        if (ctrl_held && btn_code == 0 && renderer) {
            if (event_type == 'M') { // Press or Drag
                if (!is_drag) {
                    // Initial Press
                    potential_pan = true;
                    panning_active = false;
                    pan_start_x = x;
                    pan_start_y = y;
                    last_mouse_x = x;
                    last_mouse_y = y;
                    return true; // Consume press
                } else {
                    // Drag
                    if (potential_pan) {
                        // Check if moved enough to count as drag (threshold 0 for now)
                        if (x != pan_start_x || y != pan_start_y) {
                            panning_active = true;
                        }
                    }
                    
                    if (panning_active) {
                        int dx = x - last_mouse_x;
                        int dy = y - last_mouse_y;
                        if (dx != 0 || dy != 0) {
                            renderer->moveViewport(-dx, -dy);
                        }
                        last_mouse_x = x;
                        last_mouse_y = y;
                        return true; // Consume drag
                    }
                }
            } else { // Release
                if (panning_active) {
                    // End panning
                    panning_active = false;
                    potential_pan = false;
                    return true; // Consume release
                } else if (potential_pan) {
                    // Was a click, not a pan
                    potential_pan = false;
                    
                    // Send delayed Press + Release
                    // We need to ensure Ctrl is pressed (it should be, physically)
                    // But we simulate the click
                    KeyCode ctrl_l = XKeysymToKeycode(display, XK_Control_L);
                    XTestFakeKeyEvent(display, ctrl_l, True, 0); // Ensure Ctrl is down
                    XTestFakeButtonEvent(display, 1, True, 0);
                    XTestFakeButtonEvent(display, 1, False, 0);
                    XTestFakeKeyEvent(display, ctrl_l, False, 0); // Release Ctrl (optional, but safe)
                    XFlush(display);
                    return true;
                }
            }
        }
        
        if (btn_code == 0) xbutton = 1;       // Left
        else if (btn_code == 1) xbutton = 2;  // Middle
        else if (btn_code == 2) xbutton = 3;  // Right
    }
    
    // Update last mouse position for next event
    last_mouse_x = x;
    last_mouse_y = y;
    
    XTestFakeMotionEvent(display, -1, win_x, win_y, 0);
    
    if (xbutton > 0) {
        // Special handling for scroll wheels (buttons 4-7)
        // They are often stateless "clicks"
        if (xbutton >= 4 && xbutton <= 7) {
            if (event_type == 'M') {
                // Press + Release immediately
                XTestFakeButtonEvent(display, xbutton, True, 0);
                XTestFakeButtonEvent(display, xbutton, False, 0);
            }
            // Ignore release ('m') for scroll buttons to avoid double events or stuck buttons
        } else {
            // Regular buttons (Left, Middle, Right)
            if (event_type == 'M') {
                if (is_drag) {
                    // Dragging - button is already down, just updated position
                } else {
                    // Press
                    XTestFakeButtonEvent(display, xbutton, True, 0);
                }
            } else {
                // Release
                XTestFakeButtonEvent(display, xbutton, False, 0);
            }
        }
    }
    
    XFlush(display);
    return true;
}

void InputHandler::cleanup() {
    if (display) {
        XCloseDisplay(display);
        display = nullptr;
    }
}
