#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <string>
#include <iostream>

// Constants for styling
const int TITLE_BAR_HEIGHT = 24;
const int BORDER_WIDTH = 2; 
const int HANDLE_SIZE = 5; 
const unsigned long COLOR_TITLE_BG = 0x333333; 
const unsigned long COLOR_TITLE_TEXT = 0xFFFFFF; 
const unsigned long COLOR_BORDER = 0x000000; 
const unsigned long COLOR_BTN_CLOSE = 0xFF5555; 
const unsigned long COLOR_BTN_FULL = 0x55FF55; 

struct Client {
    Window window;
    Window frame;
    Window title_bar;
    Window close_btn;
    Window full_btn;
    
    Window resize_l, resize_r, resize_t, resize_b;
    Window resize_bl, resize_br, resize_tl, resize_tr; 
    
    int x, y, w, h;
    bool fullscreen;
    int saved_x, saved_y, saved_w, saved_h;
};

Display* dpy;
Window root;
std::vector<Client*> clients;

Atom wm_protocols;
Atom wm_delete_window;

int OnXError(Display* d, XErrorEvent* e) {
    (void)d; (void)e;
    return 0;
}

Client* find_client(Window w) {
    for (auto c : clients) {
        if (c->window == w || c->frame == w || c->title_bar == w || 
            c->close_btn == w || c->full_btn == w ||
            c->resize_l == w || c->resize_r == w || c->resize_t == w || c->resize_b == w ||
            c->resize_bl == w || c->resize_br == w || c->resize_tl == w || c->resize_tr == w)
            return c;
    }
    return nullptr;
}

void send_configure_notify(Client* c) {
    XConfigureEvent ce;
    ce.type = ConfigureNotify;
    ce.display = dpy;
    ce.event = c->window;
    ce.window = c->window;
    ce.x = c->x;
    ce.y = c->y + TITLE_BAR_HEIGHT;
    ce.width = c->w;
    ce.height = c->h;
    ce.border_width = 0;
    ce.above = None;
    ce.override_redirect = False;
    XSendEvent(dpy, c->window, False, StructureNotifyMask, (XEvent*)&ce);
}

void update_frame_extents(Client* c) {
    int w = c->w;
    int h = c->h + TITLE_BAR_HEIGHT;
    int hs = HANDLE_SIZE;

    XMoveResizeWindow(dpy, c->resize_l, 0, hs, hs, h - 2*hs);
    XMoveResizeWindow(dpy, c->resize_r, w - hs, hs, hs, h - 2*hs);
    XMoveResizeWindow(dpy, c->resize_t, hs, 0, w - 2*hs, hs);
    XMoveResizeWindow(dpy, c->resize_b, hs, h - hs, w - 2*hs, hs);
    
    XMoveResizeWindow(dpy, c->resize_tl, 0, 0, hs, hs);
    XMoveResizeWindow(dpy, c->resize_tr, w - hs, 0, hs, hs);
    XMoveResizeWindow(dpy, c->resize_bl, 0, h - hs, hs, hs);
    XMoveResizeWindow(dpy, c->resize_br, w - hs, h - hs, hs, hs);
    
    XResizeWindow(dpy, c->title_bar, c->w, TITLE_BAR_HEIGHT);
    
    int btn_size = TITLE_BAR_HEIGHT - 4; 
    int margin_right = 15;
    int btn_gap = 3;
    
    int close_x = c->w - margin_right - btn_size;
    int full_x = close_x - btn_gap - btn_size;

    XMoveWindow(dpy, c->close_btn, close_x, 2);
    XMoveWindow(dpy, c->full_btn, full_x, 2);
}

void draw_button(Window btn, const char* type) {
    XWindowAttributes attrs;
    XGetWindowAttributes(dpy, btn, &attrs);
    int w = attrs.width;
    int h = attrs.height;
    
    XClearWindow(dpy, btn);
    
    GC gc = XCreateGC(dpy, btn, 0, NULL);
    XSetForeground(dpy, gc, 0xFFFFFF); 
    XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);

    if (strcmp(type, "close") == 0) {
        XDrawLine(dpy, btn, gc, 2, 2, w-3, h-3);
        XDrawLine(dpy, btn, gc, w-3, 2, 2, h-3);
    } else if (strcmp(type, "full") == 0) {
        XDrawRectangle(dpy, btn, gc, 2, 2, w-5, h-5);
    }
    XFreeGC(dpy, gc);
}

