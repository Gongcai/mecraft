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
class IWorldView;
class ResourceMgr;
class Shader;
class Window;
class TerrainRenderer;
class WorldRenderBuffer;
class TerrainRenderCache;

/// Entry describing a chunk or sub-chunk to render.
struct ChunkRenderEntry;

/// Water composite pass: renders water surfaces with deferred-compatible shading.
/// Handles depth softening, volumetric fog, sky capture reflections, and composite targets.
class WaterCompositePass : public RenderPass {
public:
    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "WaterComposite"; }

    /// Execute water composite rendering.
    /// @param ctx Frame context
    /// @param settings Render settings
    /// @param targets Deferred render targets
    /// @param world World for frame data queries
    /// @param deferredFrameActive Whether deferred pipeline is active this frame
    /// @param preTemporalResolve Whether rendering before TAA (affects jitter)
    /// @param transparentCompositeEnabled Whether transparent composite is enabled
    /// @param waterEffectsEnabled Whether water effects are enabled
    /// @param rainSurfaceRipplesEnabled Whether rain surface ripples are enabled
    /// @param volumetricFogActive Whether volumetric fog pass is active and has shaders
    /// @param useMultiDrawIndirect Whether MDI rendering is active
    /// @param worldRenderBuffer Main-view world render buffer used for the MDI transparent VAO
    /// @param transparentEntries Non-MDI path: transparent chunk entries for per-VAO draw
    /// @return true if water was rendered before temporal resolve (caller must set m_waterRenderedBeforeTemporal)
    bool execute(const FrameContext& ctx, const RenderSettings& settings,
                 DeferredRenderTargets& targets, const IWorldView& worldView,
                 bool deferredFrameActive, bool preTemporalResolve,
                 bool transparentCompositeEnabled,
                 bool waterEffectsEnabled, bool rainSurfaceRipplesEnabled,
                 bool volumetricFogActive,
                 bool useMultiDrawIndirect,
                 WorldRenderBuffer& worldRenderBuffer,
                 const std::vector<DrawBatchEntry>& transparentBatch,
                 const TransparentPassPlan& transparentPlan,
                 const std::vector<ChunkRenderEntry>& transparentEntries);

private:
    Shader* m_waterCompositeShader = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;
};

#endif // MECRAFT_WATER_COMPOSITE_PASS_H
