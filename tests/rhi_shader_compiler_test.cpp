#include "renderer/rhi/RhiShaderCompiler.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"
#include "renderer/rhi/gl/GlRhiShaderCompiler.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr uint32_t kSpirvMagicNumber = 0x07230203u;
constexpr uint32_t kSpirvVersion16 = 0x00010600u;
constexpr uint16_t kSpirvOpTerminateInvocation = 4416u;
constexpr uint16_t kSpirvOpDemoteToHelperInvocation = 5380u;

struct ShaderCase {
    const char* path;
    RhiShaderStage stage;
    const char* definition = nullptr;
    const char* secondDefinition = nullptr;
    const char* thirdDefinition = nullptr;
    bool vulkanOnly = false;
};

[[nodiscard]] bool compileForBackend(const ShaderCase& shaderCase, const std::string& source,
                                     const renderer::rhi::RhiShaderBackend backend, const char* backendName) {
    RhiShaderDesc desc;
    desc.debugName = shaderCase.path;
    desc.stage = shaderCase.stage;
    desc.source = source.c_str();
    desc.sourceSize = source.size();

    std::string errorMessage;
    const auto compiled = renderer::rhi::compileShaderToSpirv(desc, backend, errorMessage);
    if (!compiled.has_value()) {
        std::cerr << backendName << " shader compilation failed [" << shaderCase.path << "]: " << errorMessage << '\n';
        return false;
    }
    if (compiled->spirv.size() < 2u) {
        std::cerr << backendName << " shader compilation produced an incomplete SPIR-V header [" << shaderCase.path
                  << "]\n";
        return false;
    }
    if (compiled->spirv[0] != kSpirvMagicNumber || compiled->spirv[1] != kSpirvVersion16) {
        std::cerr << backendName << " shader compilation did not produce SPIR-V 1.6 [" << shaderCase.path << "]\n";
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

[[nodiscard]] bool sourceContainsAll(const std::string& source, const std::initializer_list<std::string_view> tokens) {
    for (const std::string_view token : tokens) {
        if (source.find(token) == std::string::npos) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool spirvContainsOpcode(const std::vector<uint32_t>& spirv, const uint16_t opcode) {
    size_t offset = 5u;
    while (offset < spirv.size()) {
        const uint32_t instruction = spirv[offset];
        const uint32_t wordCount = instruction >> 16u;
        if (wordCount == 0u || wordCount > spirv.size() - offset) {
            return false;
        }
        if (static_cast<uint16_t>(instruction & 0xffffu) == opcode) {
            return true;
        }
        offset += wordCount;
    }
    return false;
}

[[nodiscard]] bool validateFragmentDiscardContract() {
    constexpr std::string_view kSource = R"(#version 450 core
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform DiscardContractPushConstants {
    float cutoff;
} pc;

void main() {
    if (gl_FragCoord.x < pc.cutoff) {
        discard;
    }
    outColor = vec4(1.0);
}
)";

    RhiShaderDesc desc;
    desc.debugName = "fragment-discard-contract";
    desc.stage = RhiShaderStage::Fragment;
    desc.source = kSource.data();
    desc.sourceSize = kSource.size();

    const auto validateBackend = [&](const renderer::rhi::RhiShaderBackend backend, const char* backendName,
                                     const uint16_t expectedOpcode, const uint16_t rejectedOpcode) {
        std::string errorMessage;
        const auto compiled = renderer::rhi::compileShaderToSpirv(desc, backend, errorMessage);
        if (!compiled.has_value()) {
            std::cerr << backendName << " fragment discard contract failed to compile: " << errorMessage << '\n';
            return false;
        }
        if (!spirvContainsOpcode(compiled->spirv, expectedOpcode) ||
            spirvContainsOpcode(compiled->spirv, rejectedOpcode)) {
            std::cerr << backendName << " fragment discard contract produced an invalid termination opcode\n";
            return false;
        }
        return true;
    };

    return validateBackend(renderer::rhi::RhiShaderBackend::Vulkan, "Vulkan", kSpirvOpDemoteToHelperInvocation,
                           kSpirvOpTerminateInvocation) &&
           validateBackend(renderer::rhi::RhiShaderBackend::OpenGl, "OpenGL", kSpirvOpTerminateInvocation,
                           kSpirvOpDemoteToHelperInvocation);
}

[[nodiscard]] bool validateDescriptorArrayReflection() {
    constexpr std::string_view kFixedSource = R"(#version 450 core
layout(set = 2, binding = 5) uniform sampler2D uTextures[4];
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(uTextures[2], vec2(0.5));
}
)";
    constexpr std::string_view kRuntimeSource = R"(#version 450 core
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 2, binding = 5) uniform sampler2D uTextures[];
layout(push_constant) uniform DescriptorArrayPushConstants {
    uint textureIndex;
} pc;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(uTextures[nonuniformEXT(pc.textureIndex)], vec2(0.5));
}
)";

    const auto validateSource = [](const std::string_view source, const bool runtimeArray) {
        for (const renderer::rhi::RhiShaderBackend backend :
             {renderer::rhi::RhiShaderBackend::Vulkan, renderer::rhi::RhiShaderBackend::OpenGl}) {
            RhiShaderDesc desc;
            desc.debugName = runtimeArray ? "runtime-descriptor-array-contract" : "fixed-descriptor-array-contract";
            desc.stage = RhiShaderStage::Fragment;
            desc.source = source.data();
            desc.sourceSize = source.size();
            std::string errorMessage;
            const auto compiled = renderer::rhi::compileShaderToSpirv(desc, backend, errorMessage);
            if (!compiled.has_value()) {
                std::cerr << "Descriptor array contract failed to compile: " << errorMessage << '\n';
                return false;
            }
            if (compiled->reflection.bindings.size() != 1u) {
                std::cerr << "Descriptor array contract produced an invalid binding count\n";
                return false;
            }
            const renderer::rhi::RhiShaderBindingInfo& binding = compiled->reflection.bindings.front();
            if (binding.set != 2u || binding.binding != 5u || binding.type != RhiBindingType::CombinedTextureSampler ||
                binding.arrayCount != (runtimeArray ? 1u : 4u) || binding.runtimeArray != runtimeArray) {
                std::cerr << "Descriptor array reflection does not match the canonical contract\n";
                return false;
            }
        }
        return true;
    };

    return validateSource(kFixedSource, false) && validateSource(kRuntimeSource, true);
}

