#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
#include "gl_core_4_5.h"
#include "imgui.h"

namespace ImTF {

// A simple 2D transfer function editor following the ideas in
//   J. Kniss, G. Kindlmann, C. Hansen, "Multidimensional Transfer Functions
//   for Interactive Volume Rendering", IEEE TVCG 8(3), 2002.
//
// The horizontal axis is the data value and the vertical axis is the magnitude
// of the first derivative (gradient magnitude). The editor background is the
// log-scaled joint histogram of (value, derivative). Material boundaries show
// up in it as arches, which the classification widgets are placed over.
//
// The widget does not compute derivatives. Supply them yourself, either as an
// array with SetDerivatives() or as a callback with SetDerivativeFunction().
class TransferFunctionWidget2D {
public:
    enum class Shape {
        // The triangle widget from the paper. Its apex sits on the zero
        // derivative axis and it widens with the derivative magnitude, so it
        // captures a whole boundary arch. Opacity falls off linearly from the
        // center line to the edges. Samples below `y_min` (a derivative
        // threshold) are excluded.
        Triangle,
        // An axis-aligned box of constant opacity.
        Box
    };

    // A classification widget. All coordinates are normalized to [0, 1], where
    // x is the data value and y is the derivative magnitude (see
    // GetValueRange() and GetDerivativeRange() to map back to data units).
    struct ClassificationWidget {
        Shape shape = Shape::Triangle;
        // Bounding box of the widget. For a triangle, the top edge spans
        // [x_min, x_max] at y_max, the apex is at ((x_min + x_max) / 2, 0)
        // and y_min is the derivative threshold.
        float x_min = 0.4f, x_max = 0.6f;
        float y_min = 0.f, y_max = 0.8f;
        // Linear RGB color and peak opacity
        float color[3] = {1.f, 1.f, 1.f};
        float opacity = 0.5f;
    };

    // Called once per sample with the sample index; returns the magnitude of the
    // first derivative (e.g. the gradient magnitude) at that sample.
    using DerivativeFunction = std::function<float(size_t index)>;

    // `resolution` is the size of the square 2D lookup table and histogram
    TransferFunctionWidget2D(bool noGui = false, int resolution = 256);
    ~TransferFunctionWidget2D();

    TransferFunctionWidget2D(const TransferFunctionWidget2D &) = delete;
    TransferFunctionWidget2D &operator=(const TransferFunctionWidget2D &) = delete;

    // Set the data values (e.g. voxel values) the histogram is built from.
    // The values are copied.
    void SetValues(const float *values, size_t count);
    void SetValues(const std::vector<float> &values);

    // Supply precomputed first-derivative magnitudes, one per value.
    // Replaces any derivative function set before.
    void SetDerivatives(const float *derivatives, size_t count);
    void SetDerivatives(const std::vector<float> &derivatives);

    // Supply a callback that computes the first-derivative magnitude of sample i.
    // It is evaluated for every sample the next time the histogram is built,
    // and again whenever SetValues() is called, so anything it captures by
    // reference must stay alive until then. Replaces any derivative array set
    // before.
    void SetDerivativeFunction(DerivativeFunction fn);

    // The data ranges mapped to [0, 1] on each axis. They are computed from the
    // data when the histogram is built unless they are set explicitly. Setting
    // a smaller derivative range is useful because derivatives tend to have a
    // long tail. Pass min >= max to go back to the automatic range.
    void SetValueRange(float min, float max);
    void SetDerivativeRange(float min, float max);
    ImVec2 GetValueRange();
    ImVec2 GetDerivativeRange();

    // Draw the editor into the currently active ImGui window
    void DrawColorMap(bool show_help = true);

    // True if the lookup table changed since the last call to GetColormap()
    bool ColorMapChanged() const;
    // True if the value or derivative range changed since it was last queried
    bool RangeChanged() const;
    bool Changed() const;

    // Width and height of the lookup table
    int GetResolution() const;

    // The 2D RGBA8 lookup table, resolution x resolution, row major. Column i
    // is the normalized value (i + 0.5) / resolution and row j is the
    // normalized derivative (j + 0.5) / resolution, so row 0 is zero
    // derivative. It can be uploaded as is to a GL_TEXTURE_2D and sampled
    // with texture(tf, vec2(value_normalized, derivative_normalized)).
    std::vector<uint8_t> GetColormap();
    // The same lookup table as RGBA32F
    std::vector<float> GetColormapf();

    // Evaluate the transfer function at a data value and derivative magnitude
    // given in data units. Returns linear RGB and opacity. Inputs outside the
    // ranges are clamped to the edges, like a GL_CLAMP_TO_EDGE texture.
    ImVec4 Sample(float value, float derivative);

    // Direct access to the classification widgets
    const std::vector<ClassificationWidget> &GetWidgets() const;
    void SetWidgets(const std::vector<ClassificationWidget> &widgets);
    void AddWidget(const ClassificationWidget &widget);
    void ClearWidgets();

    // Restore the default single triangle widget
    void Reset();

private:
    enum class DragMode { None, Body, Handle };

    struct Handle {
        float x, y;
        // Which edges of the bounding box this handle moves
        bool moves_x_min, moves_x_max, moves_y_min, moves_y_max;
    };

    bool noGui;
    int resolution;

    std::vector<float> values;
    std::vector<float> derivatives;
    DerivativeFunction derivative_fn;
    bool derivatives_from_fn = false;

    ImVec2 value_range = ImVec2(0.f, 1.f);
    ImVec2 derivative_range = ImVec2(0.f, 1.f);
    bool value_range_auto = true;
    bool derivative_range_auto = true;

    std::vector<ClassificationWidget> widgets;
    std::vector<uint8_t> lut;
    std::vector<uint8_t> histogram_img;

    int selected_widget = -1;
    DragMode drag_mode = DragMode::None;
    int drag_handle = -1;
    ImVec2 drag_start;
    ClassificationWidget drag_start_widget;

    bool histogram_stale = true;
    bool lut_stale = true;
    bool histogram_gpu_stale = true;
    bool lut_gpu_stale = true;
    bool colormap_changed = true;
    bool range_changed = true;

    GLuint histogram_tex = 0;
    GLuint lut_tex = 0;

    void InvalidateLUT();
    void RebuildHistogram();
    void RebuildLUT();
    void UpdateGPUImages();
    void DrawSelectedWidgetEditor();

    // Opacity of a single widget at the normalized point (x, y)
    static float WidgetOpacity(const ClassificationWidget &w, float x, float y);
    // Composite all widgets at the normalized point (x, y)
    ImVec4 EvaluateNormalized(float x, float y) const;
    static bool WidgetContains(const ClassificationWidget &w, float x, float y);
    static std::vector<Handle> WidgetHandles(const ClassificationWidget &w);
    static void ClampWidget(ClassificationWidget &w);
};

}
