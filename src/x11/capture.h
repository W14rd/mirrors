#pragma once
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <sys/shm.h>
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

class X11Capturer {
private:
    Display* display;
    Window window;
    XShmSegmentInfo shminfo;
    XImage* ximage;
    int width;
    int height;
    bool using_shm;
    
    // Damage tracking
    Damage damage;
    int damage_event_base;
    int damage_error_base;
    bool damage_available;
    
    // XFixes for cursor
    int xfixes_event_base;
    int xfixes_error_base;
    
    // Performance optimization: track if frame is dirty
    bool frame_dirty;
    
    // Cursor cache
    struct CursorCache {
        uint64_t hash;
        std::vector<uint32_t> pixels;
        int width, height;
        int xhot, yhot;
        std::string name;
    };
    CursorCache cursor_cache;
    
    // Initialize extensions
    bool initDamage();
    bool initXFixes();

public:
    X11Capturer();
    ~X11Capturer();
    
    // Initialize capture for a specific window
    bool init(const char* display_name, Window target_window, int w, int h);
    
    // Check if frame needs update (damage-based)
    bool isDirty();
    
    // Clear dirty flag and damage regions
    void clearDamage();
    
    // Capture current frame to shared memory buffer
    // Returns pointer to RGB data (format: BGBGBGBG... typically)
    // Returns nullptr if no capture needed (no damage)
    uint8_t* captureFrame(bool force = false);
    
    // Get image dimensions
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    
    // Get bytes per line (may include padding)
    int getBytesPerLine() const { return ximage ? ximage->bytes_per_line : width * 4; }
    
    // Get color format info
    uint32_t getRedMask() const { return ximage ? ximage->red_mask : 0; }
    uint32_t getGreenMask() const { return ximage ? ximage->green_mask : 0; }
    uint32_t getBlueMask() const { return ximage ? ximage->blue_mask : 0; }
    
    // Cleanup
    void cleanup();

    struct CursorData {
        std::vector<uint32_t> pixels;
        int width, height;
        int x, y;
        int xhot, yhot;
        bool visible;
        std::string name;
        uint64_t hash;
        bool changed; // Indicates if cursor changed since last call
    };
    
    CursorData getCursor();
    
    // Process pending X events (for damage tracking)
    void processEvents();
};