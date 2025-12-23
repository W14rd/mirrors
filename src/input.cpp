#include "input.h"
#include "renderer.h"
#include <X11/keysym.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <csignal>
#include <iostream>

InputHandler::InputHandler() 
    : display(nullptr), target_window(0), term_cols(0), term_lines(0),
      button_state(0), last_mouse_x(0), last_mouse_y(0),
      potential_pan(false), panning_active(false), pan_start_x(0), pan_start_y(0),
      shell_pid(-1), renderer(nullptr) {
}

InputHandler::~InputHandler() {
    cleanup();
}

void InputHandler::initKeyMappings() {

    key_mapping["\r"] = XK_Return;
    key_mapping["\n"] = XK_Return;
    key_mapping[" "] = XK_space;
    key_mapping["\t"] = XK_Tab;
    key_mapping["\177"] = XK_BackSpace;
    key_mapping["\033[3~"] = XK_Delete;
    key_mapping["\033[Z"] = XK_ISO_Left_Tab;
    

    key_mapping["\033[13;2u"] = XK_Return;
    key_mapping["\033\177"] = XK_BackSpace;
    key_mapping["\033[7;5~"] = XK_BackSpace;
    key_mapping["\010"] = XK_BackSpace;
    key_mapping["\033[3;5~"] = XK_Delete;

   
    key_mapping["\033[A"] = XK_Up;
    key_mapping["\033[B"] = XK_Down;
    key_mapping["\033[C"] = XK_Right;
    key_mapping["\033[D"] = XK_Left;
    

    key_mapping["\033[a"] = XK_Up;   
    key_mapping["\033[b"] = XK_Down;  
    key_mapping["\033[c"] = XK_Right;
    key_mapping["\033[d"] = XK_Left; 


    key_mapping["\033[1;2A"] = XK_Up;
    key_mapping["\033[1;2B"] = XK_Down;
    key_mapping["\033[1;2C"] = XK_Right;
    key_mapping["\033[1;2D"] = XK_Left;
    
   
    key_mapping["\033[1;5A"] = XK_Up;
    key_mapping["\033[1;5B"] = XK_Down;
    key_mapping["\033[1;5C"] = XK_Right;
    key_mapping["\033[1;5D"] = XK_Left;
    
  
    key_mapping["\033[1;6A"] = XK_Up;
    key_mapping["\033[1;6B"] = XK_Down;
    key_mapping["\033[1;6C"] = XK_Right;
    key_mapping["\033[1;6D"] = XK_Left;
    

    key_mapping["\033[1;2H"] = XK_Home;
    key_mapping["\033[1;2F"] = XK_End;
    
 
    key_mapping["\033[5;2~"] = XK_Page_Up;
    key_mapping["\033[6;2~"] = XK_Page_Down;
    
  
    key_mapping["\033[H"] = XK_Home;
    key_mapping["\033[1~"] = XK_Home;
    key_mapping["\033[F"] = XK_End;
    key_mapping["\033[4~"] = XK_End;
    key_mapping["\033[2~"] = XK_Insert;
    key_mapping["\033[3~"] = XK_Delete;
    key_mapping["\033[5~"] = XK_Page_Up;
    key_mapping["\033[6~"] = XK_Page_Down;
    

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
    
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    
    return true;
}

void InputHandler::processInput() {
    char buf[256];
    int n = read(STDIN_FILENO, buf, sizeof(buf));
    
    if (n <= 0) return;
    
    int pos = 0;
    while (pos < n) {
        int consumed = 0;
         if (buf[pos] == '\033') {
            if (parseEscapeSequence(buf + pos, n - pos, consumed) ||
                parseSGRMouse(buf + pos, n - pos, consumed)) {
                pos += consumed;
                continue;
            }
        }
        
        if (buf[pos] == 0x1C) {
            std::cerr << "Ctrl+\\ detected, exiting" << std::endl;
            exit(0);
        }
        
        bool handled = false;
        
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
            
            char c = buf[pos];
            KeySym ks = NoSymbol;
            KeyCode kc = 0;
            bool need_shift = false;
            

            if (c >= 32 && c <= 126) {

                switch (c) {
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
                    

                    case 'A'...'Z':
                        ks = XK_a + (c - 'A');
                        need_shift = true;
                        break;
                    

                    case 'a'...'z':
                        ks = XK_a + (c - 'a');
                        break;
                    

                    case '0'...'9':
                        ks = XK_0 + (c - '0');
                        break;
                    

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

    for (int i = std::min(len, 10); i >= 2; --i) {
        std::string seq(buf, i);
        auto it = key_mapping.find(seq);
        if (it != key_mapping.end()) {
            KeyCode kc = XKeysymToKeycode(display, it->second);
            if (kc != 0) {
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
    

    int win_x, win_y;
    
    if (renderer) {
        renderer->mapTermToImage(x - 1, y - 1, win_x, win_y);
    } else {
        float scale_x = (float)window_width / term_cols;
        float scale_y = (float)window_height / term_lines;
        win_x = (int)((x - 1) * scale_x);
        win_y = (int)((y - 1) * scale_y);
    }
    
    int xbutton = 0;
    bool is_drag = false;
    
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
        
        if (wheel_code == 0) xbutton = 4;
        else if (wheel_code == 1) xbutton = 5;
        else if (wheel_code == 2) xbutton = 6;
        else if (wheel_code == 3) xbutton = 7;
    } else {
        int btn_code = button & 3;
        is_drag = (button & 32) != 0;
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
                    if (potential_pan) {
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
                    
                    KeyCode ctrl_l = XKeysymToKeycode(display, XK_Control_L);
                    XTestFakeKeyEvent(display, ctrl_l, True, 0);
                    XTestFakeButtonEvent(display, 1, True, 0);
                    XTestFakeButtonEvent(display, 1, False, 0);
                    XTestFakeKeyEvent(display, ctrl_l, False, 0);
                    XFlush(display);
                    return true;
                }
            }
        }
        
        if (btn_code == 0) xbutton = 1;
        else if (btn_code == 1) xbutton = 2;
        else if (btn_code == 2) xbutton = 3;
    }
    
    last_mouse_x = x;
    last_mouse_y = y;
    
    XTestFakeMotionEvent(display, -1, win_x, win_y, 0);
    
    if (xbutton > 0) {
        if (xbutton >= 4 && xbutton <= 7) {
            if (event_type == 'M') {
                XTestFakeButtonEvent(display, xbutton, True, 0);
                XTestFakeButtonEvent(display, xbutton, False, 0);
            }
        } else {
            if (event_type == 'M') {
                if (is_drag) {
                } else {
                    XTestFakeButtonEvent(display, xbutton, True, 0);
                }
            } else {
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
