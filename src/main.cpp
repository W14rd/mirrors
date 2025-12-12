// Backend selection: X11 or Wayland
#if defined(BUILD_WAYLAND_BACKEND)
    #include "wayland/capture.h"
    #include "wayland/input.h"
    using Capturer = WaylandCapturer;
    using InputHandler = WaylandInputHandler;
#else
    #include "x11/capture.h"
    #include "x11/input.h"
    using Capturer = X11Capturer;
    using InputHandler = InputHandler;
#endif

#include "renderer.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <limits.h>
#include <fcntl.h>
#include <filesystem>

// X11 includes (only for X11 backend)
#include <X11/Xlib.h>
#include <X11/Xutil.h>

// Global control
std::atomic<bool> running(true);
struct termios orig_termios;

// Child Process IDs
pid_t xvfb_pid = -1;
pid_t wm_pid = -1;
pid_t app_pid = -1;

void cleanupChildren() {
    if (app_pid > 0) kill(app_pid, SIGTERM);
    if (wm_pid > 0) kill(wm_pid, SIGTERM);
    if (xvfb_pid > 0) kill(xvfb_pid, SIGTERM);
}

void restoreTerminal() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    write(STDOUT_FILENO, "\033[?25h\033[?1006l\033[?1002l\033[?1000l\033[0m\033[?7h\n", 40);
    cleanupChildren();
}


void signalHandler(int sig) {
    // Only exit on SIGTERM and SIGQUIT
    // SIGINT (^C) and SIGTSTP (^Z) are handled via input forwarding
    if (sig == SIGTERM || sig == SIGQUIT) {
        running = false;
    }
}

int MyXErrorHandler(Display* d, XErrorEvent* e) {
    char error_text[256];
    XGetErrorText(d, e->error_code, error_text, sizeof(error_text));
    // Suppress non-critical errors during startup
    // fprintf(stderr, "X Error: %s (code: %d)\n", error_text, e->error_code);
    return 0;
}

void setupTerminal() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(restoreTerminal);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);  // Disable signals from terminal
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    write(STDOUT_FILENO, "\033[?25l\033[?1006h\033[?1002h\033[?1000h\033[?7l", 35);
}

std::string getSelfPath() {
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count != -1) {
        return std::filesystem::path(std::string(result, count)).parent_path().string();
    }
    return "";
}

bool commandExists(const std::string& cmd) {
    std::string check = "command -v " + cmd + " >/dev/null 2>&1";
    return (system(check.c_str()) == 0);
}

int findFreeDisplay() {
    int disp = 99;
    while (true) {
        std::string lockfile = "/tmp/.X" + std::to_string(disp) + "-lock";
        if (access(lockfile.c_str(), F_OK) != 0) {
            return disp;
        }
        disp++;
        if (disp > 1000) return -1;
    }
}

// Recursive window finder
Window findAppWindow(Display* d, Window current_w) {
    Window root, parent, *children;
    unsigned int nchildren;
    
    if (XQueryTree(d, current_w, &root, &parent, &children, &nchildren) == 0) {
        return 0;
    }

    Window found = 0;
    for (unsigned int i = 0; i < nchildren; i++) {
        XWindowAttributes attrs;
        if (XGetWindowAttributes(d, children[i], &attrs)) {
            // Find a visible window that is reasonably large (ignoring helper windows)
            if (attrs.map_state == IsViewable && attrs.width > 50 && attrs.height > 50) {
                found = children[i];
                break;
            }
        }
        if (!found) {
            found = findAppWindow(d, children[i]);
            if (found) break;
        }
    }

    if (children) XFree(children);
    return found;
}

