# ImGui Transfer Function

A transfer function widget implemented with ImGui. Supports loading in
arbitrary 1D RGBA8 images, which can then be used as color palettes.
The widget is used to modify the alpha channel as desired to modify
the transfer function.

## Use

Add the transfer function widget C++ and header files to your project,
along with the embedded presets header `embedded_colormaps.h`.
If you're not already using `stbi_image.h` add that file as well,
otherwise you can define `TFN_WIDGET_NO_STB_IMAGE_IMPL` to prevent
the transfer function widget C++ file from setting `STB_IMAGE_IMPLEMENTATION`.
You can also add `gl_core_4_5.h` and `gl_core_4_5.c` to your project,
or swap them for your preferred OpenGL function loader.

If you're not using OpenGL, you'll need to modify `TransferFunctionWidget::update_gpu_image`
to use the right API, and change how the image is passed to ImGui
to match what the ImGui backend expects in `TransferFunctionWidget::draw_ui`.

The widget includes some embedded color palette presets: ParaView's Cool Warm,
Rainbow, Matplotlib Plasma and Virdis, and
[Francesca Samsel's](https://sciviscolor.org/home/colormaps/) Linear Green
and Linear YGB 1211G colormaps. To load additional palettes you can add
colormaps with `TransferFunctionWidget::add_colormap`, which takes a `Colormap`.
The Colormap image should be a 1D RGBA8 image. 

## 2D Transfer Function

`transfer_function_widget_2d.h/.cpp` add `ImTF::TransferFunctionWidget2D`, a
simple 2D transfer function editor following Kniss, Kindlmann and Hansen,
"Multidimensional Transfer Functions for Interactive Volume Rendering"
(IEEE TVCG 2002). The horizontal axis is the data value and the vertical axis
is the first derivative (gradient) magnitude, drawn over their log-scaled joint
histogram. Material boundaries show up in the histogram as arches. You place
two kinds of classification widget over them:

- **Triangle** (from the paper): the apex sits on the zero-derivative axis and
  opacity falls off linearly from the center line. A derivative threshold cuts
  off its lower part, which removes material interiors.
- **Box**: constant opacity over a value and derivative range.

The widget does not compute derivatives. You supply them yourself:

```cpp
ImTF::TransferFunctionWidget2D tf;
tf.SetValues(voxels);                       // std::vector<float> or pointer + count

// Either a callback, evaluated once per sample...
tf.SetDerivativeFunction([&](size_t i) { return my_gradient_magnitude(i); });
// ...or a precomputed array with one derivative per value
tf.SetDerivatives(gradient_magnitudes);

// In the UI
tf.DrawColorMap();

// For rendering: an RGBA8 resolution x resolution table, sampled with
// texture(tf, vec2(value_normalized, derivative_normalized)), normalized with
// GetValueRange() and GetDerivativeRange()
if (tf.ColorMapChanged()) upload(tf.GetColormap(), tf.GetResolution());
```

`Sample(value, derivative)` evaluates the transfer function on the CPU.
`SetDerivativeRange()` narrows the derivative axis when derivatives have a long tail.
`DrawOpacityScale()` edits an opacity scale that is returned by `GetOpacityScale()` for the
application to apply; it is not baked into the lookup table.

Like the 1D widget, `SaveState(path)` writes the opacity scale, the value and derivative
ranges and the classification widgets to a text file, and `LoadState(path)` reads them back.
Automatic ranges are saved as the ranges they resolved to, so set the values and derivatives
before saving.

## Example

See the [example/](example/) for an example use case of the widget
with OpenGL. Additional image files can be passed as arguments to load additional
presets beyond the included presets, the resulting colormap is drawn
with alpha blending as the window background. The example requires SDL2
which is found through CMake, if it fails to find it you can specify the
root directory of you SDL2 by passing `-DSDL2_DIR=<path>` when running CMake.

![Example image](https://i.imgur.com/piHEPEl.png)

