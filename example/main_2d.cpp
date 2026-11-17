// Demo of the 2D (value x derivative magnitude) transfer function widget.
//
// A synthetic volume with three materials is ray marched on the GPU and
// classified with the 2D lookup table from TransferFunctionWidget2D:
//   - background (value 0)
//   - a large sphere (value 0.5)
//   - a smaller sphere (value 1.0) that is half inside the large sphere and
//     half outside it
// This gives three boundaries (0-0.5, 0.5-1 and 0-1), which show up as three
// arches in the histogram. The 0-1 boundary passes through value 0.5, so a 1D
// transfer function cannot separate it from the 0.5 material, but a triangle
// widget with a derivative threshold can.

#include <cmath>
#include <iostream>
#include <vector>
#include <SDL.h>
#include "gl_core_4_5.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl.h"
#include "shader.h"
#include "transfer_function_widget_2d.h"

const std::string raymarch_vs = R"(
#version 330 core

const vec2 pos[4] = vec2[4](vec2(-1, 1), vec2(-1, -1), vec2(1, 1), vec2(1, -1));

out vec2 ndc;

void main(void) {
	gl_Position = vec4(pos[gl_VertexID], 0.5, 1);
	ndc = pos[gl_VertexID];
}
)";

const std::string raymarch_fs = R"(
#version 330 core

// r: data value, g: derivative magnitude, both in data units
uniform sampler3D volume;
uniform sampler2D transfer_function;
uniform vec2 value_range;
uniform vec2 derivative_range;
uniform float yaw;
uniform float pitch;
uniform float aspect;
uniform float opacity_scale;
uniform int shading;

in vec2 ndc;

out vec4 color;

const float voxel_size = 2.0 / 128.0;

void main(void) {
	vec3 eye = 3.2 * vec3(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw));
	vec3 fwd = normalize(-eye);
	vec3 right = normalize(cross(fwd, vec3(0, 1, 0)));
	vec3 up = cross(right, fwd);
	vec3 dir = normalize(1.8 * fwd + ndc.x * aspect * right + ndc.y * up);

	vec3 background = mix(vec3(0.02), vec3(0.12), ndc.y * 0.5 + 0.5);

	// Intersect the [-1, 1]^3 volume box
	vec3 inv_dir = 1.0 / dir;
	vec3 t0 = (vec3(-1) - eye) * inv_dir;
	vec3 t1 = (vec3(1) - eye) * inv_dir;
	vec3 tmin = min(t0, t1);
	vec3 tmax = max(t0, t1);
	float t_enter = max(max(tmin.x, tmin.y), max(tmin.z, 0.0));
	float t_exit = min(min(tmax.x, tmax.y), tmax.z);
	if (t_enter >= t_exit) {
		color = vec4(background, 1);
		return;
	}

	float dt = 0.5 * voxel_size;
	vec4 acc = vec4(0);
	for (float t = t_enter; t < t_exit && acc.a < 0.99; t += dt) {
		vec3 uvw = (eye + t * dir) * 0.5 + 0.5;
		vec2 s = texture(volume, uvw).rg;
		vec2 tf_uv = vec2((s.r - value_range.x) / (value_range.y - value_range.x),
		                  (s.g - derivative_range.x) / (derivative_range.y - derivative_range.x));
		vec4 c = texture(transfer_function, tf_uv);
		if (c.a <= 0.0) {
			continue;
		}
		// The lookup table opacity is per voxel, correct it for the step size
		float a = 1.0 - pow(1.0 - clamp(c.a * opacity_scale, 0.0, 0.999), dt / voxel_size);
		if (shading == 1) {
			float h = 0.5 / 128.0;
			vec3 grad = vec3(
				texture(volume, uvw + vec3(h, 0, 0)).r - texture(volume, uvw - vec3(h, 0, 0)).r,
				texture(volume, uvw + vec3(0, h, 0)).r - texture(volume, uvw - vec3(0, h, 0)).r,
				texture(volume, uvw + vec3(0, 0, h)).r - texture(volume, uvw - vec3(0, 0, h)).r);
			if (dot(grad, grad) > 1e-10) {
				c.rgb *= 0.25 + 0.75 * abs(dot(normalize(grad), dir));
			}
		}
		acc.rgb += (1.0 - acc.a) * a * c.rgb;
		acc.a += (1.0 - acc.a) * a;
	}
	color = vec4(acc.rgb + (1.0 - acc.a) * background, 1);
}
)";

