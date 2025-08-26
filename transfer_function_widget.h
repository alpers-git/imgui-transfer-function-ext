#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gl_core_4_5.h"
#include "imgui.h"

namespace ImTF {

enum ColorSpace { LINEAR, SRGB };

struct Colormap {
    std::string name;
    // An RGBA8 1D image
    std::vector<uint8_t> colormap;
    ColorSpace color_space;

    Colormap(const std::string &name,
             const std::vector<uint8_t> &img,
             const ColorSpace color_space);
};

class TransferFunctionWidget {
    struct vec2f {
        float x, y;

        vec2f(float c = 0.f);
        vec2f(float x, float y);
        vec2f(const ImVec2 &v);

        float length() const;

        vec2f operator+(const vec2f &b) const;
        vec2f operator-(const vec2f &b) const;
        vec2f operator/(const vec2f &b) const;
        vec2f operator*(const vec2f &b) const;
        operator ImVec2() const;
    };

    std::vector<Colormap> colormaps;
    size_t selected_colormap = 0;
    std::vector<uint8_t> current_colormap;

    std::vector<vec2f> alpha_control_pts = {vec2f(0.f), vec2f(1.f)};
    size_t selected_point = -1;

    float opacity_scale = 1.f;
    ImVec2 range = ImVec2(0.f, 1.f);

    bool clicked_on_item = false;
    bool gpu_image_stale = true;
    bool colormap_changed = true;
    bool opacity_scale_changed = true;
    bool range_changed = true;
    GLuint colormap_img = -1;
    bool noGui;

public:
    TransferFunctionWidget(bool noGui = false);

    // Add a colormap preset. The image should be a 1D RGBA8 image, if the image
    // is provided in sRGBA colorspace it will be linearized
    void AddColormap(const Colormap &map);

    // Add the transfer function UI into the currently active window
    void DrawColorMap(bool show_help = true);

    // Returns true if any of the widgets was updated since the last
    // call to draw_ui
    bool Changed() const;

    // Returns true if the colormap was updated since the last
    // call to draw_ui
    bool ColorMapChanged() const;

    // Returns true if the opacity scale was updated since the last
    // call to draw_ui
    bool OpacityScaleChanged() const;

    // Returns true if the range was updated since the last
    // call to draw_ui
    bool RangeChanged() const;

    // Get back the RGBA8 color data for the transfer function
    std::vector<uint8_t> GetColormap();

    // Get back the RGBA32F color data for the transfer function
    std::vector<float> GetColormapf();

    // Get back the RGBA32F color data for the transfer function
    // as separate color and opacity vectors
    void GetColormapf(std::vector<float> &color, std::vector<float> &opacity);

    // Get back the opacity scale
    float GetOpacityScale();

    // Get back the range
    ImVec2 GetRange();

    // Draws widget that scales opacity otherwise opacity is 1.0
    bool DrawOpacityScale();

    // Draws the widget that displays the data values as a ruler
    bool DrawRuler(vec2f range);

    // Draws widget that allows you to edit range for the colormap
    bool DrawRanges();

    // Overlays the colormap bar on the image
    void OverlayColormapBar(std::vector<uint32_t>& image, int imageWidth, int imageHeight, 
                           vec2f pos, vec2f dataRange, float scale, bool flip_vertically = false);

    // Load a state from a file
    bool LoadState(const std::string &filepath);

    // Save a state to a file
    bool SaveState(const std::string &filepath);

private:
    void UpdateGPUImage();

    void UpdateColormap();

    void LoadEmbeddedPreset(const uint8_t *buf, size_t size, const std::string &name);
    
    // Helper function for drawing bitmap text on images
    void DrawBitmapNumber(std::vector<uint32_t>& image, int imageWidth, int imageHeight, 
                         float value, int x, int y, float scale, bool flip_vertically = false);
};
}