void draw_title(Client* c) {
    XClearWindow(dpy, c->title_bar);
    
    char* name = NULL;
    if (XFetchName(dpy, c->window, &name) && name) {
        GC gc = XCreateGC(dpy, c->title_bar, 0, NULL);
        XSetForeground(dpy, gc, COLOR_TITLE_TEXT);
        
        int len = strlen(name);
        XFontStruct* font = XQueryFont(dpy, XGContextFromGC(gc));
        int text_width = 0;
        if (font) {
            text_width = XTextWidth(font, name, len);
            XFreeFontInfo(NULL, font, 1); 
        }
        
        int x = (c->w - text_width) / 2;
        if (x < 5) x = 5;
        
        XDrawString(dpy, c->title_bar, gc, x, 16, name, len);
        
        XFree(name);
        XFreeGC(dpy, gc);
    }
}

Window create_handle(Window parent, Cursor cursor) {
    Window w = XCreateWindow(dpy, parent, 0, 0, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, 0, NULL);
    XDefineCursor(dpy, w, cursor);
    XSelectInput(dpy, w, ButtonPressMask | ButtonReleaseMask);
    XMapWindow(dpy, w);
    return w;
}

void frame_window(Window w) {
    XWindowAttributes attrs;
    XGetWindowAttributes(dpy, w, &attrs);
    if (attrs.override_redirect || find_client(w)) return;

    Client* c = new Client();
    c->window = w;
    c->x = attrs.x;
    c->y = attrs.y;
    c->w = attrs.width;
    c->h = attrs.height;
    c->fullscreen = false;
    
    c->saved_x = c->x; c->saved_y = c->y; 
    c->saved_w = c->w; c->saved_h = c->h;

    c->frame = XCreateSimpleWindow(dpy, root, c->x, c->y, c->w, c->h + TITLE_BAR_HEIGHT, BORDER_WIDTH, COLOR_BORDER, 0xFFFFFF);
    c->title_bar = XCreateSimpleWindow(dpy, c->frame, 0, 0, c->w, TITLE_BAR_HEIGHT, 0, COLOR_BORDER, COLOR_TITLE_BG);
    
    int btn_size = TITLE_BAR_HEIGHT - 4;
    c->close_btn = XCreateSimpleWindow(dpy, c->title_bar, 0, 0, btn_size, btn_size, 0, COLOR_BORDER, COLOR_TITLE_BG);
    c->full_btn = XCreateSimpleWindow(dpy, c->title_bar, 0, 0, btn_size, btn_size, 0, COLOR_BORDER, COLOR_TITLE_BG);

    c->resize_l = create_handle(c->frame, XCreateFontCursor(dpy, XC_left_side));
    c->resize_r = create_handle(c->frame, XCreateFontCursor(dpy, XC_right_side));
    c->resize_t = create_handle(c->frame, XCreateFontCursor(dpy, XC_top_side));
    c->resize_b = create_handle(c->frame, XCreateFontCursor(dpy, XC_bottom_side));
    c->resize_tl = create_handle(c->frame, XCreateFontCursor(dpy, XC_top_left_corner));
    c->resize_tr = create_handle(c->frame, XCreateFontCursor(dpy, XC_top_right_corner));
    c->resize_bl = create_handle(c->frame, XCreateFontCursor(dpy, XC_bottom_left_corner));
    c->resize_br = create_handle(c->frame, XCreateFontCursor(dpy, XC_bottom_right_corner));

    XSelectInput(dpy, c->frame, SubstructureRedirectMask | SubstructureNotifyMask);
    XSelectInput(dpy, c->title_bar, ButtonPressMask | ButtonReleaseMask | ExposureMask);
    XSelectInput(dpy, c->close_btn, ButtonPressMask | ButtonReleaseMask | ExposureMask);
    XSelectInput(dpy, c->full_btn, ButtonPressMask | ButtonReleaseMask | ExposureMask);
    
    XAddToSaveSet(dpy, w);
    XReparentWindow(dpy, w, c->frame, 0, TITLE_BAR_HEIGHT);
    XMapWindow(dpy, c->frame);
    XMapWindow(dpy, c->title_bar);
    XMapWindow(dpy, c->close_btn);
    XMapWindow(dpy, c->full_btn);
    XMapWindow(dpy, w);
    
    update_frame_extents(c);

    clients.push_back(c);
}