const int volume_dim = 128;

float smooth_inside(float x, float y, float z, float cx, float cy, float cz, float radius)
{
    const float dist =
        std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz)) - radius;
    // A smooth (blurred) boundary a few voxels wide, like a real scan
    return 0.5f - 0.5f * std::tanh(dist / 0.04f);
}

std::vector<float> make_volume()
{
    std::vector<float> volume(static_cast<size_t>(volume_dim) * volume_dim * volume_dim);
    for (int k = 0; k < volume_dim; ++k) {
        for (int j = 0; j < volume_dim; ++j) {
            for (int i = 0; i < volume_dim; ++i) {
                const float x = 2.f * (i + 0.5f) / volume_dim - 1.f;
                const float y = 2.f * (j + 0.5f) / volume_dim - 1.f;
                const float z = 2.f * (k + 0.5f) / volume_dim - 1.f;
                const float big = smooth_inside(x, y, z, -0.2f, 0.f, 0.f, 0.6f);
                const float small = smooth_inside(x, y, z, 0.4f, 0.1f, 0.f, 0.35f);
                volume[(static_cast<size_t>(k) * volume_dim + j) * volume_dim + i] =
                    small * 1.f + (1.f - small) * big * 0.5f;
            }
        }
    }
    return volume;
}

void run_app(SDL_Window *window);

