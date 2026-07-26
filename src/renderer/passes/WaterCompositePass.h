#ifndef MECRAFT_WATER_COMPOSITE_PASS_H
#define MECRAFT_WATER_COMPOSITE_PASS_H

#include "RenderPass.h"
#include "../mesh/WorldDrawBatch.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "renderer/core/FrameContext.h"
#include "renderer/core/RenderSettings.h"
#include "renderer/mesh/TerrainRenderCache.h"

class DeferredRenderTargets;
class ResourceMgr;
class WorldRenderBuffer;
class RhiCommandList;

/// Water composite pass: renders water surfaces with deferred-compatible shading.
/// Handles depth softening, volumetric fog, sky capture reflections, and composite targets.
class WaterCompositePass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "WaterComposite"; }

    /// Record water rendering into a caller-owned graphics command list.
    /// The caller owns resource transitions, input copies, and submission.
    [[nodiscard]] bool recordGraphPass(
        const FrameContext& ctx,
        const RenderSettings& settings,
        DeferredRenderTargets& targets,
        bool deferredFrameActive,
        bool preTemporalResolve,
        bool transparentCompositeEnabled,
        bool waterEffectsEnabled,
        bool rainSurfaceRipplesEnabled,
        bool volumetricFogActive,
        RhiCommandList& commandList,
        WorldRenderBuffer& worldRenderBuffer,
        const std::vector<DrawBatchEntry>& transparentBatch,
        const TransparentPassPlan& transparentPlan);

private:
    ResourceMgr* m_resourceMgr = nullptr;
};

#endif // MECRAFT_WATER_COMPOSITE_PASS_H
