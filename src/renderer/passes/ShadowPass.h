#ifndef MECRAFT_SHADOW_PASS_H
#define MECRAFT_SHADOW_PASS_H

#include "RenderPass.h"
#include "../mesh/WorldDrawBatch.h"
#include <glm/glm.hpp>
#include <vector>

#include "renderer/core/FrameContext.h"
#include "renderer/core/RenderSettings.h"
#include "renderer/mesh/TerrainRenderCache.h"

class DeferredRenderTargets;
class ResourceMgr;
class Shader;
class IWorldView;
class World;
class HumanoidRenderer;
class DropRenderer;
class FallingBlockRenderer;
class DropSystem;
class WorldRenderBuffer;
class TerrainRenderer;

namespace ecs { class GameplayRegistry; }

namespace shadow { class ShadowRenderer; }

/// Shadow pass: renders CSM shadow maps for all cascades.
/// Handles opaque terrain, cutout, entity, drop, and transparent shadow casting.
class ShadowPass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "Shadow"; }

    /// Inject external dependencies (non-owning pointers).
    void setShadowRenderer(shadow::ShadowRenderer* sr) { m_shadowRenderer = sr; }
    void setTerrainRenderer(TerrainRenderer* tr) { m_terrainRenderer = tr; }
    void setWorldRenderBuffer(WorldRenderBuffer* buf) { m_worldRenderBuffer = buf; }
    void setHumanoidRenderer(HumanoidRenderer* hr) { m_humanoidRenderer = hr; }
    void setDropRenderer(DropRenderer* dr) { m_dropRenderer = dr; }
    void setFallingBlockRenderer(FallingBlockRenderer* fbr) { m_fallingBlockRenderer = fbr; }
    void setDropSystem(DropSystem* ds) { m_dropSystem = ds; }
    void setGameplayRegistry(ecs::GameplayRegistry* reg) { m_gameplayRegistry = reg; }

    /// Check if the shadow shader is loaded.
    [[nodiscard]] bool hasShaders() const { return m_shadowDepthShader != nullptr; }

    /// Execute the full CSM shadow pass for all cascades.
    /// Camera data is extracted from FrameContext internally.
    /// @param ctx Frame context (camera, sky, timing)
    /// @param settings Render settings (shadow parameters)
    /// @param targets Deferred render targets (shadow FBOs)
    /// @param world World for chunk queries
    /// @param preservedTransparentBatch Transparent batch to save/restore around the pass
    /// @param preservedTransparentPlan Transparent plan to save/restore around the pass
    /// @param useMultiDrawIndirect Whether MDI rendering is active
    /// @return Updated transparent batch and plan (preserved from input)
    struct ShadowPassOutput {
        std::vector<DrawBatchEntry> transparentBatch;
        TransparentPassPlan transparentPlan;
    };
    ShadowPassOutput execute(const FrameContext& ctx, const RenderSettings& settings,
                              DeferredRenderTargets& targets, const IWorldView& worldView,
                              const std::vector<DrawBatchEntry>& preservedTransparentBatch,
                              const TransparentPassPlan& preservedTransparentPlan,
                              bool useMultiDrawIndirect);

private:
    /// Render humanoid/mob entities into the current shadow cascade layer.
    void renderShadowEntities(const IWorldView& worldView, const glm::mat4& shadowViewProj,
                              const glm::vec3& cameraPos, float splitNear, float splitFar);

    /// Render dropped items/blocks into the current shadow cascade layer.
    void renderShadowDrops(const IWorldView& worldView, const glm::mat4& shadowViewProj,
                            const glm::mat4& shadowView, const glm::mat4& shadowProjection,
                            float animationTime, float shaderTime);

    /// Render falling-block entities into the current shadow cascade layer.
    void renderShadowFallingBlocks(const glm::mat4& shadowViewProj,
                                    const glm::mat4& shadowView, const glm::mat4& shadowProjection,
                                    float animationTime, float shaderTime);

    Shader* m_shadowDepthShader = nullptr;
    Shader* m_entityShadowShader = nullptr;
    shadow::ShadowRenderer* m_shadowRenderer = nullptr;
    TerrainRenderer* m_terrainRenderer = nullptr;
    WorldRenderBuffer* m_worldRenderBuffer = nullptr;
    HumanoidRenderer* m_humanoidRenderer = nullptr;
    DropRenderer* m_dropRenderer = nullptr;
    FallingBlockRenderer* m_fallingBlockRenderer = nullptr;
    DropSystem* m_dropSystem = nullptr;
    ecs::GameplayRegistry* m_gameplayRegistry = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;
};

#endif // MECRAFT_SHADOW_PASS_H
