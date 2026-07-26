#ifndef MECRAFT_SHADOW_PASS_H
#define MECRAFT_SHADOW_PASS_H

#include "RenderPass.h"
#include "../mesh/TerrainRenderer.h"
#include "../debug/RenderDebugService.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"
#include "../rhi/RhiTypes.h"
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <vector>

#include "renderer/core/FrameContext.h"
#include "renderer/core/RenderSettings.h"
#include "renderer/mesh/TerrainRenderCache.h"

class DeferredRenderTargets;
class ResourceMgr;
class IWorldView;
class World;
class BlockEntityRenderer;
class HumanoidRenderer;
class DropRenderer;
class FallingBlockRenderer;
class DropSystem;
class WorldRenderBuffer;
class RhiCommandList;

namespace ecs { class GameplayRegistry; }

namespace shadow { class ShadowRenderer; }

/// Shadow pass: renders CSM shadow maps for all cascades.
/// Handles opaque terrain, cutout, entity, drop, and transparent shadow casting.
class ShadowPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Shadow"; }

    /// Inject external dependencies (non-owning pointers).
    void setShadowRenderer(shadow::ShadowRenderer* sr) { m_shadowRenderer = sr; }
    void setTerrainRenderer(TerrainRenderer* tr) { m_terrainRenderer = tr; }
    void setWorldRenderBuffer(WorldRenderBuffer* buf) { m_worldRenderBuffer = buf; }
    void setBlockEntityRenderer(BlockEntityRenderer* ber) { m_blockEntityRenderer = ber; }
    void setHumanoidRenderer(HumanoidRenderer* hr) { m_humanoidRenderer = hr; }
    void setDropRenderer(DropRenderer* dr) { m_dropRenderer = dr; }
    void setFallingBlockRenderer(FallingBlockRenderer* fbr) { m_fallingBlockRenderer = fbr; }
    void setDropSystem(DropSystem* ds) { m_dropSystem = ds; }
    void setGameplayRegistry(ecs::GameplayRegistry* reg) { m_gameplayRegistry = reg; }

    /// Graph handles for the persistent CSM texture arrays used by all cascades.
    struct GraphResources {
        RgTextureHandle depthOpaque;
        RgTextureHandle depthAll;
        RgTextureHandle color0;
        RgTextureHandle color1;
    };

    /// Computes cascade matrices, culls shadow casters, and creates layer views.
    /// @param ctx Source frame state retained until graph execution completes.
    /// @param settings Shadow quality and distance settings for this frame.
    /// @param targets Persistent shadow textures and per-layer attachment views.
    /// @param worldView World query interface used to collect terrain casters.
    /// @return True when all CPU data and attachment views are ready for graph recording.
    [[nodiscard]] bool prepareGraphFrame(const FrameContext& ctx,
                                         const RenderSettings& settings,
                                         DeferredRenderTargets& targets,
                                         const IWorldView& worldView);

    /// Adds opaque, depth-copy, and transparent passes for every cascade.
    /// @param graph Graph that owns the frame pass declarations.
    /// @param resources Imported CSM texture handles declared by the pipeline.
    /// @param dependency Pass that must finish before shadow command recording starts.
    /// @return Final transparent pass handle for downstream dependencies.
    [[nodiscard]] RgPassHandle addGraphPasses(RenderGraph& graph,
                                              const GraphResources& resources,
                                              RgPassHandle dependency);

    /// Opens optional debug timestamp state immediately before graph execution.
    void beginGraphExecution();

    /// Publishes successful statistics or cancels unsubmitted timestamp state.
    /// @param succeeded True only when the complete graph batch was submitted.
    void finishGraphExecution(bool succeeded);

private:
    /// Render humanoid/mob entities into the current shadow cascade layer.
    void renderShadowEntities(RhiCommandList& commandList,
                              const glm::mat4& shadowViewProj,
                              const glm::vec3& cameraPos,
                              float splitNear,
                              float splitFar);

    /// Render block entity models into the current shadow cascade layer.
    void renderShadowBlockEntities(RhiCommandList& commandList,
                                   const glm::mat4& shadowViewProj);

    /// Render dropped items/blocks into the current shadow cascade layer.
    void renderShadowDrops(RhiCommandList& commandList,
                           const glm::mat4& shadowViewProj,
                           float animationTime);

    /// Render falling-block entities into the current shadow cascade layer.
    void renderShadowFallingBlocks(RhiCommandList& commandList,
                                   const glm::mat4& shadowViewProj,
                                   float animationTime);

    /// Records opaque and cutout casters into one cascade's primary depth layer.
    [[nodiscard]] bool recordOpaquePass(RhiCommandList& commandList, int cascade);
    /// Copies one cascade's primary depth layer into its transparent depth layer.
    [[nodiscard]] bool recordCopyPass(RhiCommandList& commandList, int cascade);
    /// Clears transparent shadow colors and records near-cascade transparent casters.
    [[nodiscard]] bool recordTransparentPass(RhiCommandList& commandList, int cascade);

    shadow::ShadowRenderer* m_shadowRenderer = nullptr;
    TerrainRenderer* m_terrainRenderer = nullptr;
    WorldRenderBuffer* m_worldRenderBuffer = nullptr;
    BlockEntityRenderer* m_blockEntityRenderer = nullptr;
    HumanoidRenderer* m_humanoidRenderer = nullptr;
    DropRenderer* m_dropRenderer = nullptr;
    FallingBlockRenderer* m_fallingBlockRenderer = nullptr;
    DropSystem* m_dropSystem = nullptr;
    ecs::GameplayRegistry* m_gameplayRegistry = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;

    std::array<std::vector<GpuMeshRange>, 4> m_cascadeOpaqueRanges;
    std::array<std::vector<GpuMeshRange>, 4> m_cascadeCutoutRanges;
    std::array<std::vector<GpuMeshRange>, 4> m_cascadeTransparentRanges;
    std::array<ShadowCascadeStats, 4> m_cascadeStats{};
    std::array<std::array<char, 128>, 4> m_cullerLabels{};
    std::array<uint32_t, 4> m_cascadeResolutions{};
    const FrameContext* m_frameContext = nullptr;
    DeferredRenderTargets* m_frameTargets = nullptr;
    RenderDebugService* m_frameDebugService = nullptr;
    int m_visibleTotal = 0;
    int m_culledTotal = 0;
    float m_maxCasterDistance = 0.0f;
    bool m_graphFramePrepared = false;
    bool m_graphExecutionBegun = false;
    bool m_shadowStatsActive = false;
};

#endif // MECRAFT_SHADOW_PASS_H
