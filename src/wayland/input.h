#pragma once
#include <cstdint>
#include <unordered_map>
#include <string>

// Forward declarations for libei types
struct ei;
struct ei_device;
struct ei_seat;

// Use typedefs to avoid conflicts
typedef struct ei ei_context;

class ANSIRenderer;

class WaylandInputHandler {
private:
    ei_context* ei_ctx;
    ei_device* ei_keyboard;
    ei_device* ei_pointer;
    struct ei_seat* ei_seat;
    
    int window_width;
    int window_height;
    int term_cols;
    int term_lines;
    
    // Mouse state tracking
    uint8_t button_state;
    int last_mouse_x;
    int last_mouse_y;
    
    // Panning state
    bool potential_pan;
    bool panning_active;
    int pan_start_x;
    int pan_start_y;
    
    // Shell process for signal forwarding (compatibility)
    pid_t shell_pid;
    
    // Key mapping
    std::unordered_map<std::string, unsigned int> key_mapping;
    
    ANSIRenderer* renderer;
    
    void initKeyMappings();
    bool parseEscapeSequence(const char* buf, int len, int& consumed);
    bool parseSGRMouse(const char* buf, int len, int& consumed);
    
public:
    WaylandInputHandler();
    ~WaylandInputHandler();
    
    bool init(int win_w, int win_h, int t_cols, int t_lines);
    
    void setRenderer(ANSIRenderer* r) { renderer = r; }
    void setShellPid(pid_t pid) { shell_pid = pid; }
    
    // Process input from stdin (non-blocking)
    void processInput();
    
    void updateTerminalSize(int cols, int lines) { term_cols = cols; term_lines = lines; }
    
    void cleanup();
};
