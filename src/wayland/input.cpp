#include "input.h"
#include "../renderer.h"
#include <X11/keysym.h>
#include <linux/input-event-codes.h>  // For KEY_* and BTN_* constants
#include <libei.h>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <algorithm>

#define NoSymbol 0

// XK_* to Linux input keycode mapping
static unsigned int xk_to_linux_keycode(unsigned int xk) {
    switch (xk) {
        // Letters
        case XK_a: return KEY_A;
        case XK_b: return KEY_B;
        case XK_c: return KEY_C;
        case XK_d: return KEY_D;
        case XK_e: return KEY_E;
        case XK_f: return KEY_F;
        case XK_g: return KEY_G;
        case XK_h: return KEY_H;
        case XK_i: return KEY_I;
        case XK_j: return KEY_J;
        case XK_k: return KEY_K;
        case XK_l: return KEY_L;
        case XK_m: return KEY_M;
        case XK_n: return KEY_N;
        case XK_o: return KEY_O;
        case XK_p: return KEY_P;
        case XK_q: return KEY_Q;
        case XK_r: return KEY_R;
        case XK_s: return KEY_S;
        case XK_t: return KEY_T;
        case XK_u: return KEY_U;
        case XK_v: return KEY_V;
        case XK_w: return KEY_W;
        case XK_x: return KEY_X;
        case XK_y: return KEY_Y;
        case XK_z: return KEY_Z;
        
        // Numbers
        case XK_0: return KEY_0;
        case XK_1: return KEY_1;
        case XK_2: return KEY_2;
        case XK_3: return KEY_3;
        case XK_4: return KEY_4;
        case XK_5: return KEY_5;
        case XK_6: return KEY_6;
        case XK_7: return KEY_7;
        case XK_8: return KEY_8;
        case XK_9: return KEY_9;
        
        // Special keys
        case XK_Return: return KEY_ENTER;
        case XK_space: return KEY_SPACE;
        case XK_Tab: return KEY_TAB;
        case XK_BackSpace: return KEY_BACKSPACE;
        case XK_Delete: return KEY_DELETE;
        case XK_ISO_Left_Tab: return KEY_TAB;
        case XK_Escape: return KEY_ESC;
        
        // Arrows
        case XK_Up: return KEY_UP;
        case XK_Down: return KEY_DOWN;
        case XK_Left: return KEY_LEFT;
        case XK_Right: return KEY_RIGHT;
        
        // Navigation
        case XK_Home: return KEY_HOME;
        case XK_End: return KEY_END;
        case XK_Page_Up: return KEY_PAGEUP;
        case XK_Page_Down: return KEY_PAGEDOWN;
        case XK_Insert: return KEY_INSERT;
        
        // Function keys
        case XK_F1: return KEY_F1;
        case XK_F2: return KEY_F2;
        case XK_F3: return KEY_F3;
        case XK_F4: return KEY_F4;
        case XK_F5: return KEY_F5;
        case XK_F6: return KEY_F6;
        case XK_F7: return KEY_F7;
        case XK_F8: return KEY_F8;
        case XK_F9: return KEY_F9;
        case XK_F10: return KEY_F10;
        case XK_F11: return KEY_F11;
        case XK_F12: return KEY_F12;
        
        // Punctuation
        case XK_minus: return KEY_MINUS;
        case XK_equal: return KEY_EQUAL;
        case XK_bracketleft: return KEY_LEFTBRACE;
        case XK_bracketright: return KEY_RIGHTBRACE;
        case XK_backslash: return KEY_BACKSLASH;
        case XK_semicolon: return KEY_SEMICOLON;
        case XK_apostrophe: return KEY_APOSTROPHE;
        case XK_comma: return KEY_COMMA;
        case XK_period: return KEY_DOT;
        case XK_slash: return KEY_SLASH;
        case XK_grave: return KEY_GRAVE;
        
        // Modifiers
        case XK_Shift_L: return KEY_LEFTSHIFT;
        case XK_Shift_R: return KEY_RIGHTSHIFT;
        case XK_Control_L: return KEY_LEFTCTRL;
        case XK_Control_R: return KEY_RIGHTCTRL;
        case XK_Alt_L: return KEY_LEFTALT;
        case XK_Alt_R: return KEY_RIGHTALT;
        
        default:
            return 0;
    }
}

