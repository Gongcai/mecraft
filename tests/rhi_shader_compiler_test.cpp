#include "renderer/rhi/RhiShaderCompiler.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"

#include <array>
#include <cctype>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>

namespace {
struct ShaderCase {
    const char* path;
    RhiShaderStage stage;
    const char* definition = nullptr;
    const char* secondDefinition = nullptr;
    const char* thirdDefinition = nullptr;
};

[[nodiscard]] bool compileForBackend(const ShaderCase& shaderCase,
                                     const std::string& source,
                                     const renderer::rhi::RhiShaderBackend backend,
                                     const char* backendName) {
    RhiShaderDesc desc;
    desc.debugName = shaderCase.path;
    desc.stage = shaderCase.stage;
    desc.source = source.c_str();
    desc.sourceSize = source.size();

    std::string errorMessage;
    const auto compiled = renderer::rhi::compileShaderToSpirv(desc, backend, errorMessage);
    if (!compiled.has_value()) {
        std::cerr << backendName << " shader compilation failed [" << shaderCase.path
                  << "]: " << errorMessage << '\n';
        return false;
    }
    if (compiled->spirv.empty()) {
        std::cerr << backendName << " shader compilation produced empty SPIR-V\n";
        return false;
    }
    return true;
}

[[nodiscard]] std::string normalizedShaderSource(const std::string& source) {
    std::string normalized;
    normalized.reserve(source.size());
    for (const unsigned char character : source) {
        if (std::isspace(character) == 0) {
            normalized.push_back(static_cast<char>(character));
        }
    }
    return normalized;
}

[[nodiscard]] bool sourceContainsAll(
    const std::string& source,
    const std::initializer_list<std::string_view> tokens) {
    for (const std::string_view token : tokens) {
        if (source.find(token) == std::string::npos) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validateGBufferWriterContracts() {
    const auto terrainSource = renderer::rhi::loadShaderSource(
        "assets/shaders/chunk_gbuffer.frag");
    if (!terrainSource.has_value()) {
        std::cerr << "Terrain GBuffer fragment source failed to load\n";
        return false;
    }
    const std::string normalizedTerrain = normalizedShaderSource(*terrainSource);
    if (!sourceContainsAll(normalizedTerrain, {
            "layout(location=0)outvec4GAlbedoMaterial;",
            "layout(location=1)outvec4GNormalAo;",
            "layout(location=2)outvec4GVoxelLight;",
            "layout(location=3)outvec4GMaterial;",
            "layout(location=4)outvec4GMaterialAux;",
            "layout(location=5)outvec4GF0Metallic;",
            "layout(location=6)outuvec2GObjectMaterialId;"}) ||
        normalizedTerrain.find("layout(location=7)out") != std::string::npos) {
        std::cerr << "Terrain GBuffer writer must expose seven outputs ending in integer identity\n";
        return false;
    }

    constexpr std::array<const char*, 5> kObjectWriters{{
        "assets/shaders/entity_gbuffer_rhi.frag",
        "assets/shaders/item_drop_gbuffer_rhi.frag",
        "assets/shaders/falling_block_gbuffer_rhi.frag",
        "assets/shaders/block_entity_gbuffer_rhi.frag",
        "assets/shaders/static_mesh_gbuffer_rhi.frag"}};
    for (const char* path : kObjectWriters) {
        const auto source = renderer::rhi::loadShaderSource(path);
        if (!source.has_value()) {
            std::cerr << "Object GBuffer fragment source failed to load: " << path << '\n';
            return false;
        }
        const std::string normalized = normalizedShaderSource(*source);
        if (!sourceContainsAll(normalized, {
                "layout(location=0)outvec4gAlbedoMaterial;",
                "layout(location=1)outvec4gNormalAo;",
                "layout(location=2)outvec4gVoxelLight;",
                "layout(location=3)outvec4gMaterial;",
                "layout(location=4)outvec4gMaterialAux;",
                "layout(location=5)outvec4gF0Metallic;",
                "layout(location=6)outuvec2gObjectMaterialId;",
                "layout(location=7)outvec2gVelocity;"})) {
            std::cerr << "Object GBuffer writer must expose eight outputs with integer identity: "
                      << path << '\n';
            return false;
        }
    }
    return true;
}
} // namespace

int main() {
    constexpr std::array<ShaderCase, 84> kShaderCases{{
        {"tests/shaders/rhi_screen_coordinates_test.frag", RhiShaderStage::Fragment},
        {"tests/shaders/material_brdf_shared_test.frag", RhiShaderStage::Fragment},
        {"assets/shaders/fullscreen_triangle_rhi.vert", RhiShaderStage::Vertex},
        {"assets/shaders/deferred_lighting.vert", RhiShaderStage::Vertex},
        {"assets/shaders/skybox_blur_rhi.vert", RhiShaderStage::Vertex},
        {"assets/shaders/gameplay_sky_capture_rhi.vert", RhiShaderStage::Vertex},
        {"assets/shaders/chunk_gbuffer.vert", RhiShaderStage::Vertex,
         "RHI_TERRAIN_MDI", "RHI_TERRAIN_NORMAL_MAPS", "RHI_TERRAIN_SPECULAR_MAPS"},
        {"assets/shaders/entity_gbuffer_rhi.vert", RhiShaderStage::Vertex},
        {"assets/shaders/entity_gbuffer_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/item_drop_gbuffer_rhi.vert", RhiShaderStage::Vertex},
        {"assets/shaders/item_drop_gbuffer_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/falling_block_gbuffer_rhi.vert", RhiShaderStage::Vertex},
        {"assets/shaders/falling_block_gbuffer_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/falling_block_gbuffer_rhi.vert", RhiShaderStage::Vertex,
         "RHI_DROP_BLOCK"},
        {"assets/shaders/falling_block_gbuffer_rhi.frag", RhiShaderStage::Fragment,
         "RHI_DROP_BLOCK"},
        {"assets/shaders/block_entity_gbuffer_rhi.vert", RhiShaderStage::Vertex},
        {"assets/shaders/block_entity_gbuffer_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/static_mesh_gbuffer_rhi.vert", RhiShaderStage::Vertex},
        {"assets/shaders/static_mesh_gbuffer_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/static_mesh_shadow_rhi.vert", RhiShaderStage::Vertex},
        {"assets/shaders/static_mesh_shadow_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/static_mesh_preview_rhi.vert", RhiShaderStage::Vertex},
        {"assets/shaders/static_mesh_preview_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/static_mesh_transparent_rhi.vert", RhiShaderStage::Vertex},
        {"assets/shaders/static_mesh_transparent_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/static_mesh_transparent_rhi.frag", RhiShaderStage::Fragment,
         "MECRAFT_CLUSTERED_LIGHTING"},
        {"assets/shaders/chunk_gbuffer.frag", RhiShaderStage::Fragment,
         "RHI_TERRAIN_MDI", "RHI_TERRAIN_NORMAL_MAPS", "RHI_TERRAIN_SPECULAR_MAPS"},
        {"assets/shaders/ssao.frag", RhiShaderStage::Fragment},
        {"assets/shaders/ssao_filter.frag", RhiShaderStage::Fragment},
        {"assets/shaders/ssao_upsample.frag", RhiShaderStage::Fragment},
        {"assets/shaders/ssao_temporal.frag", RhiShaderStage::Fragment},
        {"assets/shaders/deferred_lighting.frag", RhiShaderStage::Fragment},
        {"assets/shaders/deferred_lighting.frag", RhiShaderStage::Fragment,
         "MECRAFT_CLUSTERED_LIGHTING"},
        {"assets/shaders/velocity_resolve.frag", RhiShaderStage::Fragment},
        {"assets/shaders/velocity_transparent_resolve.frag", RhiShaderStage::Fragment},
        {"assets/shaders/temporal_resolve.frag", RhiShaderStage::Fragment},
        {"assets/shaders/reflection_probe.frag", RhiShaderStage::Fragment},
        {"assets/shaders/reflection_filter.frag", RhiShaderStage::Fragment},
        {"assets/shaders/reflection_temporal.frag", RhiShaderStage::Fragment},
        {"assets/shaders/ssgi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/ssgi_upsample.frag", RhiShaderStage::Fragment},
        {"assets/shaders/ssgi_denoise_spatial.frag", RhiShaderStage::Fragment},
        {"assets/shaders/ssgi_denoise.frag", RhiShaderStage::Fragment},
        {"assets/shaders/ssgi_temporal.frag", RhiShaderStage::Fragment},
        {"assets/shaders/cloud_target.frag", RhiShaderStage::Fragment},
        {"assets/shaders/cloud_target.comp", RhiShaderStage::Compute},
        {"assets/shaders/ssao.comp", RhiShaderStage::Compute},
        {"assets/shaders/ssao_filter.comp", RhiShaderStage::Compute},
        {"assets/shaders/ssao_upsample.comp", RhiShaderStage::Compute},
        {"assets/shaders/ssao_temporal.comp", RhiShaderStage::Compute},
        {"assets/shaders/hiz_build.comp", RhiShaderStage::Compute},
        {"assets/shaders/hiz_cull.comp", RhiShaderStage::Compute},
        {"assets/shaders/shadow_cull.comp", RhiShaderStage::Compute},
        {"assets/shaders/cluster_count.comp", RhiShaderStage::Compute},
        {"assets/shaders/cluster_scan.comp", RhiShaderStage::Compute},
        {"assets/shaders/cluster_scan_add.comp", RhiShaderStage::Compute},
        {"assets/shaders/cluster_finalize.comp", RhiShaderStage::Compute},
        {"assets/shaders/cluster_fill.comp", RhiShaderStage::Compute},
        {"assets/shaders/cluster_validate.comp", RhiShaderStage::Compute},
        {"assets/shaders/volumetric_fog.frag", RhiShaderStage::Fragment},
        {"assets/shaders/volumetric_composite.frag", RhiShaderStage::Fragment},
        {"assets/shaders/volumetric_temporal.frag", RhiShaderStage::Fragment},
        {"assets/shaders/scene_composite.frag", RhiShaderStage::Fragment},
        {"assets/shaders/motion_blur.frag", RhiShaderStage::Fragment},
        {"assets/shaders/dof.frag", RhiShaderStage::Fragment},
        {"assets/shaders/postprocess.frag", RhiShaderStage::Fragment},
        {"assets/shaders/bloom_extract.frag", RhiShaderStage::Fragment},
        {"assets/shaders/bloom_blur.frag", RhiShaderStage::Fragment},
        {"assets/shaders/exposure_downsample.frag", RhiShaderStage::Fragment},
        {"assets/shaders/exposure_resolve.frag", RhiShaderStage::Fragment},
        {"assets/shaders/blur_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/fsr1_easu.frag", RhiShaderStage::Fragment},
        {"assets/shaders/fsr1_rcas.frag", RhiShaderStage::Fragment},
        {"assets/shaders/deferred_debug.frag", RhiShaderStage::Fragment},
        {"assets/shaders/skybox_blur_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/sky_capture_raw_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/sky_capture_metadata_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/gameplay_sky_capture_rhi.frag", RhiShaderStage::Fragment},
        {"assets/shaders/water_composite.frag", RhiShaderStage::Fragment,
         "RHI_TERRAIN_WATER_MDI"},
        {"assets/shaders/transparent_composite.frag", RhiShaderStage::Fragment,
         "RHI_TERRAIN_LIT_MDI"},
        {"assets/shaders/transparent_composite.frag", RhiShaderStage::Fragment,
         "RHI_TERRAIN_LIT_MDI", "MECRAFT_CLUSTERED_LIGHTING"},
        {"assets/shaders/particle_rhi.frag", RhiShaderStage::Fragment,
         "PARTICLE_DEFERRED"},
        {"assets/shaders/rain_rhi.frag", RhiShaderStage::Fragment,
         "RAIN_SCENE_DEPTH", "RAIN_TEMPORAL_MASKS"},
        {"assets/shaders/ui_glass_rhi.frag", RhiShaderStage::Fragment}
    }};

    bool success = true;
    for (const ShaderCase& shaderCase : kShaderCases) {
        renderer::rhi::RhiShaderSourceOptions sourceOptions;
        if (shaderCase.definition != nullptr) {
            sourceOptions.preprocessorDefinitions.emplace_back(shaderCase.definition);
        }
        if (shaderCase.secondDefinition != nullptr) {
            sourceOptions.preprocessorDefinitions.emplace_back(shaderCase.secondDefinition);
        }
        if (shaderCase.thirdDefinition != nullptr) {
            sourceOptions.preprocessorDefinitions.emplace_back(shaderCase.thirdDefinition);
        }
        const auto source = renderer::rhi::loadShaderSource(shaderCase.path, sourceOptions);
        if (!source.has_value()) {
            std::cerr << "Shader source failed to load: " << shaderCase.path << '\n';
            success = false;
            continue;
        }
        success = compileForBackend(shaderCase, *source,
                                    renderer::rhi::RhiShaderBackend::Vulkan,
                                    "Vulkan") && success;
        success = compileForBackend(shaderCase, *source,
                                    renderer::rhi::RhiShaderBackend::OpenGl,
                                    "OpenGL") && success;
    }
    success = validateGBufferWriterContracts() && success;
    return success ? 0 : 1;
}
