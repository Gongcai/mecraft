#ifndef MECRAFT_LOCAL_SHADOW_PASS_H
#define MECRAFT_LOCAL_SHADOW_PASS_H

#include "RenderPass.h"
#include "renderer/contracts/LocalShadowContract.h"
#include "renderer/core/FrameContext.h"
#include "renderer/mesh/TerrainRenderer.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiRenderGraph.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class BlockEntityRenderer;
class DeferredRenderTargets;
class DropRenderer;
class DropSystem;
class FallingBlockRenderer;
class HumanoidRenderer;
class IDeferredGeometryProvider;
class IWorldView;
class ResourceMgr;
class RhiCommandList;
class RhiDevice;
class StaticMeshRenderer;
class WorldRenderBuffer;

namespace ecs { class GameplayRegistry; }

/// Latest CPU-side local-shadow allocation and cache statistics.
struct LocalShadowFrameStats final {
    bool valid = false;
    uint32_t requestedSpotLights = 0u;
    uint32_t requestedPointLights = 0u;
    uint32_t renderedSpotPages = 0u;
    uint32_t renderedPointPages = 0u;
    uint32_t reusedCachedPages = 0u;
    uint32_t spotAtlasResolution = 0u;
    uint32_t pointCubeCapacity = 0u;
    renderer::contracts::LocalShadowAllocationError allocationError =
        renderer::contracts::LocalShadowAllocationError::None;
};

/// Owns stable raster resources and resolves SceneLight requests into final
/// GpuLight records before clustered-light culling begins.
class LocalShadowPass final : public RenderPass {
public:
    struct GraphResources final {
        RgBufferHandle metadata;
        RgTextureHandle spotAtlas;
        RgTextureHandle pointCubeArray;
    };

    struct ConsumerResources final {
        RhiBufferHandle metadataBuffer;
        uint64_t metadataBufferBytes = 0u;
        RhiTextureViewHandle spotAtlasView;
        RhiTextureViewHandle pointCubeArrayView;
        RhiSamplerHandle sampler;
    };

    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override {
        return "LocalShadow";
    }

    void setTerrainRenderer(TerrainRenderer* renderer) {
        m_terrainRenderer = renderer;
    }
    void setWorldRenderBuffer(WorldRenderBuffer* buffer) {
        m_worldRenderBuffer = buffer;
    }
    void setBlockEntityRenderer(BlockEntityRenderer* renderer) {
        m_blockEntityRenderer = renderer;
    }
    void setStaticMeshRenderer(StaticMeshRenderer* renderer) {
        m_staticMeshRenderer = renderer;
    }
    void setHumanoidRenderer(HumanoidRenderer* renderer) {
        m_humanoidRenderer = renderer;
    }
    void setDropRenderer(DropRenderer* renderer) {
        m_dropRenderer = renderer;
    }
    void setFallingBlockRenderer(FallingBlockRenderer* renderer) {
        m_fallingBlockRenderer = renderer;
    }
    void setDropSystem(DropSystem* system) { m_dropSystem = system; }
    void setGameplayRegistry(ecs::GameplayRegistry* registry) {
        m_gameplayRegistry = registry;
    }

    /// Replaces the complete unallocated scene-light snapshot.
    /// @param lights Normalized records carrying explicit requested policies.
    void setSceneLights(
        std::vector<renderer::contracts::SceneLight> lights);

    /// Allocates stable pages, creates matrices and culling bins, and resolves
    /// final GPU shadow indices before ClusteredLightingPass preparation.
    /// @param ctx Current frame and camera state retained through graph record.
    /// @param worldView Gameplay world revisions, or null for model scenes.
    /// @return True when every request has a complete render plan.
    [[nodiscard]] bool prepareGraphFrame(const FrameContext& ctx,
                                         const IWorldView* worldView);

    /// Imports persistent metadata and depth resources into the frame graph.
    [[nodiscard]] bool importGraphResources(
        RenderGraph& graph,
        GraphResources& resources) const;

    /// Adds metadata upload, Spot atlas update, and Point cube update passes.
    [[nodiscard]] RgPassHandle addGraphPasses(
        RenderGraph& graph,
        const GraphResources& resources,
        RgPassHandle dependency);

    /// Commits cache signatures only after the complete graph was submitted.
    /// @param succeeded True when graph execution and submission succeeded.
    void finishGraphExecution(bool succeeded);

    [[nodiscard]] const std::vector<renderer::contracts::GpuLight>&
    resolvedLights() const {
        return m_resolvedLights;
    }

    [[nodiscard]] ConsumerResources consumerResources() const;
    [[nodiscard]] const LocalShadowFrameStats& frameStats() const {
        return m_frameStats;
    }
    [[nodiscard]] const std::string& lastError() const {
        return m_lastError;
    }

private:
    struct CacheRecord final {
        renderer::contracts::LocalShadowType type =
            renderer::contracts::LocalShadowType::Spot;
        uint32_t resourceSlot = 0u;
        glm::vec4 positionAndRange{0.0f};
        glm::vec4 directionAndOuterCosine{0.0f};
        uint64_t blockContentRevision = 0u;
        uint64_t activeChunkRevision = 0u;
        uint64_t dynamicOccluderRevision = 0u;
        bool valid = false;
    };

