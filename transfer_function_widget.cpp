#include "transfer_function_widget.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include "embedded_colormaps.h"

#ifndef TFN_WIDGET_NO_STB_IMAGE_IMPL
#define STB_IMAGE_IMPLEMENTATION
#endif

#include "stb_image.h"

namespace ImTF {

template <typename T>
inline T clamp(T x, T min, T max)
{
    if (x < min) {
        return min;
    }
    if (x > max) {
        return max;
    }
    return x;
}

inline float srgb_to_linear(const float x)
{
    if (x <= 0.04045f) {
        return x / 12.92f;
    } else {
        return std::pow((x + 0.055f) / 1.055f, 2.4f);
    }
}

Colormap::Colormap(const std::string &name,
                   const std::vector<uint8_t> &img,
                   const ColorSpace color_space)
    : name(name), colormap(img), color_space(color_space)
{
}

TransferFunctionWidget::vec2f::vec2f(float c) : x(c), y(c) {}

TransferFunctionWidget::vec2f::vec2f(float x, float y) : x(x), y(y) {}

TransferFunctionWidget::vec2f::vec2f(const ImVec2 &v) : x(v.x), y(v.y) {}

float TransferFunctionWidget::vec2f::length() const
{
    return std::sqrt(x * x + y * y);
}

TransferFunctionWidget::vec2f TransferFunctionWidget::vec2f::operator+(
    const TransferFunctionWidget::vec2f &b) const
{
    return vec2f(x + b.x, y + b.y);
}

TransferFunctionWidget::vec2f TransferFunctionWidget::vec2f::operator-(
    const TransferFunctionWidget::vec2f &b) const
{
    return vec2f(x - b.x, y - b.y);
}

TransferFunctionWidget::vec2f TransferFunctionWidget::vec2f::operator/(
    const TransferFunctionWidget::vec2f &b) const
{
    return vec2f(x / b.x, y / b.y);
}

TransferFunctionWidget::vec2f TransferFunctionWidget::vec2f::operator*(
    const TransferFunctionWidget::vec2f &b) const
{
    return vec2f(x * b.x, y * b.y);
}

TransferFunctionWidget::vec2f::operator ImVec2() const
{
    return ImVec2(x, y);
}

TransferFunctionWidget::TransferFunctionWidget(bool noGui)
    :noGui(noGui)
{
    if (!noGui && ogl_LoadFunctions() == ogl_LOAD_FAILED)
    {
        std::cerr << "Failed to initialize OpenGL\n";
        return;
    }
    // Load up the embedded colormaps as the default options
    LoadEmbeddedPreset(paraview_cool_warm, sizeof(paraview_cool_warm), "ParaView Cool Warm");
    LoadEmbeddedPreset(rainbow, sizeof(rainbow), "Rainbow");
    LoadEmbeddedPreset(reds, sizeof(reds), "Reds");
    LoadEmbeddedPreset(greens, sizeof(greens), "Greens");
    LoadEmbeddedPreset(blues, sizeof(blues), "Blues");
    LoadEmbeddedPreset(matplotlib_plasma, sizeof(matplotlib_plasma), "Matplotlib Plasma");
    LoadEmbeddedPreset(matplotlib_virdis, sizeof(matplotlib_virdis), "Matplotlib Virdis");
    LoadEmbeddedPreset(matplotlib_BrBg, sizeof(matplotlib_BrBg), "Matplotlib BrBg");
    LoadEmbeddedPreset(matplotlib_terrain, sizeof(matplotlib_terrain), "Matplotlib Terrain");
    LoadEmbeddedPreset(tacc_outlier, sizeof(tacc_outlier), "TACC Outlier");
    LoadEmbeddedPreset(
        samsel_linear_green, sizeof(samsel_linear_green), "Samsel Linear Green");
    LoadEmbeddedPreset(
        samsel_linear_ygb_1211g, sizeof(samsel_linear_ygb_1211g), "Samsel Linear YGB 1211G");
    LoadEmbeddedPreset(cool_warm_extended, sizeof(cool_warm_extended), "Cool Warm Extended");
    LoadEmbeddedPreset(blackbody, sizeof(blackbody), "Black Body");
    LoadEmbeddedPreset(jet, sizeof(jet), "Jet");
    LoadEmbeddedPreset(blue_gold, sizeof(blue_gold), "Blue Gold");
    LoadEmbeddedPreset(ice_fire, sizeof(ice_fire), "Ice Fire");
    LoadEmbeddedPreset(nic_edge, sizeof(nic_edge), "nic Edge");
    LoadEmbeddedPreset(cube_helix, sizeof(cube_helix), "Cube Helix");
    LoadEmbeddedPreset(linear_grayscale, sizeof(linear_grayscale), "Linear Grayscale");
    LoadEmbeddedPreset(flat_red, sizeof(flat_red), "flat red");
    LoadEmbeddedPreset(flat_green, sizeof(flat_green), "flat green");
    LoadEmbeddedPreset(flat_blue, sizeof(flat_blue), "flat blue");

    // Initialize the colormap alpha channel w/ a linear ramp
    UpdateColormap();
}

void TransferFunctionWidget::AddColormap(const Colormap &map)
{
    colormaps.push_back(map);

    if (colormaps.back().color_space == SRGB) {
        Colormap &cmap = colormaps.back();
        cmap.color_space = LINEAR;
        for (size_t i = 0; i < cmap.colormap.size() / 4; ++i) {
            for (size_t j = 0; j < 3; ++j) {
                const float x = srgb_to_linear(cmap.colormap[i * 4 + j] / 255.f);
                cmap.colormap[i * 4 + j] = static_cast<uint8_t>(clamp(x * 255.f, 0.f, 255.f));
            }
        }
    }
}

void TransferFunctionWidget::DrawColorMap(bool show_help)
{
    if(noGui)
    {
        std::cerr << "TransferFunctionWidget::DrawColorMap() called with noGui set to true\n";
        return;
    }
    UpdateGPUImage();

    const ImGuiIO &io = ImGui::GetIO();

    if(show_help)
    {
        ImGui::Text("Transfer Function");
        ImGui::TextWrapped(
            "Left click to add a point, right click remove. "
            "Left click + drag to move points.");
    }

    if (ImGui::BeginCombo("Colormap", colormaps[selected_colormap].name.c_str())) {
        for (size_t i = 0; i < colormaps.size(); ++i) {
            if (ImGui::Selectable(colormaps[i].name.c_str(), selected_colormap == i)) {
                selected_colormap = i;
                UpdateColormap();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    //reset button to reset the transfer function
    if (ImGui::Button("Reset")) {
        alpha_control_pts.clear();
        alpha_control_pts.push_back(vec2f(0.f, 0.f));
        alpha_control_pts.push_back(vec2f(1.f, 1.f));
        selected_colormap = 0;
        opacity_scale = 1.f;
        UpdateColormap();
    }

    vec2f canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.y /= 3.f;
    // Note: If you're not using OpenGL for rendering your UI, the setup for
    // displaying the colormap texture in the UI will need to be updated.
    size_t tmp = colormap_img;
    ImGui::Image(reinterpret_cast<void*>(tmp), ImVec2(canvas_size.x, 16));
    vec2f canvas_pos = ImGui::GetCursorScreenPos();
    canvas_size.y -= 20;

    const float point_radius = 10.f;

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(canvas_pos, canvas_pos + canvas_size);

    const vec2f view_scale(canvas_size.x, -canvas_size.y);
    const vec2f view_offset(canvas_pos.x, canvas_pos.y + canvas_size.y);

    draw_list->AddRect(canvas_pos, canvas_pos + canvas_size, ImColor(180, 180, 180, 255));

    ImGui::InvisibleButton("tfn_canvas", canvas_size);

    static bool clicked_on_item = false;
    if (!io.MouseDown[0] && !io.MouseDown[1]) {
        clicked_on_item = false;
    }
    if (ImGui::IsItemHovered() && (io.MouseDown[0] || io.MouseDown[1])) {
        clicked_on_item = true;
    }

    ImVec2 bbmin = ImGui::GetItemRectMin();
    ImVec2 bbmax = ImGui::GetItemRectMax();
    ImVec2 clipped_mouse_pos = ImVec2(std::min(std::max(io.MousePos.x, bbmin.x), bbmax.x),
                                      std::min(std::max(io.MousePos.y, bbmin.y), bbmax.y));

    if (clicked_on_item) {
        vec2f mouse_pos = (vec2f(clipped_mouse_pos) - view_offset) / view_scale;
        mouse_pos.x = clamp(mouse_pos.x, 0.f, 1.f);
        mouse_pos.y = clamp(mouse_pos.y, 0.f, 1.f);

        if (io.MouseDown[0]) {
            if (selected_point != (size_t)-1) {
                alpha_control_pts[selected_point] = mouse_pos;

                // Keep the first and last control points at the edges
                if (selected_point == 0) {
                    alpha_control_pts[selected_point].x = 0.f;
                } else if (selected_point == alpha_control_pts.size() - 1) {
                    alpha_control_pts[selected_point].x = 1.f;
                }
            } else {
                auto fnd = std::find_if(
                    alpha_control_pts.begin(), alpha_control_pts.end(), [&](const vec2f &p) {
                        const vec2f pt_pos = p * view_scale + view_offset;
                        float dist = (pt_pos - vec2f(clipped_mouse_pos)).length();
                        return dist <= point_radius;
                    });
                // No nearby point, we're adding a new one
                if (fnd == alpha_control_pts.end()) {
                    alpha_control_pts.push_back(mouse_pos);
                }
            }

            // Keep alpha control points ordered by x coordinate, update
            // selected point index to match
            std::sort(alpha_control_pts.begin(),
                      alpha_control_pts.end(),
                      [](const vec2f &a, const vec2f &b) { return a.x < b.x; });
            if (selected_point != 0 && selected_point != alpha_control_pts.size() - 1) {
                auto fnd = std::find_if(
                    alpha_control_pts.begin(), alpha_control_pts.end(), [&](const vec2f &p) {
                        const vec2f pt_pos = p * view_scale + view_offset;
                        float dist = (pt_pos - vec2f(clipped_mouse_pos)).length();
                        return dist <= point_radius;
                    });
                selected_point = std::distance(alpha_control_pts.begin(), fnd);
            }
            UpdateColormap();
        } else if (ImGui::IsMouseClicked(1)) {
            selected_point = -1;
            // Find and remove the point
            auto fnd = std::find_if(
                alpha_control_pts.begin(), alpha_control_pts.end(), [&](const vec2f &p) {
                    const vec2f pt_pos = p * view_scale + view_offset;
                    float dist = (pt_pos - vec2f(clipped_mouse_pos)).length();
                    return dist <= point_radius;
                });
            // We also want to prevent erasing the first and last points
            if (fnd != alpha_control_pts.end() && fnd != alpha_control_pts.begin() &&
                fnd != alpha_control_pts.end() - 1) {
                alpha_control_pts.erase(fnd);
            }
            UpdateColormap();
        } else {
            selected_point = -1;
        }
    } else {
        selected_point = -1;
    }

    // Draw the alpha control points, and build the points for the polyline
    // which connects them
    std::vector<ImVec2> polyline_pts;
    for (const auto &pt : alpha_control_pts) {
        const vec2f pt_pos = pt * view_scale + view_offset;
        polyline_pts.push_back(pt_pos);
        draw_list->AddCircleFilled(pt_pos, point_radius, 0xFFFFFFFF);
    }
    draw_list->AddPolyline(
        polyline_pts.data(), (int)polyline_pts.size(), 0xFFFFFFFF, false, 2.f);
    draw_list->PopClipRect();
}

bool TransferFunctionWidget::DrawOpacityScale()
{
    if(noGui)
    {
        std::cerr << "TransferFunctionWidget::DrawOpacityScale() called with noGui set to true\n";
        return false;
    }
    ImGui::Text("Opacity scale");
    ImGui::SameLine();
    if (ImGui::SliderFloat("##1", &opacity_scale, 0.0f, 1.0f))
    {
        opacity_scale_changed = true;
        UpdateColormap();
        return true;
    }
    return false;
}

bool TransferFunctionWidget::DrawRuler(vec2f data_range)
{
    if(noGui)
    {
        std::cerr << "TransferFunctionWidget::DrawRuler() called with noGui set to true\n";
        return false;
    }

    // Get the canvas dimensions and position
    vec2f canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.y = 30.0f; // Height for the ruler
    vec2f canvas_pos = ImGui::GetCursorScreenPos();
    
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    
    // Draw background for ruler
    draw_list->AddRectFilled(canvas_pos, canvas_pos + canvas_size, 
                            ImColor(0, 0, 0, 255));
    draw_list->AddRect(canvas_pos, canvas_pos + canvas_size, 
                      ImColor(100, 100, 100, 0));

    // Number of ticks to draw that scales with the canvas size
    const int num_ticks = std::max(static_cast<int>(canvas_size.x / 50.0f), 2);
    const float tick_spacing = canvas_size.x / (num_ticks - 1);
    const float tick_height = 8.0f;
    
    // Calculate the actual data range that the transfer function covers
    // data_range is the full data range (min/max of actual data)
    // range is the transfer function's relative range mapping
    const float data_span = data_range.y - data_range.x;
    const float tf_range_span = range.y - range.x;
    
    // The actual data values that map to the start and end of the transfer function
    const float actual_min = data_range.x + range.x * data_span;
    const float actual_max = data_range.x + range.y * data_span;
    const float actual_span = actual_max - actual_min;
    
    // Draw ticks and labels
    for (int i = 0; i < num_ticks; ++i) {
        float x_pos = canvas_pos.x + i * tick_spacing;
        float tick_top = canvas_pos.y + 2.0f;
        float tick_bottom = tick_top + tick_height;
        
        // Draw tick mark
        draw_list->AddLine(ImVec2(x_pos, tick_top), ImVec2(x_pos, tick_bottom), 
                          ImColor(200, 200, 200, 255), 1.0f);
        
        // Calculate the actual data value this tick represents
        float normalized_pos = static_cast<float>(i) / (num_ticks - 1);
        float value = actual_min + normalized_pos * actual_span;
        
        // Format the value string
        char value_str[32];
        if (std::abs(value) < 0.001f && std::abs(value) > 0.0f) {
            // Use scientific notation for very small numbers
            snprintf(value_str, sizeof(value_str), "%.1e", value);
        } else if (std::abs(value) >= 1000.0f) {
            // Use scientific notation for large numbers
            snprintf(value_str, sizeof(value_str), "%.1e", value);
        } else {
            // Use fixed point notation for reasonable numbers
            snprintf(value_str, sizeof(value_str), "%.2f", value);
        }
        
        // Calculate text size and position for centering
        ImVec2 text_size = ImGui::CalcTextSize(value_str);
        float text_x = x_pos - text_size.x * 0.5f;
        float text_y = tick_bottom + 2.0f;
        
        // Ensure text doesn't go outside canvas bounds
        text_x = std::max(canvas_pos.x, std::min(text_x, canvas_pos.x + canvas_size.x - text_size.x));
        
        // Draw the value label
        draw_list->AddText(ImVec2(text_x, text_y), ImColor(200, 200, 200, 255), value_str);
    }
    
    // Reserve space for the ruler
    ImGui::Dummy(canvas_size);
    
    return false; // No interaction/changes made
}

bool TransferFunctionWidget::DrawRanges()
{
    if(noGui)
    {
        std::cerr << "TransferFunctionWidget::DrawRanges() called with noGui set to true\n";
        return false;
    }
    ImGui::Text("Range:");
    ImGui::SameLine();
    if (ImGui::InputFloat2("##2", &range.x, "%.3f"))
    {
        //clamp min below max and max over min
        range.x = std::min(range.x, range.y-1e-6f);
        range.y = std::max(range.x+1e-6f, range.y);
        range_changed = true;
        return true;
    }
    return false;
}

void TransferFunctionWidget::OverlayColormapBar(std::vector<uint32_t>& image, int imageWidth, int imageHeight, 
                                               vec2f pos, vec2f dataRange, float scale, bool flip_vertically)
{
    // Base dimensions for the colormap bar
    const int baseBarWidth = 40;
    const int baseBarHeight = 200;
    const int baseTickLength = 8;
    const int numTicks = 5; // Including min and max
    
    // Scale the dimensions
    const int barWidth = static_cast<int>(baseBarWidth * scale);
    const int barHeight = static_cast<int>(baseBarHeight * scale);
    const int tickLength = static_cast<int>(baseTickLength * scale);
    
    // Calculate bar position using distance from bottom-left corner
    // pos.x is distance from left edge, pos.y is distance from bottom edge
    // Account for vertical flipping if enabled
    int barX = static_cast<int>(pos.x);
    int barY;
    if (flip_vertically) {
        // When vertically flipped, top becomes bottom, so use pos.y directly from top
        barY = static_cast<int>(pos.y);
    } else {
        // Normal case: convert from bottom-left to top-left coordinates
        barY = imageHeight - static_cast<int>(pos.y) - barHeight;
    }
    
    // Skip if bar would be outside the image
    if (barX < 0 || barY < 0 || pos.x >= imageWidth || pos.y >= imageHeight) return;
    
    // Get colormap data
    auto colormap = GetColormap();
    if (colormap.empty()) return;
    
    // Draw the colormap bar (vertical)
    for (int y = 0; y < barHeight; ++y) {
        // Map y position to colormap index 
        // Account for vertical flipping
        float t;
        if (flip_vertically) {
            // When flipped, y=0 (top of bar) should show minimum value (t=0)
            // y=barHeight-1 (bottom of bar) should show maximum value (t=1)
            t = (float)y / (barHeight - 1);
        } else {
            // Normal case: y=0 (top of bar) should show maximum value (t=1)
            // y=barHeight-1 (bottom of bar) should show minimum value (t=0)
            t = 1.0f - (float)y / (barHeight - 1);
        }
        int cmapIndex = static_cast<int>(t * (colormap.size() / 4 - 1)) * 4;
        cmapIndex = std::max(0, std::min(cmapIndex, static_cast<int>(colormap.size()) - 4));
        
        // Extract RGBA values from colormap
        uint8_t r = colormap[cmapIndex + 0];
        uint8_t g = colormap[cmapIndex + 1];
        uint8_t b = colormap[cmapIndex + 2];
        uint8_t a = colormap[cmapIndex + 3];
        
        // Convert to uint32_t (assuming RGBA format)
        uint32_t color = (a << 24) | (b << 16) | (g << 8) | r;
        
        // Draw horizontal line for this color
        for (int x = 0; x < barWidth; ++x) {
            int pixelX = barX + x;
            int pixelY = barY + y;
            
            if (pixelX >= 0 && pixelX < imageWidth && 
                pixelY >= 0 && pixelY < imageHeight) {
                int pixelIndex = pixelY * imageWidth + pixelX;
                image[pixelIndex] = color;
            }
        }
    }
    
    // Calculate the actual data range that the transfer function covers
    ImVec2 tfRange = GetRange();
    const float dataSpan = dataRange.y - dataRange.x;
    const float actualMin = dataRange.x + tfRange.x * dataSpan;
    const float actualMax = dataRange.x + tfRange.y * dataSpan;
    const float actualSpan = actualMax - actualMin;
    
    // Draw ticks and labels
    for (int tick = 0; tick < numTicks; ++tick) {
        float t = static_cast<float>(tick) / (numTicks - 1);
        // Calculate tick position accounting for vertical flipping
        int tickY;
        if (flip_vertically) {
            // When flipped, tick=0 should be at top (min value), tick=numTicks-1 should be at bottom (max value)
            tickY = barY + static_cast<int>(t * (barHeight - 1));
        } else {
            // Normal case: tick=0 should be at bottom (min value), tick=numTicks-1 should be at top (max value)
            tickY = barY + barHeight - 1 - static_cast<int>(t * (barHeight - 1));
        }
        float value = actualMin + t * actualSpan;
        
        // Draw tick mark (white line extending to the right)
        uint32_t tickColor = 0xFFFFFFFF; // White
        for (int i = 0; i < tickLength; ++i) {
            int tickX = barX + barWidth + i;
            if (tickX < imageWidth && tickY >= 0 && tickY < imageHeight) {
                int pixelIndex = tickY * imageWidth + tickX;
                image[pixelIndex] = tickColor;
            }
        }
        
        // Draw simple number text
        DrawBitmapNumber(image, imageWidth, imageHeight, value, 
                        barX + barWidth + tickLength + 2, tickY - 4, scale, flip_vertically);
    }
    
    // Draw border around the colormap bar
    uint32_t borderColor = 0xFFFFFFFF; // White border
    // Top and bottom borders
    for (int x = 0; x < barWidth; ++x) {
        int topPixelIndex = barY * imageWidth + (barX + x);
        int bottomPixelIndex = (barY + barHeight - 1) * imageWidth + (barX + x);
        if (barX + x >= 0 && barX + x < imageWidth) {
            if (barY >= 0 && barY < imageHeight)
                image[topPixelIndex] = borderColor;
            if (barY + barHeight - 1 >= 0 && barY + barHeight - 1 < imageHeight)
                image[bottomPixelIndex] = borderColor;
        }
    }
    // Left and right borders
    for (int y = 0; y < barHeight; ++y) {
        int leftPixelIndex = (barY + y) * imageWidth + barX;
        int rightPixelIndex = (barY + y) * imageWidth + (barX + barWidth - 1);
        if (barY + y >= 0 && barY + y < imageHeight) {
            if (barX >= 0 && barX < imageWidth)
                image[leftPixelIndex] = borderColor;
            if (barX + barWidth - 1 >= 0 && barX + barWidth - 1 < imageWidth)
                image[rightPixelIndex] = borderColor;
        }
    }
}

void TransferFunctionWidget::DrawBitmapNumber(std::vector<uint32_t>& image, int imageWidth, int imageHeight, 
                                             float value, int x, int y, float scale, bool flip_vertically)
{
    // Format the number
    char buffer[32];
    if (std::abs(value) < 0.01f && value != 0.0f) {
        snprintf(buffer, sizeof(buffer), "%.1e", value);
    } else if (std::abs(value) >= 1000.0f) {
        snprintf(buffer, sizeof(buffer), "%.1e", value);
    } else {
        snprintf(buffer, sizeof(buffer), "%.2f", value);
    }
    
    // Simple 5x7 bitmap font for digits and basic characters
    // Each row represents a horizontal scan line from top to bottom
    // Bits are read from MSB to LSB (left to right)
    static const uint8_t font5x7[][7] = {
        {0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70}, // '0'
        {0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70}, // '1'
        {0x70, 0x88, 0x08, 0x10, 0x20, 0x40, 0xF8}, // '2'
        {0xF8, 0x10, 0x20, 0x10, 0x08, 0x88, 0x70}, // '3'
        {0x10, 0x30, 0x50, 0x90, 0xF8, 0x10, 0x10}, // '4'
        {0xF8, 0x80, 0xF0, 0x08, 0x08, 0x88, 0x70}, // '5'
        {0x30, 0x40, 0x80, 0xF0, 0x88, 0x88, 0x70}, // '6'
        {0xF8, 0x08, 0x10, 0x20, 0x40, 0x40, 0x40}, // '7'
        {0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70}, // '8'
        {0x70, 0x88, 0x88, 0x78, 0x08, 0x10, 0x60}, // '9'
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ' ' (space)
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x60}, // '.'
        {0x00, 0x00, 0x00, 0xF8, 0x00, 0x00, 0x00}, // '-'
        {0x70, 0x88, 0x08, 0x10, 0x20, 0x00, 0x20}, // '?'
        {0x70, 0x88, 0x88, 0xA8, 0xA8, 0xB0, 0x70}, // '@' (used for 'e' in scientific notation)
        {0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xF8}, // '+' 
    };
    
    // Character mapping
    auto getCharIndex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        switch (c) {
            case ' ': return 10;
            case '.': return 11;
            case '-': return 12;
            case '?': return 13; // fallback
            case 'e': case 'E': return 14; // for scientific notation
            case '+': return 15;
            default: return 13; // '?' as fallback
        }
    };
    
    uint32_t textColor = 0xFFFFFFFF; // White
    const int baseCharWidth = 6; // 5 pixels + 1 spacing
    const int baseCharHeight = 7;
    const int charWidth = static_cast<int>(baseCharWidth * scale);
    const int charHeight = static_cast<int>(baseCharHeight * scale);
    
    // Draw each character
    for (int charIdx = 0; buffer[charIdx] != '\0' && charIdx < 10; ++charIdx) {
        int charX = x + charIdx * charWidth;
        int fontIdx = getCharIndex(buffer[charIdx]);
        
        // Draw the character bitmap with scaling
        for (int row = 0; row < baseCharHeight; ++row) {
            // When flip_vertically is true, draw from bottom to top to pre-compensate for image flip
            int actualRow = flip_vertically ? (baseCharHeight - 1 - row) : row;
            uint8_t rowData = font5x7[fontIdx][actualRow];
            for (int col = 0; col < 5; ++col) {
                // Check bit from MSB to LSB (left to right) - bit 7,6,5,4,3
                if (rowData & (0x80 >> col)) {
                    // Draw scaled pixel block
                    for (int sy = 0; sy < static_cast<int>(scale); ++sy) {
                        for (int sx = 0; sx < static_cast<int>(scale); ++sx) {
                            int pixelX = charX + col * static_cast<int>(scale) + sx;
                            int pixelY = y + row * static_cast<int>(scale) + sy;
                            
                            if (pixelX >= 0 && pixelX < imageWidth && 
                                pixelY >= 0 && pixelY < imageHeight) {
                                int pixelIndex = pixelY * imageWidth + pixelX;
                                image[pixelIndex] = textColor;
                            }
                        }
                    }
                }
            }
        }
    }
}

bool TransferFunctionWidget::LoadState(const std::string &filepath)
{
    // Read the file
    std::ifstream fp;
    fp.open(filepath, std::ios::in | std::ios::binary);
    if (!fp.is_open()) {
        printf("Could not open file %s\n", filepath.c_str());
        return false;
    }

    // Read the opacity scale
    fp >> opacity_scale;

    // Read the range
    fp >> range.x >> range.y;

    // Read the current colormap size
    uint32_t current_colormap_size;
    fp >> current_colormap_size;
    fp.ignore();  // Ignore the newline character

    // Read the current colormap
    current_colormap.clear();
    current_colormap.resize(current_colormap_size);
    fp.read(reinterpret_cast<char*>(current_colormap.data()), current_colormap_size * sizeof(uint8_t));

    std::string colormap_name;
    std::getline(fp, colormap_name);  // Read the entire line as the colormap name

    // If the colormap name is not "custom," then look through the list of colormaps
    // names and set the selected_colormap variable to its index
    if (colormap_name != "custom") {
        for (int i = 0; i < colormaps.size(); i++) {
            if (colormaps[i].name == colormap_name) {
                selected_colormap = i;
                break;
            }
        }
    }
    else {
        LoadEmbeddedPreset(reinterpret_cast<uint8_t*>(current_colormap.data()), current_colormap.size(), "custom");
    }

    // Read the control points
    size_t num_pts;
    fp >> num_pts;
    alpha_control_pts.clear();
    alpha_control_pts.resize(num_pts);
    for (auto &pt : alpha_control_pts) {
        fp >> pt.x;
        fp >> pt.y;
    }
    fp.close();
    printf("Transferfunction read from file %s\n", filepath.c_str());
    UpdateColormap();
    UpdateGPUImage();
    return true;
}

bool TransferFunctionWidget::SaveState(const std::string &filepath)
{
    //create file and write to it
    std::ofstream fp;
    fp.open(filepath, std::ios::out | std::ios::binary);
    //clear the file
    fp.clear();

    if (!fp.is_open()) {
        printf("Could not open file %s\n", filepath.c_str());
        return false;
    }
    fp << opacity_scale << std::endl << range.x << " " << range.y << std::endl;
    fp << current_colormap.size() << std::endl;

    // Write the color map as binary data
    fp.write(reinterpret_cast<char*>(current_colormap.data()), current_colormap.size() * sizeof(uint8_t));

    //write the name of the colormap
    if (selected_colormap >= 0 && selected_colormap < colormaps.size())
        fp << colormaps[selected_colormap].name << std::endl;
    else
        fp << "custom" << std::endl;

    //write control point positions
    fp << alpha_control_pts.size() << std::endl;
    for (const auto &pt : alpha_control_pts) {
        fp << pt.x << " " << pt.y << std::endl;
    }
    fp.close();
    printf("Transferfunction written to file %s\n", filepath.c_str());
    return true;
}

bool TransferFunctionWidget::Changed() const
{
    return colormap_changed || opacity_scale_changed || range_changed;
}

bool TransferFunctionWidget::ColorMapChanged() const
{
    return colormap_changed;
}

bool TransferFunctionWidget::OpacityScaleChanged() const
{
    return opacity_scale_changed;
}

bool TransferFunctionWidget::RangeChanged() const
{
    return range_changed;
}

std::vector<uint8_t> TransferFunctionWidget::GetColormap()
{
    colormap_changed = false;
    return current_colormap;
}

std::vector<float> TransferFunctionWidget::GetColormapf()
{
    colormap_changed = false;
    std::vector<float> colormapf(current_colormap.size(), 0.f);
    for (size_t i = 0; i < current_colormap.size(); ++i) {
        colormapf[i] = current_colormap[i] / 255.f;
    }
    return colormapf;
}

void TransferFunctionWidget::GetColormapf(std::vector<float> &color,
                                           std::vector<float> &opacity)
{
    colormap_changed = false;
    color.resize((current_colormap.size() / 4) * 3);
    opacity.resize(current_colormap.size() / 4);
    for (size_t i = 0; i < current_colormap.size() / 4; ++i) {
        color[i * 3] = current_colormap[i * 4] / 255.f;
        color[i * 3 + 1] = current_colormap[i * 4 + 1] / 255.f;
        color[i * 3 + 2] = current_colormap[i * 4 + 2] / 255.f;
        opacity[i] = current_colormap[i * 4 + 3] / 255.f;
    }
}

float TransferFunctionWidget::GetOpacityScale()
{
    opacity_scale_changed = false;
    return opacity_scale;
}

ImVec2 TransferFunctionWidget::GetRange()
{
    range_changed = false;
    return range;
}

void TransferFunctionWidget::UpdateGPUImage()
{
    if(noGui)
    {
        std::cerr << "TransferFunctionWidget::UpdateGPUImage() called with noGui set to true\n";
        return;
    }
    GLint prev_tex_2d = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex_2d);

    if (colormap_img == (GLuint)-1) {
        glGenTextures(1, &colormap_img);
        glBindTexture(GL_TEXTURE_2D, colormap_img);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    if (gpu_image_stale) {
        gpu_image_stale = false;
        glBindTexture(GL_TEXTURE_2D, colormap_img);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGB8,
                     (GLsizei)(current_colormap.size() / 4),
                     1,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     current_colormap.data());
    }
    glBindTexture(GL_TEXTURE_2D, prev_tex_2d);
}

void TransferFunctionWidget::UpdateColormap()
{
    colormap_changed = true;
    gpu_image_stale = true;
    current_colormap = colormaps[selected_colormap].colormap;
    // We only change opacities for now, so go through and update the opacity
    // by blending between the neighboring control points
    auto a_it = alpha_control_pts.begin();
    const size_t npixels = current_colormap.size() / 4;
    for (size_t i = 0; i < npixels; ++i) {
        float x = static_cast<float>(i) / npixels;
        auto high = a_it + 1;
        if (x > high->x) {
            ++a_it;
            ++high;
        }
        float t = (x - a_it->x) / (high->x - a_it->x);
        float alpha = (1.f - t) * a_it->y + t * high->y;
        current_colormap[i * 4 + 3] = static_cast<uint8_t>(clamp(alpha * opacity_scale * 255.f, 0.f, 255.f));
    }
}

void TransferFunctionWidget::LoadEmbeddedPreset(const uint8_t *buf,
                                                  size_t size,
                                                  const std::string &name)
{
    int w, h, n;
    uint8_t *img_data = stbi_load_from_memory(buf, (int)size, &w, &h, &n, 4);
    auto img = std::vector<uint8_t>(img_data, img_data + w * 1 * 4);
    stbi_image_free(img_data);
    colormaps.emplace_back(name, img, SRGB);
    Colormap &cmap = colormaps.back();
    for (size_t i = 0; i < cmap.colormap.size() / 4; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            const float x = srgb_to_linear(cmap.colormap[i * 4 + j] / 255.f);
            cmap.colormap[i * 4 + j] = static_cast<uint8_t>(clamp(x * 255.f, 0.f, 255.f));
        }
    }
}

}
