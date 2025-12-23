#include "renderer.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

struct AnsiCode {
    char str[16];
    size_t len;
};

static std::vector<int> x_map_cache;
static std::vector<AnsiCode> ansi_code_cache;

ANSIRenderer::ANSIRenderer() 
    : term_cols(80), term_lines(24),
      zoom_level(1.0f), viewport_x(0), viewport_y(0), viewport_w(0), viewport_h(0),
      image_width(0), image_height(0), cell_char(0), mode(RenderMode::ANSI256) {
    
    ansi_code_cache.resize(256);
    for (int i = 0; i < 256; ++i) {
        int len = snprintf(ansi_code_cache[i].str, sizeof(ansi_code_cache[i].str), "\033[48;5;%dm", i);
        ansi_code_cache[i].len = len;
    }
    
    for (int r = 0; r < 32; ++r) {
        for (int g = 0; g < 32; ++g) {
            for (int b = 0; b < 32; ++b) {
                uint8_t r8 = (r * 255) / 31;
                uint8_t g8 = (g * 255) / 31;
                uint8_t b8 = (b * 255) / 31;
                
                uint8_t ansi;
                if (r == g && g == b) {
                    if (r8 < 8) ansi = 16;
                    else if (r8 > 247) ansi = 231;
                    else ansi = 232 + (r8 - 8) / 10;
                } else {
                    uint8_t r6 = (r8 * 5) / 255;
                    uint8_t g6 = (g8 * 5) / 255;
                    uint8_t b6 = (b8 * 5) / 255;
                    ansi = 16 + 36 * r6 + 6 * g6 + b6;
                }
                color_lookup[(r << 10) | (g << 5) | b] = ansi;
            }
        }
    }

    for (int i = 0; i < 256; ++i) {
        if (i < 8) grayscale_lookup[i] = 16;
        else if (i > 248) grayscale_lookup[i] = 231;
        else {
            grayscale_lookup[i] = 232 + ((i - 8) / 10);
        }
    }
    
    buffer.reserve(1920 * 1080 / 2);
    x_map_cache.reserve(300);
}

void ANSIRenderer::setMode(RenderMode m) {
    mode = m;
    back_buffer.assign(term_cols * term_lines, -1);
}


void ANSIRenderer::setDimensions(int cols, int lines) {
    term_cols = cols;
    term_lines = lines;
    x_map_cache.resize(cols);
    back_buffer.assign(cols * lines, -1); 
}

void ANSIRenderer::setImageSize(int w, int h) {
    if (image_width == w && image_height == h) return;

    image_width = w;
    image_height = h;
    
    if (viewport_w == 0 || viewport_h == 0) {
        viewport_w = w;
        viewport_h = h;
        viewport_x = 0;
        viewport_y = 0;
    } else {
        viewport_w = (int)(image_width / zoom_level);
        viewport_h = (int)(image_height / zoom_level);
    }
    
    clampViewport();
}

void ANSIRenderer::clampViewport() {
    if (image_width <= 0 || image_height <= 0) return;

    if (viewport_w > image_width) viewport_w = image_width;
    if (viewport_h > image_height) viewport_h = image_height;
    if (viewport_w < 1) viewport_w = 1;
    if (viewport_h < 1) viewport_h = 1;

    int max_x = image_width - viewport_w;
    int max_y = image_height - viewport_h;

    if (viewport_x < 0) viewport_x = 0;
    else if (viewport_x > max_x) viewport_x = max_x;

    if (viewport_y < 0) viewport_y = 0;
    else if (viewport_y > max_y) viewport_y = max_y;
}

void ANSIRenderer::mapTermToImage(int term_x, int term_y, int& img_x, int& img_y) {
    if (term_cols == 0 || term_lines == 0) { img_x = 0; img_y = 0; return; }
    
    img_x = viewport_x + (int)((long long)term_x * viewport_w / term_cols);
    img_y = viewport_y + (int)((long long)term_y * viewport_h / term_lines);
    
    if (img_x < 0) img_x = 0;
    if (img_x >= image_width) img_x = image_width - 1;
    if (img_y < 0) img_y = 0;
    if (img_y >= image_height) img_y = image_height - 1;
}


void ANSIRenderer::setZoom(float zoom, int center_term_x, int center_term_y) {
    if (zoom < 1.0f) zoom = 1.0f;
    if (zoom > 10.0f) zoom = 10.0f; 
    
    if (center_term_x < 0) center_term_x = term_cols / 2;
    if (center_term_y < 0) center_term_y = term_lines / 2;
    if (term_cols == 0 || term_lines == 0) return;
    
    float rel_x = (float)center_term_x / term_cols;
    float rel_y = (float)center_term_y / term_lines;
    
    float focus_img_x = viewport_x + rel_x * viewport_w;
    float focus_img_y = viewport_y + rel_y * viewport_h;
    
    zoom_level = zoom;
    
    if (image_width > 0 && image_height > 0) {
        viewport_w = (int)(image_width / zoom_level);
        viewport_h = (int)(image_height / zoom_level);
        
        viewport_x = (int)(focus_img_x - rel_x * viewport_w);
        viewport_y = (int)(focus_img_y - rel_y * viewport_h);
        
        clampViewport();
    }
    
    back_buffer.assign(term_cols * term_lines, -1);
}

void ANSIRenderer::moveViewport(int dx, int dy) {
    if (term_cols > 0 && term_lines > 0) {
        int img_dx = (int)((long long)dx * viewport_w / term_cols);
        int img_dy = (int)((long long)dy * viewport_h / term_lines);
        
        viewport_x += img_dx;
        viewport_y += img_dy;
        
        clampViewport();
        
        back_buffer.assign(term_cols * term_lines, -1);
    }
}

