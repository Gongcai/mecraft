#include "DefaultShaderCatalog.h"

#include "Paths.h"
#include "ShaderLibrary.h"

namespace {

struct ShaderDefinition {
    const char* name;
    const char* vertexPath;
    const char* fragmentPath;
};

constexpr ShaderDefinition kDefaultShaders[] = {
    {"shadow_depth", SHADERS_DIR "/shadow_depth.vert", SHADERS_DIR "/shadow_depth.frag"},
    {"deferred_lighting", SHADERS_DIR "/deferred_lighting.vert", SHADERS_DIR "/deferred_lighting.frag"},
    {"scene_composite", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/scene_composite.frag"},
    {"deferred_debug", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/deferred_debug.frag"},
    {"ssao", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/ssao.frag"},
    {"velocity_resolve", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/velocity_resolve.frag"},
    {"volumetric_fog", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/volumetric_fog.frag"},
    {"volumetric_temporal", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/volumetric_temporal.frag"},
    {"volumetric_composite", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/volumetric_composite.frag"},
    {"reflection_probe", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/reflection_probe.frag"},
    {"cloud_target", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/cloud_target.frag"},
    {"temporal_resolve", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/temporal_resolve.frag"},
    {"reflection_filter", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/reflection_filter.frag"},
    {"ssao_filter", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/ssao_filter.frag"},
    {"ssao_temporal", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/ssao_temporal.frag"},
    {"ssao_upsample", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/ssao_upsample.frag"},
    {"ssgi", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/ssgi.frag"},
    {"ssgi_upsample", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/ssgi_upsample.frag"},
    {"ssgi_denoise", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/ssgi_denoise.frag"},
    {"ssgi_temporal", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/ssgi_temporal.frag"},
    {"motion_blur", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/motion_blur.frag"},
    {"dof", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/dof.frag"},
    {"bloom_extract", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/bloom_extract.frag"},
    {"bloom_blur", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/bloom_blur.frag"},
    {"exposure_downsample", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/exposure_downsample.frag"},
    {"exposure_resolve", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/exposure_resolve.frag"},
    {"fsr1_easu", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/fsr1_easu.frag"},
    {"fsr1_rcas", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/fsr1_rcas.frag"},
    {"outline", SHADERS_DIR "/outline.vert", SHADERS_DIR "/outline.frag"},
    {"break_overlay", SHADERS_DIR "/break_overlay.vert", SHADERS_DIR "/break_overlay.frag"},
    {"crosshair", SHADERS_DIR "/crosshair.vert", SHADERS_DIR "/crosshair.frag"},
    {"inventory", SHADERS_DIR "/inventory.vert", SHADERS_DIR "/inventory.frag"},
    {"text", SHADERS_DIR "/text.vert", SHADERS_DIR "/text.frag"},
    {"rain", SHADERS_DIR "/rain.vert", SHADERS_DIR "/rain.frag"},
    {"postprocess", SHADERS_DIR "/postprocess.vert", SHADERS_DIR "/postprocess.frag"},
    {"ui_color", SHADERS_DIR "/ui_color.vert", SHADERS_DIR "/ui_color.frag"},
    {"ui_glass", SHADERS_DIR "/ui_glass.vert", SHADERS_DIR "/ui_glass.frag"},
};

} // namespace

namespace resource {

void loadDefaultShaders(ShaderLibrary& shaders) {
    for (const ShaderDefinition& shader : kDefaultShaders) {
        shaders.load(shader.name, shader.vertexPath, shader.fragmentPath);
    }
}

} // namespace resource