WaylandInputHandler::WaylandInputHandler()
    : ei_ctx(nullptr), ei_keyboard(nullptr), ei_pointer(nullptr), ei_seat(nullptr),
      window_width(0), window_height(0), term_cols(0), term_lines(0),
      button_state(0), last_mouse_x(0), last_mouse_y(0),
      potential_pan(false), panning_active(false), pan_start_x(0), pan_start_y(0),
      shell_pid(-1), renderer(nullptr) {
}

WaylandInputHandler::~WaylandInputHandler() {
    cleanup();
}

void WaylandInputHandler::initKeyMappings() {
    // Special keys
    key_mapping["\r"] = XK_Return;
    key_mapping["\n"] = XK_Return;
    key_mapping[" "] = XK_space;
    key_mapping["\t"] = XK_Tab;
    key_mapping["\177"] = XK_BackSpace;
    key_mapping["\033[3~"] = XK_Delete;
    key_mapping["\033[Z"] = XK_ISO_Left_Tab;
    
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
    
    // Rxvt Style Shift + Arrows
    key_mapping["\033[a"] = XK_Up;
    key_mapping["\033[b"] = XK_Down;
    key_mapping["\033[c"] = XK_Right;
    key_mapping["\033[d"] = XK_Left;

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
    
    // Ctrl + Shift + Arrows
    key_mapping["\033[1;6A"] = XK_Up;
    key_mapping["\033[1;6B"] = XK_Down;
    key_mapping["\033[1;6C"] = XK_Right;
    key_mapping["\033[1;6D"] = XK_Left;
    
    // Shift + Home/End
    key_mapping["\033[1;2H"] = XK_Home;
    key_mapping["\033[1;2F"] = XK_End;
    
    // Shift + Page Up/Down
    key_mapping["\033[5;2~"] = XK_Page_Up;
    key_mapping["\033[6;2~"] = XK_Page_Down;
    
    // Navigation keys
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

bool WaylandInputHandler::init(int win_w, int win_h, int t_cols, int t_lines) {
    window_width = win_w;
    window_height = win_h;
    term_cols = t_cols;
    term_lines = t_lines;
    
    // libei is complex and requires compositor integration
    // For now, just initialize context - full impl would need event loop
    ei_ctx = (ei_context*)ei_new_sender(nullptr);
    if (!ei_ctx) {
        std::cerr << "libei not available (needs compositor support)\n";
        return true;  // Not fatal, just won't inject
    }
    
    std::cerr << "Wayland input initialized (libei available)\n";
    
    initKeyMappings();
    
    // Set stdin to non-blocking
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    
    return true;
}

void WaylandInputHandler::processInput() {
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
        
        // Ctrl+\ (0x1C) - exit mirrors application
        if (buf[pos] == 0x1C) {
            std::cerr << "Ctrl+\\ detected, exiting\n";
            exit(0);
        }
        
        bool handled = false;
        
        // Check single char mappings first
        std::string single_char(1, buf[pos]);
        auto it = key_mapping.find(single_char);
        if (it != key_mapping.end()) {
            unsigned int linux_key = xk_to_linux_keycode(it->second);
            if (linux_key != 0 && ei_keyboard) {
                ei_device_keyboard_key(ei_keyboard, linux_key, true);
                ei_device_keyboard_key(ei_keyboard, linux_key, false);
                ei_device_frame(ei_keyboard, 0);
            }
            handled = true;
        } else if (buf[pos] > 0 && buf[pos] <= 26) {
            // Generic Ctrl+Letter
            unsigned int letter = KEY_A + (buf[pos] - 1);
            if (ei_keyboard) {
                ei_device_keyboard_key(ei_keyboard, KEY_LEFTCTRL, true);
                ei_device_keyboard_key(ei_keyboard, letter, true);
                ei_device_keyboard_key(ei_keyboard, letter, false);
                ei_device_keyboard_key(ei_keyboard, KEY_LEFTCTRL, false);
                ei_device_frame(ei_keyboard, 0);
            }
            handled = true;
        }
        
        if (!handled) {
            // Regular character
            char c = buf[pos];
            if (c >= 32 && c <= 126) {
                unsigned int linux_key = 0;
                bool need_shift = false;
                
                if (c >= 'A' && c <= 'Z') {
                    linux_key = KEY_A + (c - 'A');
                    need_shift = true;
                } else if (c >= 'a' && c <= 'z') {
                    linux_key = KEY_A + (c - 'a');
                } else if (c >= '0' && c <= '9') {
                    linux_key = KEY_0 + (c - '0');
                } else {
                    unsigned int xk = NoSymbol;
                    switch (c) {
                        case '!': xk = XK_1; need_shift = true; break;
                        case '@': xk = XK_2; need_shift = true; break;
                        case '#': xk = XK_3; need_shift = true; break;
                        case '$': xk = XK_4; need_shift = true; break;
                        case '%': xk = XK_5; need_shift = true; break;
                        case '^': xk = XK_6; need_shift = true; break;
                        case '&': xk = XK_7; need_shift = true; break;
                        case '*': xk = XK_8; need_shift = true; break;
                        case '(': xk = XK_9; need_shift = true; break;
                        case ')': xk = XK_0; need_shift = true; break;
                        case '_': xk = XK_minus; need_shift = true; break;
                        case '+': xk = XK_equal; need_shift = true; break;
                        case '{': xk = XK_bracketleft; need_shift = true; break;
                        case '}': xk = XK_bracketright; need_shift = true; break;
                        case '|': xk = XK_backslash; need_shift = true; break;
                        case ':': xk = XK_semicolon; need_shift = true; break;
                        case '"': xk = XK_apostrophe; need_shift = true; break;
                        case '<': xk = XK_comma; need_shift = true; break;
                        case '>': xk = XK_period; need_shift = true; break;
                        case '?': xk = XK_slash; need_shift = true; break;
                        case '~': xk = XK_grave; need_shift = true; break;
                        case ' ': xk = XK_space; break;
                        case '-': xk = XK_minus; break;
                        case '=': xk = XK_equal; break;
                        case '[': xk = XK_bracketleft; break;
                        case ']': xk = XK_bracketright; break;
                        case '\\': xk = XK_backslash; break;
                        case ';': xk = XK_semicolon; break;
                        case '\'': xk = XK_apostrophe; break;
                        case ',': xk = XK_comma; break;
                        case '.': xk = XK_period; break;
                        case '/': xk = XK_slash; break;
                        case '`': xk = XK_grave; break;
                    }
                    if (xk != NoSymbol) {
                        linux_key = xk_to_linux_keycode(xk);
                    }
                }
                
                if (linux_key != 0 && ei_keyboard) {
                    if (need_shift) ei_device_keyboard_key(ei_keyboard, KEY_LEFTSHIFT, true);
                    ei_device_keyboard_key(ei_keyboard, linux_key, true);
                    ei_device_keyboard_key(ei_keyboard, linux_key, false);
                    if (need_shift) ei_device_keyboard_key(ei_keyboard, KEY_LEFTSHIFT, false);
                    ei_device_frame(ei_keyboard, 0);
                }
            }
        }
        
        pos++;
    }
}

