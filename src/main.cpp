#include "capture.h"
#include "renderer.h"
#include "input.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <csignal>
#include <cstring>
#include <cstdlib>

std::atomic<bool> running(true);
struct termios orig_termios;

void restoreTerminal() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    write(STDOUT_FILENO, "\033[?25h\033[?1006l\033[?1002l\033[?1000l\033[0m\033[?7h\n", 40);
}

void signalHandler(int sig) {
    (void)sig;
    running = false;
}

int MyXErrorHandler(Display* d, XErrorEvent* e) {
    char error_text[256];
    XGetErrorText(d, e->error_code, error_text, sizeof(error_text));
    fprintf(stderr, "X Error: %s (code: %d)\n", error_text, e->error_code);
    return 0;
}

void setupTerminal() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(restoreTerminal);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    write(STDOUT_FILENO, "\033[?25l\033[?1006h\033[?1002h\033[?1000h\033[?7l", 35);
}

void captureThread(X11Capturer* capturer, ANSIRenderer* renderer,
                   std::atomic<bool>& running, int fps) {
    auto frame_time = std::chrono::milliseconds(1000 / fps);
    
    std::string last_cursor_name = "";
    uint64_t last_cursor_hash = 0;
    
    int frame_count = 0;
    int captured_count = 0;
    auto stats_start = std::chrono::steady_clock::now();

    while (running) {
        auto start = std::chrono::steady_clock::now();
        
        frame_count++;
        
        // Capture cursor
        X11Capturer::CursorData cursor = capturer->getCursor();
        renderer->setCursor(cursor); // Enable software rendering
        
        // Handle System Cursor Shape
        if (cursor.visible && cursor.hash != last_cursor_hash) {
            std::string shape = "default";
            if (cursor.name == "xterm" || cursor.name == "text" || cursor.name == "ibeam") 
                shape = "text";
            else if (cursor.name == "hand1" || cursor.name == "hand2" || 
                     cursor.name == "pointer" || cursor.name == "hand") 
                shape = "pointer";
            else if (cursor.name == "crosshair" || cursor.name == "cross") 
                shape = "crosshair";
            else if (cursor.name == "left_ptr" || cursor.name == "default") 
                shape = "default";
            else if (cursor.name == "sb_h_double_arrow" || cursor.name == "h_double_arrow") 
                shape = "ew-resize";
            else if (cursor.name == "sb_v_double_arrow" || cursor.name == "v_double_arrow") 
                shape = "ns-resize";
            
            std::string seq = "\033]22;" + shape + "\007";
            write(STDOUT_FILENO, seq.c_str(), seq.length());
            last_cursor_name = cursor.name;
            last_cursor_hash = cursor.hash;
        }

        // CRITICAL FIX: Always force capture on first few frames
        // and periodically to handle damage tracking edge cases
        bool force = (frame_count < 5) || (frame_count % 60 == 0);
        
        uint8_t* pixels = capturer->captureFrame(force);
        
        if (pixels) {
            captured_count++;
            
            // Get actual bytes per line from capturer
            int bytes_per_line = capturer->getBytesPerLine();
            
            renderer->renderFrame(pixels, capturer->getWidth(), capturer->getHeight(), 
                                 4, bytes_per_line);
            
            const char* data = renderer->getData();
            size_t remaining = renderer->getSize();
            ssize_t total_written = 0;
            
            while (remaining > 0 && running) {
                ssize_t n = write(STDOUT_FILENO, data + total_written, remaining);
                if (n == -1) {
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                        continue;
                    }
                    break;
                }
                total_written += n;
                remaining -= n;
            }
            
            // Debug stats every 5 seconds
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<double>(now - stats_start).count();
            if (elapsed >= 5.0) {
                double check_fps = frame_count / elapsed;
                double capture_fps = captured_count / elapsed;
                double efficiency = (captured_count * 100.0) / frame_count;
                
                char stats[256];
                int len = snprintf(stats, sizeof(stats),
                    "\033[H\033[2KStats: Check=%.1f fps, Capture=%.1f fps, Efficiency=%.1f%%",
                    check_fps, capture_fps, efficiency);
                write(STDERR_FILENO, stats, len);
                
                frame_count = 0;
                captured_count = 0;
                stats_start = now;
            }
        } else if (!force) {
            // No damage, no capture needed - this is good!
            // Just continue to next frame
        } else {
            // Forced capture failed - this is bad
            char error_msg[128];
            int len = snprintf(error_msg, sizeof(error_msg),
                "\033[H\033[2KERROR: Forced capture failed on frame %d\n", frame_count);
            write(STDERR_FILENO, error_msg, len);
            
            if (frame_count < 10) {
                // Critical failure in first frames
                running = false;
                break;
            }
        }
        
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed < frame_time) {
            std::this_thread::sleep_for(frame_time - elapsed);
        }
    }
}

