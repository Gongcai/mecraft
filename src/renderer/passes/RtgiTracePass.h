#ifndef MECRAFT_RTGI_TRACE_PASS_H
#define MECRAFT_RTGI_TRACE_PASS_H

#include "RenderPass.h"
#include "renderer/contracts/RtgiSamplingContract.h"
#include "renderer/core/FrameContext.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiRenderGraph.h"

#include <array>
#include <cstdint>

class RhiCommandList;
class RhiDevice;

/// Records the Vulkan raw diffuse RTGI ray-query signal with complete secondary-hit lighting.
class RtgiTracePass final : public RenderPass {
public:
    /// Immutable inputs and outputs declared by the caller's render graph.
    struct GraphResources final {
        RgTextureHandle depth;
        RgTextureHandle normalAo;
        RgTextureHandle materialAux;
        RgTextureHandle voxelLight;
        RgTextureHandle blueNoise;
        RgTextureHandle terrainAlbedo;
        RgTextureHandle terrainNormal;
        RgTextureHandle terrainSpecular;
        RgTextureHandle grassColormap;
        RgTextureHandle foliageColormap;
        RgTextureHandle skyCapture;
        RgTextureHandle diffuseRadianceHitDistance;
        RgTextureHandle validation;
    };

    /// Shared light-grid and local-shadow resources bound through the clustered consumer set.
    struct LightingResources final {
        RhiBindGroupLayoutHandle bindGroupLayout;
        RhiBindGroupHandle bindGroup;
        RgBufferHandle lights;
        RgBufferHandle worldCells;
        RgBufferHandle worldIndices;
        RgBufferHandle worldHeader;
        RgBufferHandle localShadowMetadata;
        RgTextureHandle localShadowSpotAtlas;
        RgTextureHandle localShadowPointCubeArray;
    };

    /// Explicit trace controls for primary traversal, secondary shadows, and terrain material maps.
    struct Settings final {
        float maxRayDistance = 64.0f;
        float maxShadowRayDistance = 128.0f;
        float minimumRayOriginBias = renderer::contracts::kRtgiMinimumRayOriginBias;
        uint8_t instanceMask = 3u;
        uint8_t shadowInstanceMask = 4u;
        bool useJitteredProjection = false;
        // Advance the stochastic sequence only when a temporal denoiser will
        // accumulate it. Raw RTGI inspection remains spatially stable.
        bool temporalSamplingEnabled = false;
        // The temporal owner may hold this phase while the camera is moving
        // so rejected NRD history does not expose a new one-spp estimate on
        // every frame.
        uint32_t temporalSampleIndex = 0u;
        bool terrainNormalMapsEnabled = true;
        bool terrainSpecularMapsEnabled = true;
        float blockLightStrength = 1.0f;
        // Match the artistic irradiance scale used by deferred sunlight.
        float celestialRadianceScale = 1.0f;
    };

    /// Latest successfully recorded raw-trace dispatch diagnostics.
    struct Stats final {
        bool dispatched = false;
        uint64_t frameIndex = 0u;
        uint64_t sceneTlasRevision = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint8_t instanceMask = 0u;
        uint64_t terrainHitDataBytes = 0u;
        uint64_t gpuSceneMaterialBytes = 0u;
        uint64_t gpuSceneGeometryBytes = 0u;
        uint64_t gpuSceneInstanceBytes = 0u;
        uint32_t gpuSceneMaterialCount = 0u;
        uint32_t gpuSceneGeometryCount = 0u;
    };

    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "RTGI Trace"; }

    /// Adds one Vulkan compute ray-query pass with exact G-buffer and raw-signal dependencies.
    /// @param graph Render graph receiving the trace declaration.
    /// @param ctx Current frame matrices, dimensions, and shared Global Bindless/TLAS owners.
    /// @param settings Explicit solid trace distance, origin bias, mask, and projection contract.
    /// @param resources Imported G-buffer, voxel-light, noise, terrain material, and storage-image resources.
    /// @param dependency Pass that completes all trace inputs before dispatch.
    /// @return Trace pass handle, or an invalid handle when any production contract is invalid.
    [[nodiscard]] RgPassHandle addGraphPass(RenderGraph& graph, const FrameContext& ctx, const Settings& settings,
                                            const GraphResources& resources, const LightingResources& lighting,
                                            RgPassHandle dependency);

    /// Returns diagnostics for the latest successfully recorded dispatch.
    /// @return Immutable dispatch snapshot; dispatched is false before the first successful record.
    [[nodiscard]] const Stats& stats() const { return m_stats; }

