#pragma once
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <cstdint>
#include <unordered_map>
#include <string>

class ANSIRenderer;

class InputHandler {
private:
    Display* display;
    Window target_window;
    int window_width;
    int window_height;
    int term_cols;
    int term_lines;
    
    uint8_t button_state;
    int last_mouse_x;
    int last_mouse_y;
    
    bool potential_pan;
    bool panning_active;
    int pan_start_x;
    int pan_start_y;
    
    pid_t shell_pid;
    bool track_mouse_move;
    
    std::unordered_map<std::string, unsigned int> key_mapping;
    
    ANSIRenderer* renderer;
    
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
    void setTrackMouseMove(bool track) { track_mouse_move = track; }
    
    void processInput();
    
    void updateTerminalSize(int cols, int lines) { term_cols = cols; term_lines = lines; }
    
    void cleanup();
};
