#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <pipewire/pipewire.h>
#include <spa/utils/hook.h>

class WaylandCapturer {
private:
    pw_thread_loop* loop;
    pw_context* context;
    pw_stream* stream;
    struct spa_hook stream_listener;
    
    int width;
    int height;
    uint8_t* frame_buffer;
    size_t frame_size;
    bool frame_ready;
    bool stream_connected;
    
    // Cursor data
    struct CursorCache {
        uint64_t hash;
        std::vector<uint32_t> pixels;
        int width, height;
        int xhot, yhot;
        std::string name;
        bool visible;
        int x, y;
    };
    CursorCache cursor_cache;
    
    // PipeWire callbacks
    static void onStreamParamChanged(void* data, uint32_t id, const struct spa_pod* param);
    static void onStreamProcess(void* data);
    static void onStreamStateChanged(void* data, enum pw_stream_state old_state,
                                      enum pw_stream_state state, const char* error);

public:
    WaylandCapturer();
    ~WaylandCapturer();
    
    // Initialize capture with PipeWire
    bool init(int w, int h);
    
    // Check if frame needs update
    bool isDirty();
    
    // Capture current frame
    uint8_t* captureFrame(bool force = false);
    
    // Get image dimensions
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    
    // Get bytes per line
    int getBytesPerLine() const { return width * 4; }
    
    // Cursor data structure (compatible with X11Capturer)
    struct CursorData {
        std::vector<uint32_t> pixels;
        int width, height;
        int x, y;
        int xhot, yhot;
        bool visible;
        std::string name;
        uint64_t hash;
        bool changed;
    };
    
    CursorData getCursor();
    
    // Cleanup
    void cleanup();
    
    // Process events (compatibility with X11)
    void processEvents() {}
};
