#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

class TerminalDetector {
private:
    Display* display;
    
    std::string getProperty(Window win, Atom property) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* prop = nullptr;
        
        int status = XGetWindowProperty(
            display, win, property,
            0, 1024, False, AnyPropertyType,
            &actual_type, &actual_format,
            &nitems, &bytes_after, &prop
        );
        
        std::string result;
        if (status == Success && prop) {
            if (actual_type == XA_STRING || actual_format == 8) {
                result = std::string(reinterpret_cast<char*>(prop));
            }
            XFree(prop);
        }
        
        return result;
    }
    
    Window findParentWindow(Window start) {
        Window root, parent;
        Window* children = nullptr;
        unsigned int nchildren;
        
        Window current = start;
        Window result = start;
        
        // Walk up the window tree
        while (current != 0) {
            result = current;
            
            Status status = XQueryTree(display, current, &root, &parent, &children, &nchildren);
            
            if (children) {
                XFree(children);
            }
            
            if (!status || parent == root || parent == 0) {
                break;
            }
            
            current = parent;
        }
        
        return result;
    }
    
    void printWindowInfo(Window win, const std::string& label) {
        std::cout << "\n" << label << ":" << std::endl;
        std::cout << "  Window ID: 0x" << std::hex << win << std::dec << std::endl;
        
        // Get window name/title
        Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
        Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
        std::string name = getProperty(win, net_wm_name);
        
        if (name.empty()) {
            char* window_name = nullptr;
            if (XFetchName(display, win, &window_name) && window_name) {
                name = window_name;
                XFree(window_name);
            }
        }
        
        if (!name.empty()) {
            std::cout << "  Window Title: " << name << std::endl;
        }
        
        // Get WM_CLASS (this often contains the application name)
        XClassHint class_hint;
        if (XGetClassHint(display, win, &class_hint)) {
            if (class_hint.res_name) {
                std::cout << "  WM_CLASS (instance): " << class_hint.res_name << std::endl;
                XFree(class_hint.res_name);
            }
            if (class_hint.res_class) {
                std::cout << "  WM_CLASS (class): " << class_hint.res_class << std::endl;
                XFree(class_hint.res_class);
            }
        }
        
        // Get WM_CLIENT_MACHINE
        std::string machine = getProperty(win, XA_WM_CLIENT_MACHINE);
        if (!machine.empty()) {
            std::cout << "  Client Machine: " << machine << std::endl;
        }
        
        // Get PID if available
        Atom net_wm_pid = XInternAtom(display, "_NET_WM_PID", False);
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* prop = nullptr;
        
        if (XGetWindowProperty(display, win, net_wm_pid, 0, 1, False, XA_CARDINAL,
                              &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
            if (actual_format == 32 && nitems > 0) {
                long pid = *reinterpret_cast<long*>(prop);
                std::cout << "  PID: " << pid << std::endl;
            }
            XFree(prop);
        }
        
        // Get window attributes
        XWindowAttributes attrs;
        if (XGetWindowAttributes(display, win, &attrs)) {
            std::cout << "  Size: " << attrs.width << "x" << attrs.height << std::endl;
            std::cout << "  Position: (" << attrs.x << ", " << attrs.y << ")" << std::endl;
        }
    }
    
    std::string identifyTerminal(const std::string& wm_class, const std::string& title) {
        std::string lower_class = wm_class;
        std::string lower_title = title;
        
        for (char& c : lower_class) c = tolower(c);
        for (char& c : lower_title) c = tolower(c);
        
        if (lower_class.find("gnome-terminal") != std::string::npos ||
            lower_class.find("gnome terminal") != std::string::npos) {
            return "GNOME Terminal";
        }
        if (lower_class.find("konsole") != std::string::npos) {
            return "Konsole (KDE)";
        }
        if (lower_class.find("xterm") != std::string::npos) {
            return "XTerm";
        }
        if (lower_class.find("alacritty") != std::string::npos) {
            return "Alacritty";
        }
        if (lower_class.find("kitty") != std::string::npos) {
            return "Kitty";
        }
        if (lower_class.find("wezterm") != std::string::npos) {
            return "WezTerm";
        }
        if (lower_class.find("terminator") != std::string::npos) {
            return "Terminator";
        }
        if (lower_class.find("tilix") != std::string::npos) {
            return "Tilix";
        }
        if (lower_class.find("urxvt") != std::string::npos || lower_class.find("rxvt") != std::string::npos) {
            return "rxvt-unicode (urxvt)";
        }
        if (lower_class.find("st") != std::string::npos && lower_class.length() <= 3) {
            return "Simple Terminal (st)";
        }
        if (lower_class.find("foot") != std::string::npos) {
            return "Foot";
        }
        if (lower_class.find("terminology") != std::string::npos) {
            return "Terminology";
        }
        if (lower_class.find("xfce4-terminal") != std::string::npos) {
            return "XFCE Terminal";
        }
        if (lower_class.find("lxterminal") != std::string::npos) {
            return "LXTerminal";
        }
        if (lower_class.find("mate-terminal") != std::string::npos) {
            return "MATE Terminal";
        }
        if (lower_class.find("qterminal") != std::string::npos) {
            return "QTerminal";
        }
        if (lower_class.find("hyper") != std::string::npos) {
            return "Hyper";
        }
        
        return "Unknown X11 Terminal";
    }

public:
    TerminalDetector() : display(nullptr) {
        display = XOpenDisplay(nullptr);
        if (!display) {
            std::cerr << "Error: Cannot open X11 display" << std::endl;
            std::cerr << "Make sure DISPLAY environment variable is set" << std::endl;
            exit(1);
        }
    }
    
    ~TerminalDetector() {
        if (display) {
            XCloseDisplay(display);
        }
    }
    
    void detect() {
        std::cout << "=== X11 Terminal Emulator Detection ===" << std::endl;
        std::cout << "\nDisplay: " << XDisplayString(display) << std::endl;
        
        // Get the window ID from WINDOWID environment variable (set by most terminals)
        const char* windowid_str = getenv("WINDOWID");
        
        if (!windowid_str) {
            std::cout << "\nWARNING: WINDOWID environment variable not set" << std::endl;
            std::cout << "This terminal may not expose its window ID" << std::endl;
            
            // Try to get the active window as fallback
            Atom net_active = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
            Window root = DefaultRootWindow(display);
            
            Atom actual_type;
            int actual_format;
            unsigned long nitems, bytes_after;
            unsigned char* prop = nullptr;
            
            if (XGetWindowProperty(display, root, net_active, 0, 1, False, XA_WINDOW,
                                  &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
                Window active_window = *reinterpret_cast<Window*>(prop);
                XFree(prop);
                
                std::cout << "\nTrying active window instead..." << std::endl;
                printWindowInfo(active_window, "Active Window");
            }
            return;
        }
        
        Window window = strtol(windowid_str, nullptr, 0);
        std::cout << "WINDOWID from environment: 0x" << std::hex << window << std::dec << std::endl;
        
        // Get info about the terminal window
        printWindowInfo(window, "Terminal Window");
        
        // Try to find the top-level parent window
        Window parent = findParentWindow(window);
        if (parent != window) {
            printWindowInfo(parent, "Top-level Window");
        }
        
        // Get WM_CLASS to identify terminal
        XClassHint class_hint;
        std::string wm_class;
        std::string window_title;
        
        if (XGetClassHint(display, window, &class_hint)) {
            if (class_hint.res_class) {
                wm_class = class_hint.res_class;
                XFree(class_hint.res_class);
            }
            if (class_hint.res_name) {
                XFree(class_hint.res_name);
            }
        }
        
        char* name = nullptr;
        if (XFetchName(display, window, &name) && name) {
            window_title = name;
            XFree(name);
        }
        
        std::string terminal_type = identifyTerminal(wm_class, window_title);
        
        std::cout << "\n=== DETECTED TERMINAL ===" << std::endl;
        std::cout << "Terminal Emulator: " << terminal_type << std::endl;
        
        // Additional environment info
        std::cout << "\n=== Environment Variables ===" << std::endl;
        const char* term = getenv("TERM");
        const char* colorterm = getenv("COLORTERM");
        const char* term_program = getenv("TERM_PROGRAM");
        
        if (term) std::cout << "TERM: " << term << std::endl;
        if (colorterm) std::cout << "COLORTERM: " << colorterm << std::endl;
        if (term_program) std::cout << "TERM_PROGRAM: " << term_program << std::endl;
    }
};

int main() {
    try {
        TerminalDetector detector;
        detector.detect();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