void unframe_window(Window w) {
    Client* c = find_client(w);
    if (!c) return;

    XUnmapWindow(dpy, c->frame);
    XReparentWindow(dpy, c->window, root, c->x, c->y);
    XRemoveFromSaveSet(dpy, c->window);
    
    XDestroyWindow(dpy, c->close_btn);
    XDestroyWindow(dpy, c->full_btn);
    XDestroyWindow(dpy, c->title_bar);
    XDestroyWindow(dpy, c->resize_l); XDestroyWindow(dpy, c->resize_r);
    XDestroyWindow(dpy, c->resize_t); XDestroyWindow(dpy, c->resize_b);
    XDestroyWindow(dpy, c->resize_tl); XDestroyWindow(dpy, c->resize_tr);
    XDestroyWindow(dpy, c->resize_bl); XDestroyWindow(dpy, c->resize_br);
    XDestroyWindow(dpy, c->frame);

    clients.erase(std::remove(clients.begin(), clients.end(), c), clients.end());
    delete c;
}

void toggle_fullscreen(Client* c) {
    if (!c->fullscreen) {
        c->saved_x = c->x; c->saved_y = c->y; c->saved_w = c->w; c->saved_h = c->h;
        XWindowAttributes rattrs;
        XGetWindowAttributes(dpy, root, &rattrs);
        
        c->x = 0; c->y = 0; 
        c->w = rattrs.width; 
        c->h = rattrs.height - TITLE_BAR_HEIGHT; 
        
        XSetWindowBorderWidth(dpy, c->frame, 0);
        c->fullscreen = true;
    } else {
        c->x = c->saved_x; c->y = c->saved_y; 
        c->w = c->saved_w; c->h = c->saved_h;
        
        XSetWindowBorderWidth(dpy, c->frame, BORDER_WIDTH);
        c->fullscreen = false;
    }
    
    XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w, c->h + TITLE_BAR_HEIGHT);
    XResizeWindow(dpy, c->window, c->w, c->h);
    update_frame_extents(c);
    send_configure_notify(c);
    
    XRaiseWindow(dpy, c->frame);
    XSetInputFocus(dpy, c->window, RevertToPointerRoot, CurrentTime);
}

void close_window(Client* c) {
    XEvent ev;
    ev.xclient.type = ClientMessage;
    ev.xclient.window = c->window;
    ev.xclient.message_type = wm_protocols;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = wm_delete_window;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, c->window, False, NoEventMask, &ev);
}

enum Action { NONE, MOVE, RESIZE_L, RESIZE_R, RESIZE_T, RESIZE_B, RESIZE_TL, RESIZE_TR, RESIZE_BL, RESIZE_BR };
Action action = NONE;
Client* active_client = nullptr;
int start_x_root, start_y_root;
int start_win_x, start_win_y, start_win_w, start_win_h;

