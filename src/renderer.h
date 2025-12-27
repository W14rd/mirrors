#pragma once

#include "x11/capture.h"
using CaptureBackend = X11Capturer;

#include <string>
#include <vector>
#include <cstdint>

enum class RenderMode {
    ANSI256,
    TRUECOLOR,
    GRAYSCALE
};

class ANSIRenderer {
private:
    int term_cols, term_lines;
    float zoom_level;
    int viewport_x, viewport_y;
    int viewport_w, viewport_h;
    int image_width, image_height;
    
    char cell_char;
    RenderMode mode;
    
    std::string buffer;
    std::vector<int> back_buffer;
    
    uint8_t color_lookup[32768];
    uint8_t grayscale_lookup[256];
    
    CaptureBackend::CursorData current_cursor;
    
    void clampViewport();
    
    inline uint8_t rgbToAnsi256(uint8_t r, uint8_t g, uint8_t b);
    inline uint8_t rgbToGrayAnsi(uint8_t r, uint8_t g, uint8_t b);

public:
    ANSIRenderer();
    
    void setDimensions(int cols, int lines);
    void setImageSize(int w, int h);
    void setZoom(float zoom, int center_term_x = -1, int center_term_y = -1);
    void moveViewport(int dx, int dy);
    void setCellChar(char c);
    void setMode(RenderMode m);
    

    void mapTermToImage(int term_x, int term_y, int& img_x, int& img_y);
    
    void setCursor(const CaptureBackend::CursorData& cursor) {
        current_cursor = cursor;
    }
    
    void renderFrame(const uint8_t* rgb_data, int width, int height,
                    int bytes_per_pixel, int bytes_per_line);
    
    const char* getData() const { return buffer.c_str(); }
    size_t getSize() const { return buffer.size(); }
};