bool WaylandInputHandler::parseEscapeSequence(const char* buf, int len, int& consumed) {
    for (int i = std::min(len, 10); i >= 2; --i) {
        std::string seq(buf, i);
        auto it = key_mapping.find(seq);
        if (it != key_mapping.end()) {
            unsigned int linux_key = xk_to_linux_keycode(it->second);
            if (linux_key == 0) {
                consumed = i;
                return true;
            }
            
            // Extract modifiers
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

            if (seq == "\033[a" || seq == "\033[b" || seq == "\033[c" || seq == "\033[d") {
                shift = true;
            }
            
            if (ei_keyboard) {
                if (shift) ei_device_keyboard_key(ei_keyboard, KEY_LEFTSHIFT, true);
                if (ctrl) ei_device_keyboard_key(ei_keyboard, KEY_LEFTCTRL, true);
                if (alt) ei_device_keyboard_key(ei_keyboard, KEY_LEFTALT, true);
                
                ei_device_keyboard_key(ei_keyboard, linux_key, true);
                ei_device_keyboard_key(ei_keyboard, linux_key, false);
                
                if (alt) ei_device_keyboard_key(ei_keyboard, KEY_LEFTALT, false);
                if (ctrl) ei_device_keyboard_key(ei_keyboard, KEY_LEFTCTRL, false);
                if (shift) ei_device_keyboard_key(ei_keyboard, KEY_LEFTSHIFT, false);
                
                ei_device_frame(ei_keyboard, 0);
            }
            
            consumed = i;
            return true;
        }
    }
    return false;
}

