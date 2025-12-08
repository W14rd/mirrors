#include "capture.h"
#include <cstring>
#include <iostream>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/Xutil.h>

X11Capturer::X11Capturer() 
    : display(nullptr), window(0), ximage(nullptr), 
      width(0), height(0), using_shm(false),
      damage(0), damage_event_base(0), damage_error_base(0), damage_available(false),
      xfixes_event_base(0), xfixes_error_base(0),
      frame_dirty(true) {
    memset(&shminfo, 0, sizeof(shminfo));
    shminfo.shmid = -1;
    cursor_cache.hash = 0;
}

X11Capturer::~X11Capturer() {
    cleanup();
}

bool X11Capturer::initDamage() {
    if (!XDamageQueryExtension(display, &damage_event_base, &damage_error_base)) {
        std::cerr << "X DAMAGE extension not available\n";
        return false;
    }
    
    // Create damage object for the window
    // DamageReportNonEmpty: only get notified when damage transitions from empty to non-empty
    // This is more efficient than DamageReportRawRectangles
    damage = XDamageCreate(display, window, XDamageReportNonEmpty);
    if (!damage) {
        std::cerr << "Failed to create damage object\n";
        return false;
    }
    
    damage_available = true;
    return true;
}

bool X11Capturer::initXFixes() {
    int major = 4, minor = 0; // We need at least 4.0 for cursor
    if (!XFixesQueryVersion(display, &major, &minor)) {
        std::cerr << "XFixes extension not available\n";
        return false;
    }
    
    if (!XQueryExtension(display, "XFIXES", &xfixes_event_base, 
                         &xfixes_event_base, &xfixes_error_base)) {
        return false;
    }
    
    // Select cursor notify events
    XFixesSelectCursorInput(display, window, XFixesDisplayCursorNotifyMask);
    
    return true;
}

bool X11Capturer::init(const char* display_name, Window target_window, int w, int h) {
    cleanup();
    
    display = XOpenDisplay(display_name);
    if (!display) {
        std::cerr << "Failed to open display\n";
        return false;
    }
    
    window = target_window;
    width = w;
    height = h;
    frame_dirty = true;
    
    // Initialize extensions
    initXFixes();
    
    // Try XShm for zero-copy capture
    if (XShmQueryExtension(display)) {
        Visual* visual = DefaultVisual(display, DefaultScreen(display));
        int depth = DefaultDepth(display, DefaultScreen(display));
        
        ximage = XShmCreateImage(display, visual, depth, ZPixmap, 
                                 nullptr, &shminfo, width, height);
        
        if (ximage) {
            size_t size = ximage->bytes_per_line * ximage->height;
            
            // Use IPC_PRIVATE for security, 0600 permissions
            shminfo.shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
            
            if (shminfo.shmid != -1) {
                shminfo.shmaddr = (char*)shmat(shminfo.shmid, nullptr, 0);
                
                if (shminfo.shmaddr != (char*)-1) {
                    ximage->data = shminfo.shmaddr;
                    shminfo.readOnly = False;
                    
                    // Mark for immediate destruction - auto cleanup on crash
                    shmctl(shminfo.shmid, IPC_RMID, nullptr);
                    
                    if (XShmAttach(display, &shminfo)) {
                        XSync(display, False);
                        using_shm = true;
                        std::cout << "XShm initialized: " << width << "x" << height 
                                  << ", " << ximage->bytes_per_line << " bytes/line, depth=" 
                                  << ximage->depth << ", bpp=" << ximage->bits_per_pixel << "\n";
                        std::cout << "Red mask: 0x" << std::hex << ximage->red_mask 
                                  << ", Green: 0x" << ximage->green_mask 
                                  << ", Blue: 0x" << ximage->blue_mask << std::dec << "\n";
                    } else {
                        std::cerr << "XShmAttach failed\n";
                        shmdt(shminfo.shmaddr);
                        shminfo.shmaddr = nullptr;
                    }
                } else {
                    std::cerr << "shmat failed\n";
                }
            } else {
                std::cerr << "shmget failed\n";
            }
            
            if (!using_shm) {
                XDestroyImage(ximage);
                ximage = nullptr;
            }
        }
    } else {
        std::cerr << "XShm extension not available\n";
    }
    
    // Initialize damage tracking after SHM setup
    // IMPORTANT: Only enable damage if XShm worked
    if (using_shm) {
        if (!initDamage()) {
            std::cerr << "Damage extension not available - will capture every frame\n";
        }
    }
    
    return using_shm || (display != nullptr);
}

void X11Capturer::processEvents() {
    if (!display) return;
    
    // Process all pending events without blocking
    while (XPending(display) > 0) {
        XEvent event;
        XNextEvent(display, &event);
        
        if (damage_available && event.type == damage_event_base + XDamageNotify) {
            frame_dirty = true;
        }
    }
}