int main() {
    XSetErrorHandler(OnXError);
    dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;

    root = DefaultRootWindow(dpy);
    wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);

    unsigned int nwins;
    Window d1, d2, *wins;
    if (XQueryTree(dpy, root, &d1, &d2, &wins, &nwins)) {
        for (unsigned int i = 0; i < nwins; i++) frame_window(wins[i]);
        XFree(wins);
    }

    XEvent ev;
    while (true) {
        XNextEvent(dpy, &ev);

        if (ev.type == MapRequest) {
            frame_window(ev.xmaprequest.window);
        }
        else if (ev.type == UnmapNotify) {
            Client* c = find_client(ev.xunmap.window);
            if (c && c->window == ev.xunmap.window) {
                unframe_window(c->window);
            }
        }
        else if (ev.type == DestroyNotify) {
            Client* c = find_client(ev.xdestroywindow.window);
            if (c && c->window == ev.xdestroywindow.window) {
                unframe_window(c->window);
            }
        }
        else if (ev.type == ConfigureRequest) {
            XConfigureRequestEvent& cre = ev.xconfigurerequest;
            Client* c = find_client(cre.window);
            if (c) {
                if (c->fullscreen) {
                    send_configure_notify(c);
                } else {
                    c->w = cre.width;
                    c->h = cre.height;
                    XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w, c->h + TITLE_BAR_HEIGHT);
                    XResizeWindow(dpy, c->window, c->w, c->h);
                    update_frame_extents(c);
                }
            } else {
                XWindowChanges wc;
                wc.x = cre.x; wc.y = cre.y; wc.width = cre.width; wc.height = cre.height;
                wc.border_width = cre.border_width; wc.sibling = cre.above; wc.stack_mode = cre.detail;
                XConfigureWindow(dpy, cre.window, cre.value_mask, &wc);
            }
        }
        else if (ev.type == ButtonPress) {
            if (ev.xbutton.button == 1) {
                Client* c = find_client(ev.xbutton.window);
                if (c) {
                    active_client = c;
                    start_x_root = ev.xbutton.x_root;
                    start_y_root = ev.xbutton.y_root;
                    start_win_x = c->x; start_win_y = c->y;
                    start_win_w = c->w; start_win_h = c->h;
                    
                    if (ev.xbutton.window == c->close_btn) close_window(c);
                    else if (ev.xbutton.window == c->full_btn) toggle_fullscreen(c);
                    else if (ev.xbutton.window == c->title_bar) action = MOVE;
                    else if (ev.xbutton.window == c->resize_l) action = RESIZE_L;
                    else if (ev.xbutton.window == c->resize_r) action = RESIZE_R;
                    else if (ev.xbutton.window == c->resize_t) action = RESIZE_T;
                    else if (ev.xbutton.window == c->resize_b) action = RESIZE_B;
                    else if (ev.xbutton.window == c->resize_tl) action = RESIZE_TL;
                    else if (ev.xbutton.window == c->resize_tr) action = RESIZE_TR;
                    else if (ev.xbutton.window == c->resize_bl) action = RESIZE_BL;
                    else if (ev.xbutton.window == c->resize_br) action = RESIZE_BR;
                    
                    XRaiseWindow(dpy, c->frame);
                    XSetInputFocus(dpy, c->window, RevertToPointerRoot, CurrentTime);
                }
            }
        }
        else if (ev.type == ButtonRelease) {
            if (ev.xbutton.button == 1 && action != NONE && active_client) {
                int dx = ev.xbutton.x_root - start_x_root;
                int dy = ev.xbutton.y_root - start_y_root;
                Client* c = active_client;
                
                if (action == MOVE) {
                    c->x = start_win_x + dx;
                    c->y = start_win_y + dy;
                } else {
                    if (action == RESIZE_R || action == RESIZE_TR || action == RESIZE_BR) c->w = start_win_w + dx;
                    if (action == RESIZE_L || action == RESIZE_TL || action == RESIZE_BL) { c->x = start_win_x + dx; c->w = start_win_w - dx; }
                    if (action == RESIZE_B || action == RESIZE_BL || action == RESIZE_BR) c->h = start_win_h + dy;
                    if (action == RESIZE_T || action == RESIZE_TL || action == RESIZE_TR) { c->y = start_win_y + dy; c->h = start_win_h - dy; }
                    
                    if (c->w < 50) c->w = 50;
                    if (c->h < 50) c->h = 50;
                }
                
                XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w, c->h + TITLE_BAR_HEIGHT);
                XResizeWindow(dpy, c->window, c->w, c->h);
                update_frame_extents(c);
                send_configure_notify(c);
                
                action = NONE;
                active_client = nullptr;
            }
        }
        else if (ev.type == Expose) {
            if (ev.xexpose.count == 0) {
                Client* c = find_client(ev.xexpose.window);
                if (c) {
                    if (ev.xexpose.window == c->title_bar) draw_title(c);
                    else if (ev.xexpose.window == c->close_btn) draw_button(c->close_btn, "close");
                    else if (ev.xexpose.window == c->full_btn) draw_button(c->full_btn, "full");
                }
            }
        }
        else if (ev.type == PropertyNotify) {
            if (ev.xproperty.atom == XA_WM_NAME) {
                Client* c = find_client(ev.xproperty.window);
                if (c) draw_title(c);
            }
        }
    }
    return 0;
}