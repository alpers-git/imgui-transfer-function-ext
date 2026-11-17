#include "transfer_function_widget_2d.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace ImTF {

namespace {

const float min_widget_size = 0.01f;
const float handle_radius = 6.f;

template <typename T>
T clamp(T x, T lo, T hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

ImTextureID to_texture_id(GLuint tex)
{
    return reinterpret_cast<ImTextureID>(static_cast<intptr_t>(tex));
}

GLuint create_texture()
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

}

TransferFunctionWidget2D::TransferFunctionWidget2D(bool noGui, int resolution)
    : noGui(noGui), resolution(std::max(resolution, 2))
{
    if (!noGui && ogl_LoadFunctions() == ogl_LOAD_FAILED) {
        std::cerr << "Failed to initialize OpenGL\n";
    }
    Reset();
}

TransferFunctionWidget2D::~TransferFunctionWidget2D()
{
    if (histogram_tex != 0) {
        glDeleteTextures(1, &histogram_tex);
    }
    if (lut_tex != 0) {
        glDeleteTextures(1, &lut_tex);
    }
}

void TransferFunctionWidget2D::SetValues(const float *data, size_t count)
{
    values.assign(data, data + count);
    if (derivatives_from_fn) {
        derivatives.clear();
    }
    histogram_stale = true;
}

void TransferFunctionWidget2D::SetValues(const std::vector<float> &data)
{
    SetValues(data.data(), data.size());
}

void TransferFunctionWidget2D::SetDerivatives(const float *data, size_t count)
{
    derivatives.assign(data, data + count);
    derivative_fn = nullptr;
    derivatives_from_fn = false;
    histogram_stale = true;
}

void TransferFunctionWidget2D::SetDerivatives(const std::vector<float> &data)
{
    SetDerivatives(data.data(), data.size());
}

void TransferFunctionWidget2D::SetDerivativeFunction(DerivativeFunction fn)
{
    derivative_fn = std::move(fn);
    derivatives_from_fn = static_cast<bool>(derivative_fn);
    derivatives.clear();
    histogram_stale = true;
}

void TransferFunctionWidget2D::SetValueRange(float min, float max)
{
    value_range_auto = !(min < max);
    if (!value_range_auto) {
        value_range = ImVec2(min, max);
    }
    histogram_stale = true;
    range_changed = true;
}

void TransferFunctionWidget2D::SetDerivativeRange(float min, float max)
{
    derivative_range_auto = !(min < max);
    if (!derivative_range_auto) {
        derivative_range = ImVec2(min, max);
    }
    histogram_stale = true;
    range_changed = true;
}

ImVec2 TransferFunctionWidget2D::GetValueRange()
{
    if (histogram_stale) {
        RebuildHistogram();
    }
    range_changed = false;
    return value_range;
}

ImVec2 TransferFunctionWidget2D::GetDerivativeRange()
{
    if (histogram_stale) {
        RebuildHistogram();
    }
    range_changed = false;
    return derivative_range;
}

bool TransferFunctionWidget2D::ColorMapChanged() const
{
    return colormap_changed;
}

bool TransferFunctionWidget2D::RangeChanged() const
{
    return range_changed;
}

bool TransferFunctionWidget2D::Changed() const
{
    return colormap_changed || range_changed;
}

int TransferFunctionWidget2D::GetResolution() const
{
    return resolution;
}

std::vector<uint8_t> TransferFunctionWidget2D::GetColormap()
{
    if (lut_stale) {
        RebuildLUT();
    }
    colormap_changed = false;
    return lut;
}

std::vector<float> TransferFunctionWidget2D::GetColormapf()
{
    if (lut_stale) {
        RebuildLUT();
    }
    colormap_changed = false;
    std::vector<float> lutf(lut.size());
    for (size_t i = 0; i < lut.size(); ++i) {
        lutf[i] = lut[i] / 255.f;
    }
    return lutf;
}

ImVec4 TransferFunctionWidget2D::Sample(float value, float derivative)
{
    if (histogram_stale) {
        RebuildHistogram();
    }
    const float x = (value - value_range.x) / (value_range.y - value_range.x);
    const float y = (derivative - derivative_range.x) / (derivative_range.y - derivative_range.x);
    return EvaluateNormalized(clamp(x, 0.f, 1.f), clamp(y, 0.f, 1.f));
}

const std::vector<TransferFunctionWidget2D::ClassificationWidget> &
TransferFunctionWidget2D::GetWidgets() const
{
    return widgets;
}

void TransferFunctionWidget2D::SetWidgets(const std::vector<ClassificationWidget> &new_widgets)
{
    widgets = new_widgets;
    for (auto &w : widgets) {
        ClampWidget(w);
    }
    selected_widget = -1;
    drag_mode = DragMode::None;
    InvalidateLUT();
}

void TransferFunctionWidget2D::AddWidget(const ClassificationWidget &widget)
{
    widgets.push_back(widget);
    ClampWidget(widgets.back());
    InvalidateLUT();
}

void TransferFunctionWidget2D::ClearWidgets()
{
    SetWidgets({});
}

void TransferFunctionWidget2D::Reset()
{
    ClassificationWidget w;
    w.shape = Shape::Triangle;
    w.x_min = 0.4f;
    w.x_max = 0.6f;
    w.y_min = 0.f;
    w.y_max = 0.8f;
    w.color[0] = 1.f;
    w.color[1] = 0.6f;
    w.color[2] = 0.2f;
    w.opacity = 0.6f;
    SetWidgets({w});
}

void TransferFunctionWidget2D::InvalidateLUT()
{
    lut_stale = true;
    colormap_changed = true;
}

void TransferFunctionWidget2D::RebuildHistogram()
{
    histogram_stale = false;
    histogram_gpu_stale = true;
    // Opaque black image, filled in below
    histogram_img.assign(static_cast<size_t>(resolution) * resolution * 4, 0);
    for (size_t i = 3; i < histogram_img.size(); i += 4) {
        histogram_img[i] = 255;
    }

    if (values.empty()) {
        return;
    }
    if (derivatives_from_fn && derivatives.size() != values.size()) {
        derivatives.resize(values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            derivatives[i] = derivative_fn(i);
        }
    }
    if (derivatives.size() != values.size()) {
        std::cerr << "TransferFunctionWidget2D: got " << values.size() << " values but "
                  << derivatives.size()
                  << " derivatives, supply one derivative per value with SetDerivatives() or "
                     "SetDerivativeFunction()\n";
        return;
    }

    const ImVec2 prev_value_range = value_range;
    const ImVec2 prev_derivative_range = derivative_range;
    if (value_range_auto || derivative_range_auto) {
        float vmin = INFINITY, vmax = -INFINITY;
        float dmin = 0.f, dmax = -INFINITY;
        for (size_t i = 0; i < values.size(); ++i) {
            if (!std::isfinite(values[i]) || !std::isfinite(derivatives[i])) {
                continue;
            }
            vmin = std::min(vmin, values[i]);
            vmax = std::max(vmax, values[i]);
            dmin = std::min(dmin, derivatives[i]);
            dmax = std::max(dmax, derivatives[i]);
        }
        if (value_range_auto && vmin <= vmax) {
            value_range = ImVec2(vmin, vmax > vmin ? vmax : vmin + 1.f);
        }
        if (derivative_range_auto && dmin <= dmax) {
            derivative_range = ImVec2(dmin, dmax > dmin ? dmax : dmin + 1.f);
        }
    }
    if (prev_value_range.x != value_range.x || prev_value_range.y != value_range.y ||
        prev_derivative_range.x != derivative_range.x ||
        prev_derivative_range.y != derivative_range.y) {
        range_changed = true;
    }

    // Joint histogram of (value, derivative). Samples outside the ranges are
    // skipped rather than piled up on the edges.
    std::vector<uint32_t> counts(static_cast<size_t>(resolution) * resolution, 0);
    const float value_scale = resolution / (value_range.y - value_range.x);
    const float derivative_scale = resolution / (derivative_range.y - derivative_range.x);
    uint32_t max_count = 0;
    for (size_t i = 0; i < values.size(); ++i) {
        const float fx = (values[i] - value_range.x) * value_scale;
        const float fy = (derivatives[i] - derivative_range.x) * derivative_scale;
        // Written so that NaNs fail the test
        if (!(fx >= 0.f && fx <= resolution && fy >= 0.f && fy <= resolution)) {
            continue;
        }
        const int bx = std::min(static_cast<int>(fx), resolution - 1);
        const int by = std::min(static_cast<int>(fy), resolution - 1);
        max_count = std::max(max_count, ++counts[static_cast<size_t>(by) * resolution + bx]);
    }
    if (max_count == 0) {
        return;
    }

    // Log scale so that the sparse boundary arches are visible next to the
    // dense material peaks at zero derivative
    const float inv_log_max = 1.f / std::log(1.f + max_count);
    for (size_t i = 0; i < counts.size(); ++i) {
        const float c = std::log(1.f + counts[i]) * inv_log_max;
        const uint8_t g = static_cast<uint8_t>(clamp(c * 255.f, 0.f, 255.f));
        histogram_img[i * 4] = g;
        histogram_img[i * 4 + 1] = g;
        histogram_img[i * 4 + 2] = g;
    }
}

void TransferFunctionWidget2D::RebuildLUT()
{
    lut_stale = false;
    lut_gpu_stale = true;
    colormap_changed = true;
    lut.resize(static_cast<size_t>(resolution) * resolution * 4);
    for (int j = 0; j < resolution; ++j) {
        const float y = (j + 0.5f) / resolution;
        for (int i = 0; i < resolution; ++i) {
            const float x = (i + 0.5f) / resolution;
            const ImVec4 c = EvaluateNormalized(x, y);
            uint8_t *px = &lut[(static_cast<size_t>(j) * resolution + i) * 4];
            px[0] = static_cast<uint8_t>(clamp(c.x * 255.f, 0.f, 255.f));
            px[1] = static_cast<uint8_t>(clamp(c.y * 255.f, 0.f, 255.f));
            px[2] = static_cast<uint8_t>(clamp(c.z * 255.f, 0.f, 255.f));
            px[3] = static_cast<uint8_t>(clamp(c.w * 255.f, 0.f, 255.f));
        }
    }
}

float TransferFunctionWidget2D::WidgetOpacity(const ClassificationWidget &w, float x, float y)
{
    if (y < w.y_min || y > w.y_max) {
        return 0.f;
    }
    if (w.shape == Shape::Box) {
        return (x >= w.x_min && x <= w.x_max) ? w.opacity : 0.f;
    }
    // Triangle: the half width grows linearly from 0 at the apex (y = 0) to
    // half the top edge at y_max, and opacity falls off linearly from the
    // center line to the edges
    const float center = 0.5f * (w.x_min + w.x_max);
    const float half_width = 0.5f * (w.x_max - w.x_min) * (y / w.y_max);
    if (half_width <= 0.f) {
        return 0.f;
    }
    const float d = std::abs(x - center) / half_width;
    return d < 1.f ? w.opacity * (1.f - d) : 0.f;
}

ImVec4 TransferFunctionWidget2D::EvaluateNormalized(float x, float y) const
{
    // Overlapping widgets combine like stacked semi-transparent layers: the
    // opacities are composited and the colors are averaged by opacity.
    float transparency = 1.f;
    float weight = 0.f;
    float r = 0.f, g = 0.f, b = 0.f;
    for (const auto &w : widgets) {
        const float a = WidgetOpacity(w, x, y);
        if (a <= 0.f) {
            continue;
        }
        transparency *= 1.f - a;
        weight += a;
        r += a * w.color[0];
        g += a * w.color[1];
        b += a * w.color[2];
    }
    if (weight <= 0.f) {
        return ImVec4(0.f, 0.f, 0.f, 0.f);
    }
    return ImVec4(r / weight, g / weight, b / weight, 1.f - transparency);
}

bool TransferFunctionWidget2D::WidgetContains(const ClassificationWidget &w, float x, float y)
{
    if (y < w.y_min || y > w.y_max || x < w.x_min || x > w.x_max) {
        return false;
    }
    if (w.shape == Shape::Box) {
        return true;
    }
    const float center = 0.5f * (w.x_min + w.x_max);
    const float half_width = 0.5f * (w.x_max - w.x_min) * (y / w.y_max);
    return std::abs(x - center) <= half_width;
}

std::vector<TransferFunctionWidget2D::Handle> TransferFunctionWidget2D::WidgetHandles(
    const ClassificationWidget &w)
{
    if (w.shape == Shape::Triangle) {
        return {
            // Top corners set the width and height
            {w.x_min, w.y_max, true, false, false, true},
            {w.x_max, w.y_max, false, true, false, true},
            // On the center line: the derivative threshold
            {0.5f * (w.x_min + w.x_max), w.y_min, false, false, true, false},
        };
    }
    return {
        {w.x_min, w.y_max, true, false, false, true},
        {w.x_max, w.y_max, false, true, false, true},
        {w.x_min, w.y_min, true, false, true, false},
        {w.x_max, w.y_min, false, true, true, false},
    };
}

void TransferFunctionWidget2D::ClampWidget(ClassificationWidget &w)
{
    w.x_min = clamp(w.x_min, 0.f, 1.f - min_widget_size);
    w.x_max = clamp(w.x_max, w.x_min + min_widget_size, 1.f);
    w.y_min = clamp(w.y_min, 0.f, 1.f - min_widget_size);
    w.y_max = clamp(w.y_max, w.y_min + min_widget_size, 1.f);
    w.opacity = clamp(w.opacity, 0.f, 1.f);
    for (float &c : w.color) {
        c = clamp(c, 0.f, 1.f);
    }
}

void TransferFunctionWidget2D::UpdateGPUImages()
{
    GLint prev_tex_2d = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex_2d);

