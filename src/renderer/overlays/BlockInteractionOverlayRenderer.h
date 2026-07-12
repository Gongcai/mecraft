#ifndef MECRAFT_BLOCK_INTERACTION_OVERLAY_RENDERER_H
#define MECRAFT_BLOCK_INTERACTION_OVERLAY_RENDERER_H

#include <cstdint>

#include <glm/glm.hpp>
#include "../rhi/RhiHandles.h"

class ResourceMgr;
class RhiDevice;
class RhiCommandList;
class IWorldView;

/// Decoupled data transfer structs for block interaction rendering.
struct BlockTargetRenderData {
    bool hasTarget = false;
    glm::ivec3 targetBlock{};
    glm::ivec3 hitNormal{};
};

struct BlockBreakRenderData {
    bool active = false;
    float progress01 = 0.0f;
    glm::ivec3 blockPos{};
    glm::ivec3 hitNormal{};
};

/// Renders block selection outline and break progress overlay.
/// Extracted from Renderer to reduce its responsibilities.
class BlockInteractionOverlayRenderer {
public:
    void init(ResourceMgr& resourceMgr, RhiDevice& rhiDevice);
    void shutdown();

    /// Render both block outline and break overlay.
    /// @param world The world containing block data
    /// @param viewProj Combined view-projection matrix
    /// @param target Block selection target data
    /// @param blockBreak Block break progress data
    void render(const IWorldView& worldView,
                const glm::mat4& viewProj,
                const BlockTargetRenderData& target,
                const BlockBreakRenderData& blockBreak,
                RhiCommandList& commandList);

private:
    void initOutlineMesh();
    void initBreakOverlayMesh();
    void renderBlockOutline(const IWorldView& worldView, const glm::mat4& viewProj,
                            const BlockTargetRenderData& target, RhiCommandList& commandList);
    void renderBlockBreakOverlay(const IWorldView& worldView, const glm::mat4& viewProj,
                                 const BlockBreakRenderData& blockBreak,
                                 RhiCommandList& commandList);

    int32_t m_breakOverlayVertexCount = 0;
    int32_t m_breakOverlayCrossVertexCount = 0;
    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_outlineVertexBuffer;
    RhiBufferHandle m_breakOverlayVertexBuffer;
    RhiBufferHandle m_breakOverlayCrossVertexBuffer;
    RhiShaderHandle m_outlineVertexShader;
    RhiShaderHandle m_outlineFragmentShader;
    RhiPipelineLayoutHandle m_outlinePipelineLayout;
    RhiPipelineHandle m_outlinePipeline;
    RhiShaderHandle m_breakVertexShader;
    RhiShaderHandle m_breakFragmentShader;
    RhiPipelineLayoutHandle m_breakPipelineLayout;
    RhiPipelineHandle m_breakPipeline;
};

#endif // MECRAFT_BLOCK_INTERACTION_OVERLAY_RENDERER_H
