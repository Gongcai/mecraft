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
    {"chunk", SHADERS_DIR "/chunk.vert", SHADERS_DIR "/chunk.frag"},
    {"chunk_lit", SHADERS_DIR "/chunk_lit.vert", SHADERS_DIR "/chunk_lit.frag"},
    {"transparent_composite", SHADERS_DIR "/chunk_lit.vert", SHADERS_DIR "/transparent_composite.frag"},
    {"water_composite", SHADERS_DIR "/chunk_lit.vert", SHADERS_DIR "/water_composite.frag"},
    {"block_item_lit", SHADERS_DIR "/chunk_lit.vert", SHADERS_DIR "/block_item_lit.frag"},
    {"block_item_forward", SHADERS_DIR "/chunk_lit.vert", SHADERS_DIR "/block_item_forward.frag"},
    {"forward_basic_terrain", SHADERS_DIR "/chunk_lit.vert", SHADERS_DIR "/forward_basic_terrain.frag"},
    {"chunk_gbuffer", SHADERS_DIR "/chunk_gbuffer.vert", SHADERS_DIR "/chunk_gbuffer.frag"},
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
    {"motion_blur", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/motion_blur.frag"},
    {"dof", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/dof.frag"},
    {"bloom_extract", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/bloom_extract.frag"},
    {"bloom_blur", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/bloom_blur.frag"},
    {"exposure_downsample", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/exposure_downsample.frag"},
    {"exposure_resolve", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/exposure_resolve.frag"},
    {"fsr1_easu", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/fsr1_easu.frag"},
    {"fsr1_rcas", SHADERS_DIR "/fullscreen_triangle.vert", SHADERS_DIR "/fsr1_rcas.frag"},
    {"drop_block", SHADERS_DIR "/drop_block.vert", SHADERS_DIR "/drop_block.frag"},
    {"drop_block_forward", SHADERS_DIR "/drop_block.vert", SHADERS_DIR "/drop_block_forward.frag"},
    {"outline", SHADERS_DIR "/outline.vert", SHADERS_DIR "/outline.frag"},
    {"break_overlay", SHADERS_DIR "/break_overlay.vert", SHADERS_DIR "/break_overlay.frag"},
    {"crosshair", SHADERS_DIR "/crosshair.vert", SHADERS_DIR "/crosshair.frag"},
    {"inventory", SHADERS_DIR "/inventory.vert", SHADERS_DIR "/inventory.frag"},
    {"text", SHADERS_DIR "/text.vert", SHADERS_DIR "/text.frag"},
    {"particle", SHADERS_DIR "/particle.vert", SHADERS_DIR "/particle.frag"},
    {"particle_gbuffer", SHADERS_DIR "/particle_gbuffer.vert", SHADERS_DIR "/particle_gbuffer.frag"},
    {"rain", SHADERS_DIR "/rain.vert", SHADERS_DIR "/rain.frag"},
    {"postprocess", SHADERS_DIR "/postprocess.vert", SHADERS_DIR "/postprocess.frag"},
    {"blit_texture", SHADERS_DIR "/postprocess.vert", SHADERS_DIR "/blit_texture.frag"},
    {"item_model", SHADERS_DIR "/item_model.vert", SHADERS_DIR "/item_model.frag"},
    {"item_model_forward", SHADERS_DIR "/item_model.vert", SHADERS_DIR "/item_model_forward.frag"},
    {"steve", SHADERS_DIR "/steve.vert", SHADERS_DIR "/steve.frag"},
    {"steve_forward", SHADERS_DIR "/steve.vert", SHADERS_DIR "/steve_forward.frag"},
    {"entity_gbuffer", SHADERS_DIR "/entity_gbuffer.vert", SHADERS_DIR "/entity_gbuffer.frag"},
    {"entity_shadow", SHADERS_DIR "/entity_shadow.vert", SHADERS_DIR "/entity_shadow.frag"},
    {"block_entity_gbuffer", SHADERS_DIR "/block_entity_gbuffer.vert", SHADERS_DIR "/block_entity_gbuffer.frag"},
    {"block_entity_shadow", SHADERS_DIR "/block_entity_shadow.vert", SHADERS_DIR "/block_entity_shadow.frag"},
    {"drop_gbuffer", SHADERS_DIR "/drop_gbuffer.vert", SHADERS_DIR "/drop_gbuffer.frag"},
    {"item_gbuffer", SHADERS_DIR "/item_gbuffer.vert", SHADERS_DIR "/item_gbuffer.frag"},
    {"item_shadow", SHADERS_DIR "/item_shadow.vert", SHADERS_DIR "/item_shadow.frag"},
    {"ui_color", SHADERS_DIR "/ui_color.vert", SHADERS_DIR "/ui_color.frag"},
    {"ui_glass", SHADERS_DIR "/ui_glass.vert", SHADERS_DIR "/ui_glass.frag"},
    {"skybox", SHADERS_DIR "/skybox.vert", SHADERS_DIR "/skybox.frag"},
    {"gameplay_sky", SHADERS_DIR "/gameplay_sky.vert", SHADERS_DIR "/gameplay_sky.frag"},
    {"gameplay_sky_forward", SHADERS_DIR "/gameplay_sky.vert", SHADERS_DIR "/gameplay_sky_forward.frag"},
    {"blur", SHADERS_DIR "/blur.vert", SHADERS_DIR "/blur.frag"},
};

} // namespace

namespace resource {

void loadDefaultShaders(ShaderLibrary& shaders) {
    for (const ShaderDefinition& shader : kDefaultShaders) {
        shaders.load(shader.name, shader.vertexPath, shader.fragmentPath);
    }
}

} // namespace resource