bool WaylandInputHandler::parseSGRMouse(const char* buf, int len, int& consumed) {
    if (len < 9 || buf[0] != '\033' || buf[1] != '[' || buf[2] != '<') {
        return false;
    }
    
    int button, x, y;
    char event_type;
    int parsed = sscanf(buf + 3, "%d;%d;%d%c", &button, &x, &y, &event_type);
    
    if (parsed != 4 || (event_type != 'M' && event_type != 'm')) {
        return false;
    }
    
    const char* end = strchr(buf + 3, event_type);
    if (!end) return false;
    consumed = (end - buf) + 1;
    
    // Convert terminal coords to window coords
    int win_x, win_y;
    
    if (renderer) {
        renderer->mapTermToImage(x - 1, y - 1, win_x, win_y);
    } else {
        float scale_x = (float)window_width / term_cols;
        float scale_y = (float)window_height / term_lines;
        win_x = (int)((x - 1) * scale_x);
        win_y = (int)((y - 1) * scale_y);
    }
    
    // Handle zoom/pan (Ctrl+Wheel and Ctrl+Drag)
    if (button & 64) {
        int wheel_code = button & 3;
        bool ctrl_held = (button & 16) != 0;
        
        if (ctrl_held && renderer) {
            static float current_zoom = 1.0f;
            if (wheel_code == 0) {
                current_zoom += 0.5f;
                if (current_zoom > 10.0f) current_zoom = 10.0f;
                renderer->setZoom(current_zoom, x - 1, y - 1);
            } else if (wheel_code == 1) {
                current_zoom -= 0.5f;
                if (current_zoom < 1.0f) current_zoom = 1.0f;
                renderer->setZoom(current_zoom, x - 1, y - 1);
            }
            return true;
        }
    } else {
        int btn_code = button & 3;
        bool is_drag = (button & 32) != 0;
        bool ctrl_held = (button & 16) != 0;
        
        if (ctrl_held && btn_code == 0 && renderer) {
            if (event_type == 'M') {
                if (!is_drag) {
                    potential_pan = true;
                    panning_active = false;
                    pan_start_x = x;
                    pan_start_y = y;
                    last_mouse_x = x;
                    last_mouse_y = y;
                    return true;
                } else {
                    if (potential_pan && (x != pan_start_x || y != pan_start_y)) {
                        panning_active = true;
                    }
                    
                    if (panning_active) {
                        int dx = x - last_mouse_x;
                        int dy = y - last_mouse_y;
                        if (dx != 0 || dy != 0) {
                            renderer->moveViewport(-dx, -dy);
                        }
                        last_mouse_x = x;
                        last_mouse_y = y;
                        return true;
                    }
                }
            } else {
                if (panning_active) {
                    panning_active = false;
                    potential_pan = false;
                    return true;
                } else if (potential_pan) {
                    potential_pan = false;
                    if (ei_pointer) {
                        ei_device_button_button(ei_pointer, BTN_LEFT, true);
                        ei_device_button_button(ei_pointer, BTN_LEFT, false);
                        ei_device_frame(ei_pointer, 0);
                    }
                    return true;
                }
            }
        }
    }
    
    // Inject mouse events via libei
    if (ei_pointer) {
        ei_device_pointer_motion_absolute(ei_pointer, win_x, win_y);
        
        if (button & 64) {
            // Wheel - use REL_WHEEL for scrolling
            int wheel_code = button & 3;
            if (event_type == 'M') {
                // libei uses relative wheel events, not buttons
                // Just skip for now - would need ei_device_scroll()
            }
        } else {
            // Regular buttons
            int btn_code = button & 3;
            bool is_drag = (button & 32) != 0;
            int btn = (btn_code == 0) ? BTN_LEFT :
                      (btn_code == 1) ? BTN_MIDDLE :
                      (btn_code == 2) ? BTN_RIGHT : 0;
            
            if (btn && ei_pointer) {
                if (event_type == 'M' && !is_drag) {
                    ei_device_button_button(ei_pointer, btn, true);
                } else if (event_type == 'm') {
                    ei_device_button_button(ei_pointer, btn, false);
                }
            }
        }
        
        ei_device_frame(ei_pointer, 0);
    }
    
    last_mouse_x = x;
    last_mouse_y = y;
    
    return true;
}

void WaylandInputHandler::cleanup() {
    if (ei_keyboard) {
        ei_device_unref(ei_keyboard);
        ei_keyboard = nullptr;
    }
    if (ei_pointer) {
        ei_device_unref(ei_pointer);
        ei_pointer = nullptr;
    }
    if (ei_seat) {
        ei_seat_unref(ei_seat);
        ei_seat = nullptr;
    }
    if (ei_ctx) {
        ei_unref((struct ei*)ei_ctx);
        ei_ctx = nullptr;
    }
}