    if (histogram_tex == 0) {
        histogram_tex = create_texture();
    }
    if (lut_tex == 0) {
        lut_tex = create_texture();
    }
    if (histogram_gpu_stale) {
        histogram_gpu_stale = false;
        glBindTexture(GL_TEXTURE_2D, histogram_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, resolution, resolution, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, histogram_img.data());
    }
    if (lut_gpu_stale) {
        lut_gpu_stale = false;
        // Display copy of the lookup table. Opacity is boosted so that low
        // opacity regions stay visible over the histogram.
        std::vector<uint8_t> display(lut);
        for (size_t i = 3; i < display.size(); i += 4) {
            display[i] = static_cast<uint8_t>(std::sqrt(display[i] / 255.f) * 0.85f * 255.f);
        }
        glBindTexture(GL_TEXTURE_2D, lut_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, resolution, resolution, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, display.data());
    }
    glBindTexture(GL_TEXTURE_2D, prev_tex_2d);
}

void TransferFunctionWidget2D::DrawColorMap(bool show_help)
{
    if (noGui) {
        std::cerr << "TransferFunctionWidget2D::DrawColorMap() called with noGui set to true\n";
        return;
    }
    ImGui::PushID(this);

    if (show_help) {
        ImGui::Text("2D Transfer Function (value x derivative magnitude)");
        ImGui::TextWrapped(
            "Click a widget to select it, drag to move it and drag its handles to resize it. "
            "Double-click empty space to add a triangle, right-click a widget to remove it.");
    }

    ClassificationWidget new_widget;
    new_widget.color[0] = 0.3f;
    new_widget.color[1] = 0.7f;
    new_widget.color[2] = 1.f;
    if (ImGui::Button("Add triangle")) {
        new_widget.shape = Shape::Triangle;
        AddWidget(new_widget);
        selected_widget = static_cast<int>(widgets.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add box")) {
        new_widget.shape = Shape::Box;
        new_widget.y_min = 0.3f;
        new_widget.y_max = 0.6f;
        AddWidget(new_widget);
        selected_widget = static_cast<int>(widgets.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        Reset();
    }

    if (histogram_stale) {
        RebuildHistogram();
    }

    const ImGuiIO &io = ImGui::GetIO();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 canvas_size(std::max(avail.x, 64.f),
                             std::max(150.f, std::min(avail.x * 0.75f, avail.y * 0.6f)));
    const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();

    auto to_screen = [&](float x, float y) {
        return ImVec2(canvas_pos.x + x * canvas_size.x, canvas_pos.y + (1.f - y) * canvas_size.y);
    };
    auto to_normalized = [&](const ImVec2 &p) {
        return ImVec2((p.x - canvas_pos.x) / canvas_size.x,
                      1.f - (p.y - canvas_pos.y) / canvas_size.y);
    };

    ImGui::InvisibleButton("tf2d_canvas", canvas_size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 mouse = to_normalized(io.MousePos);

    auto handle_under_mouse = [&](int widget) {
        if (widget < 0 || widget >= static_cast<int>(widgets.size())) {
            return -1;
        }
        const auto handles = WidgetHandles(widgets[widget]);
        for (size_t i = 0; i < handles.size(); ++i) {
            const ImVec2 p = to_screen(handles[i].x, handles[i].y);
            const float dx = p.x - io.MousePos.x;
            const float dy = p.y - io.MousePos.y;
            if (dx * dx + dy * dy <= (handle_radius + 2.f) * (handle_radius + 2.f)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    // The last widget is drawn on top, so test it first
    auto widget_under_mouse = [&]() {
        for (int i = static_cast<int>(widgets.size()) - 1; i >= 0; --i) {
            if (WidgetContains(widgets[i], mouse.x, mouse.y)) {
                return i;
            }
        }
        return -1;
    };

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        drag_mode = DragMode::None;
        const int handle = handle_under_mouse(selected_widget);
        if (handle >= 0) {
            drag_mode = DragMode::Handle;
            drag_handle = handle;
        } else {
            selected_widget = widget_under_mouse();
            if (selected_widget >= 0) {
                drag_mode = DragMode::Body;
            } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                new_widget.shape = Shape::Triangle;
                const float half_width = 0.5f * (new_widget.x_max - new_widget.x_min);
                new_widget.x_min = clamp(mouse.x, half_width, 1.f - half_width) - half_width;
                new_widget.x_max = new_widget.x_min + 2.f * half_width;
                AddWidget(new_widget);
                selected_widget = static_cast<int>(widgets.size()) - 1;
            }
        }
        if (drag_mode != DragMode::None) {
            drag_start = mouse;
            drag_start_widget = widgets[selected_widget];
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const int widget = widget_under_mouse();
        if (widget >= 0) {
            widgets.erase(widgets.begin() + widget);
            if (selected_widget == widget) {
                selected_widget = -1;
            } else if (selected_widget > widget) {
                --selected_widget;
            }
            drag_mode = DragMode::None;
            InvalidateLUT();
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        drag_mode = DragMode::None;
    }
    if (active && drag_mode != DragMode::None && selected_widget >= 0 &&
        selected_widget < static_cast<int>(widgets.size())) {
        const float dx = mouse.x - drag_start.x;
        const float dy = mouse.y - drag_start.y;
        ClassificationWidget w = drag_start_widget;
        if (drag_mode == DragMode::Body) {
            const float sx = clamp(dx, -w.x_min, 1.f - w.x_max);
            w.x_min += sx;
            w.x_max += sx;
            // A triangle's apex stays on the zero derivative axis, so it only
            // moves along the value axis
            if (w.shape == Shape::Box) {
                const float sy = clamp(dy, -w.y_min, 1.f - w.y_max);
                w.y_min += sy;
                w.y_max += sy;
            }
        } else {
            const Handle h = WidgetHandles(drag_start_widget)[drag_handle];
            if (h.moves_x_min) {
                w.x_min = clamp(w.x_min + dx, 0.f, w.x_max - min_widget_size);
            }
            if (h.moves_x_max) {
                w.x_max = clamp(w.x_max + dx, w.x_min + min_widget_size, 1.f);
            }
            if (h.moves_y_min) {
                w.y_min = clamp(w.y_min + dy, 0.f, w.y_max - min_widget_size);
            }
            if (h.moves_y_max) {
                w.y_max = clamp(w.y_max + dy, w.y_min + min_widget_size, 1.f);
            }
        }
        ClassificationWidget &cur = widgets[selected_widget];
        if (cur.x_min != w.x_min || cur.x_max != w.x_max || cur.y_min != w.y_min ||
            cur.y_max != w.y_max) {
            cur.x_min = w.x_min;
            cur.x_max = w.x_max;
            cur.y_min = w.y_min;
            cur.y_max = w.y_max;
            InvalidateLUT();
        }
    }

    if (lut_stale) {
        RebuildLUT();
    }
    UpdateGPUImages();

    // Histogram, then the transfer function on top of it
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImVec2 canvas_max(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y);
    draw_list->PushClipRect(canvas_pos, canvas_max, true);
    // Row 0 of the textures is zero derivative, which goes at the bottom
    draw_list->AddImage(
        to_texture_id(histogram_tex), canvas_pos, canvas_max, ImVec2(0.f, 1.f), ImVec2(1.f, 0.f));
    draw_list->AddImage(
        to_texture_id(lut_tex), canvas_pos, canvas_max, ImVec2(0.f, 1.f), ImVec2(1.f, 0.f));

    // Widget outlines
    for (size_t i = 0; i < widgets.size(); ++i) {
        const ClassificationWidget &w = widgets[i];
        const bool selected = static_cast<int>(i) == selected_widget;
        const ImU32 color =
            selected ? IM_COL32(255, 255, 255, 255)
                     : ImGui::ColorConvertFloat4ToU32(
                           ImVec4(w.color[0], w.color[1], w.color[2], 1.f));
        const float thickness = selected ? 2.f : 1.5f;
        if (w.shape == Shape::Box) {
            draw_list->AddRect(to_screen(w.x_min, w.y_max), to_screen(w.x_max, w.y_min), color, 0.f,
                               0, thickness);
        } else {
            const float center = 0.5f * (w.x_min + w.x_max);
            const float bottom_half_width = 0.5f * (w.x_max - w.x_min) * (w.y_min / w.y_max);
            const ImVec2 outline[4] = {to_screen(center - bottom_half_width, w.y_min),
                                       to_screen(w.x_min, w.y_max),
                                       to_screen(w.x_max, w.y_max),
                                       to_screen(center + bottom_half_width, w.y_min)};
            if (w.y_min > 0.f) {
                // The part cut off by the derivative threshold
                const ImU32 faint = (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 90);
                const ImVec2 apex = to_screen(center, 0.f);
                draw_list->AddLine(apex, outline[0], faint, 1.f);
                draw_list->AddLine(apex, outline[3], faint, 1.f);
                draw_list->AddPolyline(outline, 4, color, ImDrawFlags_Closed, thickness);
            } else {
                const ImVec2 tri[3] = {outline[0], outline[1], outline[2]};
                draw_list->AddPolyline(tri, 3, color, ImDrawFlags_Closed, thickness);
            }
        }
        if (selected) {
            for (const Handle &h : WidgetHandles(w)) {
                const ImVec2 p = to_screen(h.x, h.y);
                draw_list->AddRectFilled(ImVec2(p.x - handle_radius, p.y - handle_radius),
                                         ImVec2(p.x + handle_radius, p.y + handle_radius),
                                         IM_COL32(255, 255, 255, 255));
                draw_list->AddRect(ImVec2(p.x - handle_radius, p.y - handle_radius),
                                   ImVec2(p.x + handle_radius, p.y + handle_radius),
                                   IM_COL32(0, 0, 0, 255));
            }
        }
    }
    draw_list->PopClipRect();
    draw_list->AddRect(canvas_pos, canvas_max, IM_COL32(180, 180, 180, 255));

    // Axis readout
    if (hovered) {
        const float v = value_range.x + clamp(mouse.x, 0.f, 1.f) * (value_range.y - value_range.x);
        const float d = derivative_range.x +
                        clamp(mouse.y, 0.f, 1.f) * (derivative_range.y - derivative_range.x);
        ImGui::Text("value %.4g   |f'| %.4g", v, d);
    } else {
        ImGui::Text("value [%.4g, %.4g]   |f'| [%.4g, %.4g]", value_range.x, value_range.y,
                    derivative_range.x, derivative_range.y);
    }

    DrawSelectedWidgetEditor();
    ImGui::PopID();
}

void TransferFunctionWidget2D::DrawSelectedWidgetEditor()
{
    if (selected_widget < 0 || selected_widget >= static_cast<int>(widgets.size())) {
        return;
    }
    ClassificationWidget &w = widgets[selected_widget];
    ImGui::Separator();
    ImGui::Text("Selected %s widget", w.shape == Shape::Triangle ? "triangle" : "box");
    if (ImGui::ColorEdit3("Color", w.color)) {
        InvalidateLUT();
    }
    if (ImGui::SliderFloat("Opacity", &w.opacity, 0.f, 1.f)) {
        InvalidateLUT();
    }

    // Edit the geometry in data units
    const float vspan = value_range.y - value_range.x;
    const float dspan = derivative_range.y - derivative_range.x;
    float vals[2] = {value_range.x + w.x_min * vspan, value_range.x + w.x_max * vspan};
    float ders[2] = {derivative_range.x + w.y_min * dspan, derivative_range.x + w.y_max * dspan};
    bool edited = false;
    if (ImGui::DragFloat2(w.shape == Shape::Triangle ? "Top edge value" : "Value range", vals,
                          vspan * 0.002f, value_range.x, value_range.y, "%.4g")) {
        w.x_min = (vals[0] - value_range.x) / vspan;
        w.x_max = (vals[1] - value_range.x) / vspan;
        edited = true;
    }
    if (ImGui::DragFloat2(w.shape == Shape::Triangle ? "|f'| threshold, top" : "|f'| range",
                          ders, dspan * 0.002f, derivative_range.x, derivative_range.y, "%.4g")) {
        w.y_min = (ders[0] - derivative_range.x) / dspan;
        w.y_max = (ders[1] - derivative_range.x) / dspan;
        edited = true;
    }
    if (edited) {
        ClampWidget(w);
        InvalidateLUT();
    }

    if (ImGui::Button("Delete widget")) {
        widgets.erase(widgets.begin() + selected_widget);
        selected_widget = -1;
        drag_mode = DragMode::None;
        InvalidateLUT();
    }
}

}