bool X11Capturer::isDirty() {
    processEvents();
    return frame_dirty;
}

void X11Capturer::clearDamage() {
    if (damage_available && damage) {
        // Subtract all damage, resetting the damage region
        XDamageSubtract(display, damage, None, None);
    }
    frame_dirty = false;
}

uint8_t* X11Capturer::captureFrame(bool force) {
    if (!display || !window) return nullptr;
    
    // CRITICAL FIX: Always capture on first call or force
    static bool first_frame = true;
    if (first_frame) {
        force = true;
        first_frame = false;
    }
    
    // Check if we need to capture
    if (!force && damage_available) {
        processEvents();
        if (!frame_dirty) {
            // Return cached data if available
            return (using_shm && ximage) ? (uint8_t*)ximage->data : nullptr;
        }
    }
    
    if (using_shm && ximage) {
        // Zero-copy capture using XShm
        if (XShmGetImage(display, window, ximage, 0, 0, AllPlanes)) {
            if (damage_available) clearDamage();
            return (uint8_t*)ximage->data;
        } else {
            std::cerr << "XShmGetImage failed\n";
            return nullptr;
        }
    } else {
        // Fallback to regular XGetImage
        XImage* img = XGetImage(display, window, 0, 0, width, height, 
                                AllPlanes, ZPixmap);
        if (img) {
            static std::vector<uint8_t> fallback_buffer;
            size_t size = img->bytes_per_line * height;
            
            fallback_buffer.assign((uint8_t*)img->data, 
                                   (uint8_t*)img->data + size);
            
            XDestroyImage(img);
            if (damage_available) clearDamage();
            return fallback_buffer.data();
        }
    }
    
    return nullptr;
}

X11Capturer::CursorData X11Capturer::getCursor() {
    CursorData data;
    data.visible = false;
    data.changed = false;
    
    if (!display) return data;

    XFixesCursorImage* img = XFixesGetCursorImage(display);
    if (!img) return data;

    // Quick hash check to see if cursor changed
    uint64_t hash = 5381;
    for (int i = 0; i < img->width * img->height; ++i) {
        hash = ((hash << 5) + hash) + (uint32_t)img->pixels[i];
    }
    
    // If cursor hasn't changed, return cached data with updated position
    if (hash == cursor_cache.hash && 
        cursor_cache.width == img->width && 
        cursor_cache.height == img->height) {
        
        data = {
            cursor_cache.pixels,
            cursor_cache.width,
            cursor_cache.height,
            0, 0, // Will update below
            cursor_cache.xhot,
            cursor_cache.yhot,
            true,
            cursor_cache.name,
            hash,
            false // Not changed
        };
        
        // Update position only
        int dest_x, dest_y;
        Window child;
        XTranslateCoordinates(display, DefaultRootWindow(display), window, 
                              img->x, img->y, &dest_x, &dest_y, &child);
        data.x = dest_x;
        data.y = dest_y;
        
        XFree(img);
        return data;
    }
    
    // Cursor changed - update cache
    data.visible = true;
    data.changed = true;
    data.width = img->width;
    data.height = img->height;
    data.xhot = img->xhot;
    data.yhot = img->yhot;
    data.hash = hash;
    
    // Convert root coordinates to window coordinates
    int dest_x, dest_y;
    Window child;
    XTranslateCoordinates(display, DefaultRootWindow(display), window, 
                          img->x, img->y, &dest_x, &dest_y, &child);
    data.x = dest_x;
    data.y = dest_y;

    // Get cursor name if available
    if (img->atom) {
        char* name = XGetAtomName(display, img->atom);
        if (name) {
            data.name = name;
            XFree(name);
        }
    }

    // Copy pixels
    data.pixels.resize(data.width * data.height);
    for (int i = 0; i < data.width * data.height; ++i) {
        data.pixels[i] = (uint32_t)img->pixels[i];
    }

    // Update cache
    cursor_cache = {
        hash,
        data.pixels,
        data.width,
        data.height,
        data.xhot,
        data.yhot,
        data.name
    };

    XFree(img);
    return data;
}

void X11Capturer::cleanup() {
    // Destroy damage object first
    if (damage_available && damage) {
        XDamageDestroy(display, damage);
        damage = 0;
        damage_available = false;
    }
    
    // Cleanup XShm
    if (using_shm && ximage) {
        XShmDetach(display, &shminfo);
        
        if (shminfo.shmaddr && shminfo.shmaddr != (char*)-1) {
            shmdt(shminfo.shmaddr);
            shminfo.shmaddr = nullptr;
        }
        
        // Prevent XDestroyImage from trying to free SHM memory
        ximage->data = nullptr;
        XDestroyImage(ximage);
        ximage = nullptr;
        using_shm = false;
    }
    
    if (display) {
        XCloseDisplay(display);
        display = nullptr;
    }
    
    window = 0;
    frame_dirty = true;
    cursor_cache.hash = 0;
}