int main(int, char **)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "Failed to init SDL: " << SDL_GetError() << "\n";
        return -1;
    }

    const char *glsl_version = "#version 330 core";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *window = SDL_CreateWindow("ImGui 2D Transfer Function Demo",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          1400,
                                          850,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    if (ogl_LoadFunctions() == ogl_LOAD_FAILED) {
        std::cerr << "Failed to initialize OpenGL\n";
        return 1;
    }

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    run_app(window);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

void run_app(SDL_Window *window)
{
    ImGuiIO &io = ImGui::GetIO();

    const std::vector<float> volume = make_volume();
    const int n = volume_dim;

    // The library leaves computing derivatives to the user. Here: the gradient
    // magnitude by central differences on the voxel grid.
    auto gradient_magnitude = [&volume, n](size_t index) {
        const int i = static_cast<int>(index % n);
        const int j = static_cast<int>((index / n) % n);
        const int k = static_cast<int>(index / (static_cast<size_t>(n) * n));
        auto at = [&](int x, int y, int z) {
            x = std::min(std::max(x, 0), n - 1);
            y = std::min(std::max(y, 0), n - 1);
            z = std::min(std::max(z, 0), n - 1);
            return volume[(static_cast<size_t>(z) * n + y) * n + x];
        };
        const float gx = 0.5f * (at(i + 1, j, k) - at(i - 1, j, k));
        const float gy = 0.5f * (at(i, j + 1, k) - at(i, j - 1, k));
        const float gz = 0.5f * (at(i, j, k + 1) - at(i, j, k - 1));
        return std::sqrt(gx * gx + gy * gy + gz * gz);
    };

    // The same derivatives as a precomputed array, for the setter path and for
    // the GPU volume texture
    std::vector<float> derivatives(volume.size());
    for (size_t i = 0; i < volume.size(); ++i) {
        derivatives[i] = gradient_magnitude(i);
    }

    ImTF::TransferFunctionWidget2D tfn_widget;
    tfn_widget.SetValues(volume);
    // Option 1: a lambda the widget evaluates per sample
    tfn_widget.SetDerivativeFunction(gradient_magnitude);
    int derivative_source = 0;

    // Volume texture with (value, derivative magnitude) per voxel
    std::vector<float> volume_rg(volume.size() * 2);
    for (size_t i = 0; i < volume.size(); ++i) {
        volume_rg[i * 2] = volume[i];
        volume_rg[i * 2 + 1] = derivatives[i];
    }
    GLuint volume_texture;
    glGenTextures(1, &volume_texture);
    glBindTexture(GL_TEXTURE_3D, volume_texture);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RG16F, n, n, n, 0, GL_RG, GL_FLOAT, volume_rg.data());

    // The 2D transfer function lookup table, filled in the render loop
    GLuint tf_texture;
    glGenTextures(1, &tf_texture);
    glBindTexture(GL_TEXTURE_2D, tf_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glDisable(GL_DEPTH_TEST);

    Shader raymarch(raymarch_vs, raymarch_fs);

    float yaw = 0.6f;
    float pitch = 0.35f;
    bool auto_rotate = false;
    bool shading = true;
    float opacity_scale = 1.f;

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                done = true;
            }
            if (!io.WantCaptureKeyboard && event.type == SDL_KEYDOWN &&
                event.key.keysym.sym == SDLK_ESCAPE) {
                done = true;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Left drag on the background orbits the camera
        if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            yaw -= io.MouseDelta.x * 0.01f;
            pitch = std::min(std::max(pitch + io.MouseDelta.y * 0.01f, -1.5f), 1.5f);
        }
        if (auto_rotate) {
            yaw += io.DeltaTime * 0.4f;
        }

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(500, 720), ImGuiCond_FirstUseEver);
        ImGui::Begin("2D Transfer Function");
        tfn_widget.DrawColorMap();
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 330, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320, 270), ImGuiCond_FirstUseEver);
        ImGui::Begin("Demo");
        ImGui::Text("%.2f ms/frame (%.1f FPS)", 1000.f / io.Framerate, io.Framerate);
        ImGui::TextWrapped("Drag on the background to rotate the volume.");
        ImGui::Separator();
        ImGui::Text("Derivatives supplied by:");
        if (ImGui::RadioButton("Lambda (SetDerivativeFunction)", derivative_source == 0)) {
            derivative_source = 0;
            tfn_widget.SetDerivativeFunction(gradient_magnitude);
        }
        if (ImGui::RadioButton("Array (SetDerivatives)", derivative_source == 1)) {
            derivative_source = 1;
            tfn_widget.SetDerivatives(derivatives);
        }
        ImGui::Separator();
        ImGui::SliderFloat("Opacity scale", &opacity_scale, 0.f, 2.f);
        ImGui::Checkbox("Shading", &shading);
        ImGui::Checkbox("Auto rotate", &auto_rotate);
        ImGui::Separator();
        ImGui::TextWrapped(
            "Try: raise the selected triangle's |f'| threshold to drop the 0.5 material "
            "interior and keep only the 0-1 boundary.");
        ImGui::End();

        if (tfn_widget.ColorMapChanged()) {
            const std::vector<uint8_t> lut = tfn_widget.GetColormap();
            const int res = tfn_widget.GetResolution();
            glBindTexture(GL_TEXTURE_2D, tf_texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, res, res, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         lut.data());
        }
        const ImVec2 value_range = tfn_widget.GetValueRange();
        const ImVec2 derivative_range = tfn_widget.GetDerivativeRange();

        ImGui::Render();
        int fb_width = 0, fb_height = 0;
        SDL_GL_GetDrawableSize(window, &fb_width, &fb_height);
        glViewport(0, 0, fb_width, fb_height);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(raymarch.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, volume_texture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tf_texture);
        glActiveTexture(GL_TEXTURE0);
        raymarch.uniform("volume", 0);
        raymarch.uniform("transfer_function", 1);
        glUniform2f(raymarch.uniforms["value_range"], value_range.x, value_range.y);
        glUniform2f(raymarch.uniforms["derivative_range"], derivative_range.x, derivative_range.y);
        raymarch.uniform("yaw", yaw);
        raymarch.uniform("pitch", pitch);
        raymarch.uniform("aspect", fb_height > 0 ? float(fb_width) / fb_height : 1.f);
        raymarch.uniform("opacity_scale", opacity_scale);
        raymarch.uniform("shading", shading ? 1 : 0);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    glDeleteTextures(1, &volume_texture);
    glDeleteTextures(1, &tf_texture);
    glDeleteVertexArrays(1, &vao);
}