void ANSIRenderer::setCellChar(char c) {
    cell_char = c;
    back_buffer.assign(term_cols * term_lines, -1);
}

inline uint8_t ANSIRenderer::rgbToGrayAnsi(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t luma = (r * 77 + g * 150 + b * 29) >> 8;
    return grayscale_lookup[luma > 255 ? 255 : luma];
}


inline uint8_t ANSIRenderer::rgbToAnsi256(uint8_t r, uint8_t g, uint8_t b) {
    return color_lookup[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
}

void ANSIRenderer::renderFrame(const uint8_t* rgb_data, int width, int height,
                               int bytes_per_pixel, int bytes_per_line) {
    if (width != image_width || height != image_height) {
        setImageSize(width, height);
    }

    buffer.clear();
    size_t char_size = (mode == RenderMode::TRUECOLOR) ? 25 : 15;
    size_t needed_cap = term_cols * term_lines * char_size;
    if (buffer.capacity() < needed_cap) buffer.reserve(needed_cap);

    clampViewport();

    static std::vector<int> img_x_cache;
    if (x_map_cache.size() != (size_t)term_cols) x_map_cache.resize(term_cols);
    if (img_x_cache.size() != (size_t)term_cols) img_x_cache.resize(term_cols);
    
    for (int x = 0; x < term_cols; ++x) {
        int img_x = viewport_x + (int)((long long)x * viewport_w / term_cols);
        if (img_x < 0) img_x = 0; else if (img_x >= width) img_x = width - 1;
        x_map_cache[x] = img_x * bytes_per_pixel;
        img_x_cache[x] = img_x;
    }

    buffer.append("\033[H", 3);

    int last_ansi = -1;
    int last_r = -1, last_g = -1, last_b = -1;
    
    char char_to_print = (cell_char == 0) ? ' ' : cell_char;
    char tmp_seq[32]; 

    for (int y = 0; y < term_lines; ++y) {
        int img_y = viewport_y + (int)((long long)y * viewport_h / term_lines);
        if (img_y < 0) img_y = 0; else if (img_y >= height) img_y = height - 1;
        
        const uint8_t* row_ptr = rgb_data + (img_y * bytes_per_line);
        
        for (int x = 0; x < term_cols; ++x) {
            const uint8_t* pixel = row_ptr + x_map_cache[x];
            
            uint8_t r = pixel[2];
            uint8_t g = pixel[1];
            uint8_t b = pixel[0];

            if (current_cursor.visible) {
                int img_x = img_x_cache[x];
                int cur_x = img_x - (current_cursor.x - current_cursor.xhot);
                int cur_y = img_y - (current_cursor.y - current_cursor.yhot);
                
                if (cur_x >= 0 && cur_x < current_cursor.width &&
                    cur_y >= 0 && cur_y < current_cursor.height) {
                    
                    uint32_t c_pixel = current_cursor.pixels[cur_y * current_cursor.width + cur_x];
                    uint8_t ca = (c_pixel >> 24) & 0xFF;
                    
                    if (ca > 0) {
                        uint8_t cr = (c_pixel >> 16) & 0xFF;
                        uint8_t cg = (c_pixel >> 8) & 0xFF;
                        uint8_t cb = (c_pixel) & 0xFF;
                        
                        r = (cr * ca + r * (255 - ca)) / 255;
                        g = (cg * ca + g * (255 - ca)) / 255;
                        b = (cb * ca + b * (255 - ca)) / 255;
                    }
                }
            }

            if (mode == RenderMode::TRUECOLOR) {
                if (r == 0 && g == 0 && b == 0) {
                    if (last_r != 0 || last_g != 0 || last_b != 0 || last_ansi != -2) {
                        buffer.append("\033[49m", 5);
                        last_r = 0; last_g = 0; last_b = 0;
                        last_ansi = -2; // Special value for transparent
                    }
                } else if (r != last_r || g != last_g || b != last_b) {
                    int len = snprintf(tmp_seq, sizeof(tmp_seq), "\033[48;2;%d;%d;%dm", r, g, b);
                    buffer.append(tmp_seq, len);
                    last_r = r; last_g = g; last_b = b;
                    last_ansi = -1;
                }
            } 
            else if (mode == RenderMode::GRAYSCALE) {
                uint8_t ansi = rgbToGrayAnsi(r, g, b);
                if (r == 0 && g == 0 && b == 0) {
                    if (last_ansi != -2) {
                        buffer.append("\033[49m", 5);
                        last_ansi = -2;
                    }
                } else if (ansi != last_ansi) {
                    const AnsiCode& code = ansi_code_cache[ansi];
                    buffer.append(code.str, code.len);
                    last_ansi = ansi;
                }
            } 
            else {
                uint8_t ansi = color_lookup[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
                if (r == 0 && g == 0 && b == 0) {
                    if (last_ansi != -2) {
                        buffer.append("\033[49m", 5);
                        last_ansi = -2;
                    }
                } else if (ansi != last_ansi) {
                    const AnsiCode& code = ansi_code_cache[ansi];
                    buffer.append(code.str, code.len);
                    last_ansi = ansi;
                }
            }
            buffer.push_back(char_to_print);
        }
        
        if (y < term_lines - 1) {
            buffer.append("\r\n", 2);
        } else {
            buffer.append("\033[0m", 4);
        }
    }
}