[[nodiscard]] bool validateAccelerationStructureReflection() {
    constexpr std::string_view kSource = R"(#version 460 core
#extension GL_EXT_ray_query : require
layout(set = 0, binding = 4) uniform accelerationStructureEXT uSceneTlas;
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main() {
    rayQueryEXT query;
    rayQueryInitializeEXT(query, uSceneTlas, gl_RayFlagsTerminateOnFirstHitEXT, 0xff,
                          vec3(0.0), 0.01, vec3(0.0, 0.0, 1.0), 100.0);
}
)";

    RhiShaderDesc desc;
    desc.debugName = "acceleration-structure-reflection-contract";
    desc.stage = RhiShaderStage::Compute;
    desc.source = kSource.data();
    desc.sourceSize = kSource.size();
    std::string errorMessage;
    const auto compiled =
        renderer::rhi::compileShaderToSpirv(desc, renderer::rhi::RhiShaderBackend::Vulkan, errorMessage);
    if (!compiled.has_value() || compiled->reflection.bindings.size() != 1u) {
        std::cerr << "Acceleration-structure shader reflection failed: " << errorMessage << '\n';
        return false;
    }
    const renderer::rhi::RhiShaderBindingInfo& binding = compiled->reflection.bindings.front();
    if (binding.set != 0u || binding.binding != 4u || binding.type != RhiBindingType::AccelerationStructure ||
        binding.arrayCount != 1u || binding.runtimeArray || binding.stages != rhiFlag(RhiShaderStage::Compute)) {
        std::cerr << "Acceleration-structure reflection does not match the canonical contract\n";
        return false;
    }

    errorMessage.clear();
    const auto openGlSource = renderer::rhi::gl::crossCompileShaderToOpenGl(*compiled, {}, std::nullopt, errorMessage);
    if (openGlSource.has_value() || errorMessage != "OpenGL does not support acceleration-structure shader resources") {
        std::cerr << "OpenGL must explicitly reject acceleration-structure shader resources\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool validateSpirvBytecodeInput() {
    constexpr std::string_view kSource = R"(#version 450 core
layout(set = 0, binding = 3, rgba16f) uniform image2D uOutput;
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main() {
    imageStore(uOutput, ivec2(gl_GlobalInvocationID.xy), vec4(1.0));
}
)";

    RhiShaderDesc sourceDesc;
    sourceDesc.debugName = "spirv-bytecode-source";
    sourceDesc.stage = RhiShaderStage::Compute;
    sourceDesc.source = kSource.data();
    sourceDesc.sourceSize = kSource.size();
    std::string errorMessage;
    const auto sourceCompiled =
        renderer::rhi::compileShaderToSpirv(sourceDesc, renderer::rhi::RhiShaderBackend::Vulkan, errorMessage);
    if (!sourceCompiled.has_value()) {
        std::cerr << "SPIR-V bytecode source failed to compile: " << errorMessage << '\n';
        return false;
    }

    RhiShaderDesc bytecodeDesc;
    bytecodeDesc.debugName = "spirv-bytecode-input";
    bytecodeDesc.stage = RhiShaderStage::Compute;
    bytecodeDesc.bytecode = sourceCompiled->spirv.data();
    bytecodeDesc.bytecodeSize = sourceCompiled->spirv.size() * sizeof(uint32_t);
    const auto bytecodeCompiled =
        renderer::rhi::compileShaderToSpirv(bytecodeDesc, renderer::rhi::RhiShaderBackend::Vulkan, errorMessage);
    if (!bytecodeCompiled.has_value() || bytecodeCompiled->spirv != sourceCompiled->spirv ||
        bytecodeCompiled->reflection.bindings.size() != 1u) {
        std::cerr << "Valid SPIR-V bytecode was not accepted and reflected: " << errorMessage << '\n';
        return false;
    }
    const renderer::rhi::RhiShaderBindingInfo& binding = bytecodeCompiled->reflection.bindings.front();
    if (binding.set != 0u || binding.binding != 3u || binding.type != RhiBindingType::StorageTexture ||
        binding.arrayCount != 1u || binding.runtimeArray || binding.stages != rhiFlag(RhiShaderStage::Compute)) {
        std::cerr << "SPIR-V bytecode reflection does not match the source contract\n";
        return false;
    }

    std::array<uint32_t, 5u> invalidHeader{};
    bytecodeDesc.bytecode = invalidHeader.data();
    bytecodeDesc.bytecodeSize = sizeof(invalidHeader);
    errorMessage.clear();
    if (renderer::rhi::compileShaderToSpirv(bytecodeDesc, renderer::rhi::RhiShaderBackend::Vulkan, errorMessage)
            .has_value() ||
        errorMessage != "SPIR-V bytecode has an invalid module header") {
        std::cerr << "Invalid SPIR-V bytecode must be rejected by its module header\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool validateGBufferWriterContracts() {
    const auto terrainSource = renderer::rhi::loadShaderSource("assets/shaders/chunk_gbuffer.frag");
    if (!terrainSource.has_value()) {
        std::cerr << "Terrain GBuffer fragment source failed to load\n";
        return false;
    }
    const std::string normalizedTerrain = normalizedShaderSource(*terrainSource);
    if (!sourceContainsAll(normalizedTerrain,
                           {"layout(location=0)outvec4GAlbedoMaterial;", "layout(location=1)outvec4GNormalAo;",
                            "layout(location=2)outvec4GVoxelLight;", "layout(location=3)outvec4GMaterial;",
                            "layout(location=4)outvec4GMaterialAux;", "layout(location=5)outvec4GF0Metallic;",
                            "layout(location=6)outuvec2GObjectMaterialId;"}) ||
        normalizedTerrain.find("layout(location=7)out") != std::string::npos) {
        std::cerr << "Terrain GBuffer writer must expose seven outputs ending in integer identity\n";
        return false;
    }

    constexpr std::array<const char*, 5> kObjectWriters{
        {"assets/shaders/entity_gbuffer_rhi.frag", "assets/shaders/item_drop_gbuffer_rhi.frag",
         "assets/shaders/falling_block_gbuffer_rhi.frag", "assets/shaders/block_entity_gbuffer_rhi.frag",
         "assets/shaders/static_mesh_gbuffer_rhi.frag"}};
    for (const char* path : kObjectWriters) {
        const auto source = renderer::rhi::loadShaderSource(path);
        if (!source.has_value()) {
            std::cerr << "Object GBuffer fragment source failed to load: " << path << '\n';
            return false;
        }
        const std::string normalized = normalizedShaderSource(*source);
        if (!sourceContainsAll(
                normalized, {"layout(location=0)outvec4gAlbedoMaterial;", "layout(location=1)outvec4gNormalAo;",
                             "layout(location=2)outvec4gVoxelLight;", "layout(location=3)outvec4gMaterial;",
                             "layout(location=4)outvec4gMaterialAux;", "layout(location=5)outvec4gF0Metallic;",
                             "layout(location=6)outuvec2gObjectMaterialId;", "layout(location=7)outvec2gVelocity;"})) {
            std::cerr << "Object GBuffer writer must expose eight outputs with integer identity: " << path << '\n';
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validateVelocityPixelCenterContracts() {
    const auto opaqueSource = renderer::rhi::loadShaderSource("assets/shaders/velocity_resolve.frag");
    const auto transparentSource = renderer::rhi::loadShaderSource("assets/shaders/velocity_transparent_resolve.frag");
    if (!opaqueSource.has_value() || !transparentSource.has_value()) {
        std::cerr << "Velocity resolve fragment sources failed to load\n";
        return false;
    }

    const std::string normalizedOpaque = normalizedShaderSource(*opaqueSource);
    if (!sourceContainsAll(normalizedOpaque,
                           {"vec2pixelCenter=vec2(texel)+vec2(0.5);",
                            "vec2currentScreenUv=rhiNativeFragCoordToScreenUv(pixelCenter,uScreenParams.xy);",
                            "vec2currentTextureUv=rhiScreenUvToTextureUv(currentScreenUv);",
                            "vec2currentClipUv=rhiScreenUvToClipUv(currentScreenUv);", "vec4uSkyClipToPrevClipRows[3];",
                            "constfloatkSkyDepth=1.0;",
                            "depth==kSkyDepth?reprojectSky(currentClip.xy):uClipToPrevClip*currentClip;",
                            "texelFetch(uPerObjectVelocityTex,texel,0).rg;"}) ||
        normalizedOpaque.find("rhiNativeFragCoordToScreenUv(closestFragment.xy") != std::string::npos ||
        normalizedOpaque.find("closestFragment.xy/uScreenParams.xy") != std::string::npos) {
        std::cerr << "Opaque velocity reprojection must use the fetched depth texel center\n";
        return false;
    }

    const std::string normalizedTransparent = normalizedShaderSource(*transparentSource);
    if (!sourceContainsAll(normalizedTransparent,
                           {"vec4uSkyClipToPrevClipRows[3];",
                            "rhiNativeFragCoordToScreenUv(vec2(gl_FragCoord.xy),uScreenParams.xy)"})) {
        std::cerr << "Transparent velocity reprojection must use the fragment sample center\n";
        return false;
    }
    return true;
}
} // namespace

int main() {
    constexpr std::array<ShaderCase, 110> kShaderCases{
        {{"tests/shaders/rhi_screen_coordinates_test.frag", RhiShaderStage::Fragment},
         {"tests/shaders/material_brdf_shared_test.frag", RhiShaderStage::Fragment},
         {"tests/shaders/reflection_probe_contract_test.frag", RhiShaderStage::Fragment},
         {"tests/shaders/gpu_scene_contract_test.comp", RhiShaderStage::Compute},
         {"tests/shaders/terrain_ray_tracing_contract_test.comp", RhiShaderStage::Compute},
         {"tests/shaders/global_bindless_gpu_scene_test.comp", RhiShaderStage::Compute, nullptr, nullptr, nullptr,
          true},
         {"tests/shaders/cutout_ray_query_test.comp", RhiShaderStage::Compute, nullptr, nullptr, nullptr, true},
         {"assets/shaders/rtgi_trace.comp", RhiShaderStage::Compute, nullptr, nullptr, nullptr, true},
         {"assets/shaders/nrd_guide_prep.comp", RhiShaderStage::Compute, nullptr, nullptr, nullptr, true},
         {"assets/shaders/rtgi_nrd_signal_pack.comp", RhiShaderStage::Compute, "MECRAFT_RTGI_SIGNAL_PACK_RELAX",
          nullptr, nullptr, true},
         {"assets/shaders/rtgi_nrd_signal_pack.comp", RhiShaderStage::Compute, "MECRAFT_RTGI_SIGNAL_PACK_REBLUR",
          nullptr, nullptr, true},
         {"assets/shaders/fullscreen_triangle_rhi.vert", RhiShaderStage::Vertex},
         {"assets/shaders/deferred_lighting.vert", RhiShaderStage::Vertex},
         {"assets/shaders/skybox_blur_rhi.vert", RhiShaderStage::Vertex},
         {"assets/shaders/gameplay_sky_capture_rhi.vert", RhiShaderStage::Vertex},
         {"assets/shaders/chunk_gbuffer.vert", RhiShaderStage::Vertex, "RHI_TERRAIN_MDI", "RHI_TERRAIN_NORMAL_MAPS",
          "RHI_TERRAIN_SPECULAR_MAPS"},
         {"assets/shaders/terrain_probe_capture.vert", RhiShaderStage::Vertex},
         {"assets/shaders/terrain_probe_capture.frag", RhiShaderStage::Fragment},
         {"assets/shaders/terrain_probe_capture.frag", RhiShaderStage::Fragment, "RHI_TERRAIN_NORMAL_MAPS",
          "RHI_TERRAIN_SPECULAR_MAPS"},
         {"assets/shaders/entity_gbuffer_rhi.vert", RhiShaderStage::Vertex},
         {"assets/shaders/entity_gbuffer_rhi.frag", RhiShaderStage::Fragment},
         {"assets/shaders/item_drop_gbuffer_rhi.vert", RhiShaderStage::Vertex},
         {"assets/shaders/item_drop_gbuffer_rhi.frag", RhiShaderStage::Fragment},
         {"assets/shaders/falling_block_gbuffer_rhi.vert", RhiShaderStage::Vertex},
         {"assets/shaders/falling_block_gbuffer_rhi.frag", RhiShaderStage::Fragment},
         {"assets/shaders/falling_block_gbuffer_rhi.vert", RhiShaderStage::Vertex, "RHI_DROP_BLOCK"},
         {"assets/shaders/falling_block_gbuffer_rhi.frag", RhiShaderStage::Fragment, "RHI_DROP_BLOCK"},
         {"assets/shaders/block_drop_forward_rhi.vert", RhiShaderStage::Vertex},
         {"assets/shaders/block_drop_forward_rhi.frag", RhiShaderStage::Fragment},
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
         {"assets/shaders/static_mesh_transparent_rhi.frag", RhiShaderStage::Fragment, "MECRAFT_CLUSTERED_LIGHTING"},
         {"assets/shaders/static_mesh_probe_capture_rhi.vert", RhiShaderStage::Vertex},
         {"assets/shaders/static_mesh_probe_capture_rhi.frag", RhiShaderStage::Fragment},
         {"assets/shaders/chunk_gbuffer.frag", RhiShaderStage::Fragment, "RHI_TERRAIN_MDI", "RHI_TERRAIN_NORMAL_MAPS",
          "RHI_TERRAIN_SPECULAR_MAPS"},
         {"assets/shaders/forward_basic_terrain.frag", RhiShaderStage::Fragment, "RHI_TERRAIN_FORWARD_MDI"},
         {"assets/shaders/shadow_depth.frag", RhiShaderStage::Fragment, "RHI_TERRAIN_SHADOW_MDI"},
         {"assets/shaders/shadow_depth.frag", RhiShaderStage::Fragment, "RHI_TERRAIN_SHADOW_MDI",
          "RHI_TERRAIN_SHADOW_DEPTH_ONLY"},
         {"assets/shaders/ssao.frag", RhiShaderStage::Fragment},
         {"assets/shaders/ssao_filter.frag", RhiShaderStage::Fragment},
         {"assets/shaders/ssao_upsample.frag", RhiShaderStage::Fragment},
         {"assets/shaders/ssao_temporal.frag", RhiShaderStage::Fragment},
         {"assets/shaders/deferred_lighting.frag", RhiShaderStage::Fragment},
         {"assets/shaders/deferred_lighting.frag", RhiShaderStage::Fragment, "MECRAFT_CLUSTERED_LIGHTING"},
         {"assets/shaders/deferred_lighting.frag", RhiShaderStage::Fragment, "MECRAFT_CLUSTERED_LIGHTING",
          "MECRAFT_RTGI_DIFFUSE"},
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
         {"assets/shaders/shadow_depth.vert", RhiShaderStage::Vertex, "RHI_TERRAIN_SHADOW_MDI"},
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
         {"assets/shaders/fsr_exposure_normalize.frag", RhiShaderStage::Fragment},
         {"assets/shaders/blur_rhi.frag", RhiShaderStage::Fragment},
         {"assets/shaders/fsr1_easu.frag", RhiShaderStage::Fragment},
         {"assets/shaders/fsr1_rcas.frag", RhiShaderStage::Fragment},
         {"assets/shaders/deferred_debug.frag", RhiShaderStage::Fragment},
         {"assets/shaders/skybox_blur_rhi.frag", RhiShaderStage::Fragment},
         {"assets/shaders/sky_capture_raw_rhi.frag", RhiShaderStage::Fragment},
         {"assets/shaders/sky_capture_metadata_rhi.frag", RhiShaderStage::Fragment},
         {"assets/shaders/gameplay_sky_capture_rhi.frag", RhiShaderStage::Fragment},
         {"assets/shaders/sky_ibl_radiance.frag", RhiShaderStage::Fragment},
         {"assets/shaders/sky_ibl_prefilter.frag", RhiShaderStage::Fragment},
         {"assets/shaders/reflection_probe_prefilter.frag", RhiShaderStage::Fragment},
         {"assets/shaders/sky_ibl_dfg.frag", RhiShaderStage::Fragment},
         {"assets/shaders/water_composite.frag", RhiShaderStage::Fragment, "RHI_TERRAIN_WATER_MDI"},
         {"assets/shaders/transparent_composite.frag", RhiShaderStage::Fragment, "RHI_TERRAIN_LIT_MDI"},
         {"assets/shaders/transparent_composite.frag", RhiShaderStage::Fragment, "RHI_TERRAIN_LIT_MDI",
          "MECRAFT_CLUSTERED_LIGHTING"},
         {"assets/shaders/particle_rhi.frag", RhiShaderStage::Fragment, "PARTICLE_DEFERRED"},
         {"assets/shaders/rain_rhi.frag", RhiShaderStage::Fragment, "RAIN_SCENE_DEPTH", "RAIN_TEMPORAL_MASKS"},
         {"assets/shaders/ui_glass_rhi.frag", RhiShaderStage::Fragment}}};

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
        success = compileForBackend(shaderCase, *source, renderer::rhi::RhiShaderBackend::Vulkan, "Vulkan") && success;
        if (!shaderCase.vulkanOnly) {
            success =
                compileForBackend(shaderCase, *source, renderer::rhi::RhiShaderBackend::OpenGl, "OpenGL") && success;
        }
    }
    success = validateFragmentDiscardContract() && success;
    success = validateDescriptorArrayReflection() && success;
    success = validateAccelerationStructureReflection() && success;
    success = validateSpirvBytecodeInput() && success;
    success = validateGBufferWriterContracts() && success;
    success = validateVelocityPixelCenterContracts() && success;
    return success ? 0 : 1;
}
