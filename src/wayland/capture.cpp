#include "capture.h"
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/type-info.h>
#include <spa/debug/types.h>
#include <cstring>
#include <iostream>

// Stream state changed callback
void WaylandCapturer::onStreamStateChanged(void* data, enum pw_stream_state old_state,
                                           enum pw_stream_state state, const char* error) {
    WaylandCapturer* capturer = (WaylandCapturer*)data;
    
    std::cerr << "PipeWire stream state: " << pw_stream_state_as_string(state);
    if (error) std::cerr << " (error: " << error << ")";
    std::cerr << "\n";
    
    if (state == PW_STREAM_STATE_STREAMING) {
        capturer->stream_connected = true;
        std::cerr << "PipeWire: Screen capture started\n";
    } else if (state == PW_STREAM_STATE_ERROR) {
        std::cerr << "PipeWire: Stream error\n";
        capturer->stream_connected = false;
    }
}

// Stream parameter changed callback
void WaylandCapturer::onStreamParamChanged(void* data, uint32_t id, const struct spa_pod* param) {
    WaylandCapturer* capturer = (WaylandCapturer*)data;
    
    if (!param || id != SPA_PARAM_Format) return;
    
    struct spa_video_info_raw format;
    if (spa_format_video_raw_parse(param, &format) < 0) return;
    
    std::cerr << "PipeWire: Format " << format.format << " size " 
              << format.size.width << "x" << format.size.height << "\n";
    
    // Reallocate buffer if size changed
    if ((int)format.size.width != capturer->width || (int)format.size.height != capturer->height) {
        capturer->width = format.size.width;
        capturer->height = format.size.height;
        capturer->frame_size = capturer->width * capturer->height * 4;
        
        delete[] capturer->frame_buffer;
        capturer->frame_buffer = new uint8_t[capturer->frame_size];
        memset(capturer->frame_buffer, 0, capturer->frame_size);
    }
}

// Stream process callback - called when new frame is available
void WaylandCapturer::onStreamProcess(void* data) {
    WaylandCapturer* capturer = (WaylandCapturer*)data;
    
    struct pw_buffer* buf = pw_stream_dequeue_buffer(capturer->stream);
    if (!buf) return;
    
    struct spa_buffer* spa_buf = buf->buffer;
    if (!spa_buf || !spa_buf->datas[0].data) {
        pw_stream_queue_buffer(capturer->stream, buf);
        return;
    }
    
    // Copy frame data
    uint8_t* src = (uint8_t*)spa_buf->datas[0].data;
    uint32_t src_stride = spa_buf->datas[0].chunk->stride;
    size_t copy_size = std::min((size_t)spa_buf->datas[0].chunk->size, capturer->frame_size);
    
    // Simple copy if stride matches
    if (src_stride == (uint32_t)(capturer->width * 4)) {
        memcpy(capturer->frame_buffer, src, copy_size);
    } else {
        // Copy line by line if stride differs
        for (int y = 0; y < capturer->height; y++) {
            memcpy(capturer->frame_buffer + y * capturer->width * 4,
                   src + y * src_stride,
                   capturer->width * 4);
        }
    }
    
    capturer->frame_ready = true;
    pw_stream_queue_buffer(capturer->stream, buf);
}

WaylandCapturer::WaylandCapturer()
    : loop(nullptr), context(nullptr), stream(nullptr),
      width(0), height(0), frame_buffer(nullptr), frame_size(0),
      frame_ready(false), stream_connected(false) {
    cursor_cache.hash = 0;
    cursor_cache.visible = false;
}

WaylandCapturer::~WaylandCapturer() {
    cleanup();
}