void inputThread(InputHandler* input, std::atomic<bool>& running) {
    while (running) {
        input->processInput();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main(int argc, char** argv) {
    XSetErrorHandler(MyXErrorHandler);
    
    if (argc < 8) {
        std::cerr << "Usage: " << argv[0] 
                  << " <display> <win_id> <fps> <cols> <lines> <w> <h> [flags]\n"
                  << "Flags:\n"
                  << "  --cell <char>  Set fill character\n"
                  << "  --ansi          Use default ANSI colors\n"
                  << "  --grey/--gray  Use grayscale mode\n";
        return 1;
    }
    
    const char* display_name = argv[1];
    Window window_id = std::stoul(argv[2], nullptr, 0);
    int fps = std::stoi(argv[3]);
    int term_cols = std::stoi(argv[4]);
    int term_lines = std::stoi(argv[5]);
    int window_width = std::stoi(argv[6]);
    int window_height = std::stoi(argv[7]);
    
    char cell_char = 0;
    RenderMode mode = RenderMode::TRUECOLOR;

    for (int i = 8; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cell" && i + 1 < argc) {
            cell_char = argv[++i][0];
        } else if (arg == "--ansi") {
            mode = RenderMode::ANSI256;
        } else if (arg == "--grey" || arg == "--gray") {
            mode = RenderMode::GRAYSCALE;
        }
    }
    
    std::cerr << "Initializing with:\n"
              << "  Display: " << (display_name[0] ? display_name : ":0") << "\n"
              << "  Window: 0x" << std::hex << window_id << std::dec << "\n"
              << "  Size: " << window_width << "x" << window_height << "\n"
              << "  Terminal: " << term_cols << "x" << term_lines << "\n"
              << "  FPS: " << fps << "\n";
    
    X11Capturer capturer;
    ANSIRenderer renderer;
    InputHandler input;
    
    if (!capturer.init(display_name, window_id, window_width, window_height)) {
        std::cerr << "Failed to initialize capturer\n";
        return 1;
    }
    
    renderer.setDimensions(term_cols, term_lines);
    renderer.setImageSize(window_width, window_height);
    if (cell_char != 0) renderer.setCellChar(cell_char);
    renderer.setMode(mode);
    
    if (!input.init(display_name, window_id, window_width, window_height, term_cols, term_lines)) {
        std::cerr << "Warning: Failed to initialize input handler\n";
    }
    input.setRenderer(&renderer);
    
    setupTerminal();
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
    
    std::cerr << "Starting capture threads...\n";
    
    std::thread capture_thread(captureThread, &capturer, &renderer, std::ref(running), fps);
    std::thread input_thread_obj(inputThread, &input, std::ref(running));
    
    capture_thread.join();
    input_thread_obj.join();
    
    std::cerr << "\nCleaning up...\n";
    
    capturer.cleanup();
    input.cleanup();
    
    return 0;
}