    struct PreparedShadow final {
        renderer::contracts::LocalShadowAllocation allocation;
        glm::vec3 worldPosition{0.0f};
        glm::vec3 direction{0.0f};
        float range = 0.0f;
        std::array<glm::mat4, 6> views{};
        std::array<glm::mat4, 6> projections{};
        std::array<glm::mat4, 6> worldViewProjections{};
        LocalShadowChunkRanges terrainRanges;
        CacheRecord pendingCache;
        bool redraw = false;
    };

    [[nodiscard]] bool ensureResources(RhiDevice& rhiDevice,
                                       uint32_t spotSlotCount,
                                       uint32_t pointSlotCount);
    [[nodiscard]] bool ensureMetadataBuffer(RhiDevice& rhiDevice);
    [[nodiscard]] bool ensureSampler(RhiDevice& rhiDevice);
    [[nodiscard]] bool ensureSpotAtlas(RhiDevice& rhiDevice,
                                       uint32_t requiredGridSize);
    [[nodiscard]] bool ensurePointCubeArray(RhiDevice& rhiDevice,
                                            uint32_t requiredCapacity);
    [[nodiscard]] bool buildPreparedShadows(const FrameContext& ctx,
                                            const IWorldView* worldView);
    [[nodiscard]] bool prepareWorldActors();
    [[nodiscard]] bool prepareWorldGeometry(RhiCommandList& commandList,
                                            const PreparedShadow& shadow,
                                            uint32_t faceIndex);
    void drawWorldGeometry(RhiCommandList& commandList,
                           const PreparedShadow& shadow,
                           uint32_t faceIndex) const;
    [[nodiscard]] bool recordMetadataUpload(
        RhiCommandList& commandList) const;
    [[nodiscard]] bool recordSpotAtlas(RhiCommandList& commandList);
    [[nodiscard]] bool recordPointCubeArray(RhiCommandList& commandList);
    [[nodiscard]] bool clearPointFace(RhiCommandList& commandList,
                                      uint32_t faceLayer) const;
    [[nodiscard]] bool renderShadowPage(RhiCommandList& commandList,
                                        const PreparedShadow& shadow,
                                        uint32_t faceIndex,
                                        RhiTextureViewHandle attachment,
                                        const RhiRect2D& renderArea,
                                        const char* debugName);
    [[nodiscard]] bool importTexture(RenderGraph& graph,
                                     RhiTextureHandle texture,
                                     RhiTextureViewHandle view,
                                     bool initialized,
                                     RgTextureHandle& graphTexture) const;
    void invalidateCache(renderer::contracts::LocalShadowType type);
    void destroySpotAtlas();
    void destroyPointCubeArray();
    void destroyResources();
    [[nodiscard]] static bool sameCacheRecord(const CacheRecord& lhs,
                                              const CacheRecord& rhs);

    ResourceMgr* m_resourceMgr = nullptr;
    RhiDevice* m_rhiDevice = nullptr;
    TerrainRenderer* m_terrainRenderer = nullptr;
    WorldRenderBuffer* m_worldRenderBuffer = nullptr;
    BlockEntityRenderer* m_blockEntityRenderer = nullptr;
    StaticMeshRenderer* m_staticMeshRenderer = nullptr;
    HumanoidRenderer* m_humanoidRenderer = nullptr;
    DropRenderer* m_dropRenderer = nullptr;
    FallingBlockRenderer* m_fallingBlockRenderer = nullptr;
    DropSystem* m_dropSystem = nullptr;
    ecs::GameplayRegistry* m_gameplayRegistry = nullptr;

    renderer::contracts::LocalShadowStableAllocator m_allocator;
    std::vector<renderer::contracts::SceneLight> m_sceneLights;
    std::vector<renderer::contracts::GpuLight> m_resolvedLights;
    std::vector<renderer::contracts::LocalShadowAllocation> m_allocations;
    std::vector<PreparedShadow> m_preparedShadows;
    std::array<renderer::contracts::LocalShadowMetadata,
               renderer::contracts::kLocalShadowMetadataCount> m_metadata{};
    std::unordered_map<uint32_t, CacheRecord> m_cacheRecords;

    RhiBufferHandle m_metadataBuffer;
    RhiTextureHandle m_spotAtlasTexture;
    RhiTextureViewHandle m_spotAtlasView;
    RhiTextureHandle m_pointCubeArrayTexture;
    RhiTextureViewHandle m_pointCubeArrayView;
    std::vector<RhiTextureViewHandle> m_pointFaceViews;
    RhiSamplerHandle m_sampler;
    uint32_t m_spotGridSize = 0u;
    uint32_t m_pointCubeCapacity = 0u;
    bool m_spotAtlasInitialized = false;
    bool m_pointCubeArrayInitialized = false;
    bool m_spotAtlasRebuilt = false;
    bool m_pointCubeArrayRebuilt = false;

    const FrameContext* m_frameContext = nullptr;
    const IWorldView* m_worldView = nullptr;
    bool m_externalGeometryFrame = false;
    bool m_worldActorsPrepared = false;
    bool m_graphFramePrepared = false;
    LocalShadowFrameStats m_pendingFrameStats;
    LocalShadowFrameStats m_frameStats;
    std::string m_lastError;
};

#endif // MECRAFT_LOCAL_SHADOW_PASS_H
