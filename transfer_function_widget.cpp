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