void captureThread(Capturer* capturer, ANSIRenderer* renderer,
                   std::atomic<bool>& running, int fps, bool isCursor) {
    auto frame_time = std::chrono::milliseconds(1000 / fps);
    int frame_count = 0;
    
    while (running) {
        auto start = std::chrono::steady_clock::now();
        frame_count++;
        
        if (isCursor) {
            auto cursor = capturer->getCursor();
            renderer->setCursor(cursor);
        }

        bool force = (frame_count < 10) || (frame_count % 60 == 0);
        uint8_t* pixels = capturer->captureFrame(force);
        
        if (pixels) {
            int bytes_per_line = capturer->getBytesPerLine();
            renderer->renderFrame(pixels, capturer->getWidth(), capturer->getHeight(), 4, bytes_per_line);
            
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

void show_help(const char* prog) {
    std::cout << "Usage: " << prog << " [options] <executable> [its args...]\n"
              << "Options:\n"
              << "  -r, --refresh-rate <fps>   Set target FPS (default: 30)\n"
              << "  -w, --width <pixels>       Set virtual screen width\n"
              << "  -h, --height <pixels>      Set virtual screen height\n"
              << "  --cell <char>              Use character for rendering\n"
              << "  --ansi                     Enable standard ANSI colors\n"
              << "  --rgb                      Enable TrueColor (default)\n"
              << "  --grey                     Enable Grayscale\n"
              << "  --cursor                   Show cursor\n";
}

int main(int argc, char** argv) {
    int fps = 30;
    int width = 1920;
    int height = 1080;
    char cell_char = 0;
    RenderMode mode = RenderMode::TRUECOLOR;
    bool isCursor = false;
    std::string bin_path;
    std::vector<std::string> bin_args;

    // 1. Argument Parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-r" || arg == "--refresh-rate") {
            if (i + 1 < argc) fps = std::stoi(argv[++i]);
        } else if (arg == "-w" || arg == "--width") {
            if (i + 1 < argc) width = std::stoi(argv[++i]);
        } else if (arg == "-h" || arg == "--height") {
            if (i + 1 < argc) height = std::stoi(argv[++i]);
        } else if (arg == "--cell") {
            if (i + 1 < argc) cell_char = argv[++i][0];
        } else if (arg == "--ansi") {
            mode = RenderMode::ANSI256;
        } else if (arg == "--grey" || arg == "--gray") {
            mode = RenderMode::GRAYSCALE;
        } else if (arg == "--cursor") {
            isCursor = true;
        } else if (arg == "--rgb") {
            mode = RenderMode::TRUECOLOR;
        } else if (arg == "--help") {
            show_help(argv[0]);
            return 0;
        } else {
            if (bin_path.empty()) bin_path = arg;
            else bin_args.push_back(arg);
        }
    }

    if (bin_path.empty()) {
        std::cerr << "Error: No command provided.\n";
        show_help(argv[0]);
        return 1;
    }

    // 2. Environment & Xvfb Setup
    if (!commandExists("Xvfb")) { std::cerr << "Error: Xvfb not found.\n"; return 1; }

    std::string script_dir = getSelfPath();
    std::string wm_binary = script_dir + "/mirrors-wm";
    if (access(wm_binary.c_str(), X_OK) != 0) wm_binary = script_dir + "/../build/mirrors-wm";

    int display_num = findFreeDisplay();
    std::string display_str = ":" + std::to_string(display_num);
    setenv("DISPLAY", display_str.c_str(), 1);

    std::cout << "Starting Display " << display_str << " (" << width << "x" << height << ")...\n";

    xvfb_pid = fork();
    if (xvfb_pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, 2); dup2(devnull, 1); close(devnull);
        std::string res = std::to_string(width) + "x" + std::to_string(height) + "x24";
        execlp("Xvfb", "Xvfb", display_str.c_str(), "-screen", "0", res.c_str(), "+extension", "RANDR", NULL);
        exit(1);
    }

    Display* display = nullptr;
    for (int i = 0; i < 50; ++i) {
        display = XOpenDisplay(display_str.c_str());
        if (display) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!display) {
        std::cerr << "Error: Xvfb failed.\n"; cleanupChildren(); return 1;
    }
    XSetErrorHandler(MyXErrorHandler);

    // 3. Start WM
    if (access(wm_binary.c_str(), X_OK) == 0) {
        wm_pid = fork();
        if (wm_pid == 0) {
            execl(wm_binary.c_str(), wm_binary.c_str(), NULL);
            exit(1);
        }
    } else {
        std::cerr << "Warning: mirrors-wm not found, apps might not maximize.\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 4. Start Application
    app_pid = fork();
    if (app_pid == 0) {
        // Suppress application logs
        int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
        
        std::vector<char*> args;
        if (commandExists("dbus-run-session")) args.push_back(strdup("dbus-run-session"));
        else if (commandExists("dbus-launch")) {
            args.push_back(strdup("dbus-launch")); args.push_back(strdup("--exit-with-session"));
        }
        args.push_back(strdup(bin_path.c_str()));
        for (const auto& a : bin_args) args.push_back(strdup(a.c_str()));
        args.push_back(NULL);
        execvp(args[0], args.data());
        exit(1);
    }
    
#if !defined(BUILD_WAYLAND_BACKEND)
    // X11-specific: Setup window geometry
    std::cout << "Waiting for window...\n";
    Window app_window = 0;
    int wait_counter = 0;
    while (wait_counter < 100 && running) {
        app_window = findAppWindow(display, DefaultRootWindow(display));
        if (app_window != 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_counter++;
    }

    if (app_window != 0) {
        XMoveResizeWindow(display, app_window, 0, 0, width, height);
        XMapWindow(display, app_window);
        XFlush(display);
    } else {
        std::cerr << "Warning: No visible app window found (capturing background).\n";
    }

    Window root_window = DefaultRootWindow(display);
#else
    // Wayland: No window manipulation needed
    std::cout << "Wayland mode: waiting for application...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif

    struct winsize ts;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ts);
    int term_cols = ts.ws_col;
    int term_lines = ts.ws_row;

    // 6. Initialize Engine
    Capturer capturer;
    ANSIRenderer renderer;
    InputHandler input;
    
#if !defined(BUILD_WAYLAND_BACKEND)
    // X11: Close display so capturer can open its own
    XCloseDisplay(display);

    if (!capturer.init(display_str.c_str(), root_window, width, height)) {
        std::cerr << "Failed to initialize capturer\n";
        cleanupChildren();
        return 1;
    }
    
    if (!input.init(display_str.c_str(), root_window, width, height, term_cols, term_lines)) {
        std::cerr << "Warning: Failed to initialize input handler\n";
    }
#else
    // Wayland: Simplified init
    if (!capturer.init(width, height)) {
        std::cerr << "Failed to initialize Wayland capturer\n";
        cleanupChildren();
        return 1;
    }
    
    if (!input.init(width, height, term_cols, term_lines)) {
        std::cerr << "Warning: Failed to initialize Wayland input handler\n";
    }
#endif
    
    renderer.setDimensions(term_cols, term_lines);
    renderer.setImageSize(width, height);
    if (cell_char != 0) renderer.setCellChar(cell_char);
    renderer.setMode(mode);
    
    input.setRenderer(&renderer);
    input.setShellPid(app_pid);

    setupTerminal();
    
    // Ignore SIGINT and SIGTSTP - they'll be forwarded to the app via input handler
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    
    // Handle SIGTERM and SIGQUIT for graceful shutdown
    signal(SIGTERM, signalHandler);
    signal(SIGQUIT, signalHandler);
    
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
    
    std::thread capture_thread(captureThread, &capturer, &renderer, std::ref(running), fps, isCursor);
    std::thread input_thread_obj(inputThread, &input, std::ref(running));
    
    capture_thread.join();
    input_thread_obj.join();
    
    capturer.cleanup();
    input.cleanup();
    cleanupChildren();
    
    return 0;
}