private:
    struct TraceViews final {
        RhiTextureViewHandle depth;
        RhiTextureViewHandle normalAo;
        RhiTextureViewHandle materialAux;
        RhiTextureViewHandle voxelLight;
        RhiTextureViewHandle blueNoise;
        RhiTextureViewHandle terrainAlbedo;
        RhiTextureViewHandle terrainNormal;
        RhiTextureViewHandle terrainSpecular;
        RhiTextureViewHandle grassColormap;
        RhiTextureViewHandle foliageColormap;
        RhiTextureViewHandle skyCapture;
        RhiTextureViewHandle diffuseRadianceHitDistance;
        RhiTextureViewHandle validation;
    };

    struct TraceSceneBuffers final {
        RhiBufferHandle terrainHitData;
        RhiBufferHandle gpuSceneMaterials;
        RhiBufferHandle gpuSceneGeometries;
        RhiBufferHandle gpuSceneInstances;
        uint64_t terrainHitDataBytes = 0u;
        uint64_t gpuSceneMaterialBytes = 0u;
        uint64_t gpuSceneGeometryBytes = 0u;
        uint64_t gpuSceneInstanceBytes = 0u;
        uint32_t sceneInstanceCount = 0u;
        uint32_t gpuSceneMaterialCount = 0u;
        uint32_t gpuSceneGeometryCount = 0u;
        glm::vec3 sceneOrigin{0.0f};
    };

    [[nodiscard]] bool recordTrace(RhiCommandList& commandList, const FrameContext& ctx, const Settings& settings,
                                   const TraceViews& views, const TraceSceneBuffers& sceneBuffers,
                                   RhiBindGroupLayoutHandle lightingLayout, RhiBindGroupHandle lightingBindGroup,
                                   uint64_t sceneTlasRevision);
    [[nodiscard]] bool ensurePipeline(RhiDevice& rhiDevice, RhiBindGroupLayoutHandle globalBindlessLayout,
                                      RhiBindGroupLayoutHandle lightingLayout);
    [[nodiscard]] bool ensureBindGroup(RhiDevice& rhiDevice, const TraceViews& views,
                                       const TraceSceneBuffers& sceneBuffers);
    void destroyRhiResources();

    RhiDevice* m_rhiDevice = nullptr;
    RhiShaderHandle m_shader;
    RhiSamplerHandle m_sampler;
    RhiSamplerHandle m_terrainSampler;
    RhiSamplerHandle m_linearClampSampler;
    RhiBufferHandle m_secondaryLightingBuffer;
    RhiBindGroupLayoutHandle m_globalBindlessLayout;
    RhiBindGroupLayoutHandle m_lightingLayout;
    RhiBindGroupLayoutHandle m_traceBindGroupLayout;
    RhiPipelineLayoutHandle m_pipelineLayout;
    RhiPipelineHandle m_pipeline;
    RhiBindGroupHandle m_traceBindGroup;
    std::array<RhiBufferHandle, 4u> m_boundSceneBuffers{};
    std::array<uint64_t, 4u> m_boundSceneBufferBytes{};
    std::array<RhiTextureViewHandle, 13u> m_boundViews{};
    Stats m_stats;
};

#endif // MECRAFT_RTGI_TRACE_PASS_H
