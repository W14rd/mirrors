#pragma once
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <cstdint>
#include <unordered_map>
#include <string>

class ANSIRenderer; // Forward declaration

class InputHandler {
private:
    Display* display;
    Window target_window;
    int window_width;
    int window_height;
    int term_cols;
    int term_lines;
    
    // Mouse state tracking
    uint8_t button_state;  // Bit mask for pressed buttons
    int last_mouse_x;
    int last_mouse_y;
    
    // Panning state
    bool potential_pan;
    bool panning_active;
    int pan_start_x;
    int pan_start_y;
    
    // Shell process for signal forwarding
    pid_t shell_pid;
    
    // Escape sequence to X11 KeySym mapping
    std::unordered_map<std::string, unsigned int> key_mapping;
    
    ANSIRenderer* renderer; // For zoom control
    
    void initKeyMappings();
    bool parseEscapeSequence(const char* buf, int len, int& consumed);
    bool parseSGRMouse(const char* buf, int len, int& consumed);
    
public:
    InputHandler();
    ~InputHandler();
    
    bool init(const char* display_name, Window win, 
              int win_w, int win_h, int t_cols, int t_lines);
    
    void setRenderer(ANSIRenderer* r) { renderer = r; }
    void setShellPid(pid_t pid) { shell_pid = pid; }
    
    // Process input from stdin (non-blocking)
    void processInput();
    
    void updateTerminalSize(int cols, int lines) { term_cols = cols; term_lines = lines; }
    
    void cleanup();
};