bool WaylandCapturer::init(int w, int h) {
    width = w;
    height = h;
    frame_size = width * height * 4;
    
    std::cerr << "Initializing PipeWire screen capture (" << width << "x" << height << ")...\n";
    
    // Initialize PipeWire
    pw_init(nullptr, nullptr);
    
    // Create thread loop
    loop = pw_thread_loop_new("mirrors-capture", nullptr);
    if (!loop) {
        std::cerr << "Failed to create PipeWire thread loop\n";
        return false;
    }
    
    // Create context
    context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    if (!context) {
        std::cerr << "Failed to create PipeWire context\n";
        return false;
    }
    
    // Allocate frame buffer
    frame_buffer = new uint8_t[frame_size];
    memset(frame_buffer, 0, frame_size);
    
    // Stream events
    static const struct pw_stream_events stream_events = {
        .version = PW_VERSION_STREAM_EVENTS,
        .state_changed = onStreamStateChanged,
        .param_changed = onStreamParamChanged,
        .process = onStreamProcess,
    };
    
    // Create stream
    stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop),
        "mirrors-screen-capture",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Video",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Screen",
            nullptr
        ),
        &stream_events,
        this
    );
    
    if (!stream) {
        std::cerr << "Failed to create PipeWire stream\n";
        return false;
    }
    
    // Build format parameters
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    
    // Use local variables for SPA macros (can't take address of temporaries)
    struct spa_rectangle min_size = SPA_RECTANGLE(1, 1);
    struct spa_rectangle max_size = SPA_RECTANGLE(8192, 8192);
    struct spa_rectangle def_size = SPA_RECTANGLE(width, height);
    struct spa_fraction min_fps = SPA_FRACTION(0, 1);
    struct spa_fraction max_fps = SPA_FRACTION(120, 1);
    struct spa_fraction def_fps = SPA_FRACTION(30, 1);
    
    const struct spa_pod* params[1];
    params[0] = (const struct spa_pod*)spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(3,
            SPA_VIDEO_FORMAT_BGRx,
            SPA_VIDEO_FORMAT_RGBx,
            SPA_VIDEO_FORMAT_BGRA),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&def_size, &min_size, &max_size),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&def_fps, &min_fps, &max_fps)
    );
    
    // Start thread loop
    if (pw_thread_loop_start(loop) < 0) {
        std::cerr << "Failed to start PipeWire thread loop\n";
        return false;
    }
    
    pw_thread_loop_lock(loop);
    
    // Connect stream
    int res = pw_stream_connect(stream,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        (enum pw_stream_flags)(
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS
        ),
        params, 1
    );
    
    pw_thread_loop_unlock(loop);
    
    if (res < 0) {
        std::cerr << "Failed to connect PipeWire stream (error " << res << ")\n";
        return false;
    }
    
    std::cerr << "PipeWire: Waiting for stream connection...\n";
    std::cerr << "Note: You may need to approve screen sharing via xdg-desktop-portal\n";
    
    return true;
}

bool WaylandCapturer::isDirty() {
    bool was_ready = frame_ready;
    if (frame_ready) frame_ready = false;
    return was_ready;
}

uint8_t* WaylandCapturer::captureFrame(bool force) {
    (void)force; // PipeWire is event-driven, force doesn't apply
    
    if (!stream_connected) {
        return nullptr; // Not yet connected
    }
    
    return frame_buffer;
}

WaylandCapturer::CursorData WaylandCapturer::getCursor() {
    // TODO: Extract cursor from PipeWire stream metadata
    // Most compositors don't provide cursor in the stream yet
    // Would need to parse stream metadata or use cursor-shape protocol
    
    CursorData data;
    data.visible = cursor_cache.visible;
    data.width = cursor_cache.width;
    data.height = cursor_cache.height;
    data.x = cursor_cache.x;
    data.y = cursor_cache.y;
    data.xhot = cursor_cache.xhot;
    data.yhot = cursor_cache.yhot;
    data.hash = cursor_cache.hash;
    data.pixels = cursor_cache.pixels;
    data.changed = false;
    
    return data;
}

void WaylandCapturer::cleanup() {
    if (loop) {
        pw_thread_loop_stop(loop);
    }
    
    if (stream) {
        pw_stream_destroy(stream);
        stream = nullptr;
    }
    
    if (context) {
        pw_context_destroy(context);
        context = nullptr;
    }
    
    if (loop) {
        pw_thread_loop_destroy(loop);
        loop = nullptr;
    }
    
    if (frame_buffer) {
        delete[] frame_buffer;
        frame_buffer = nullptr;
    }
    
    pw_deinit();
}
