#ifndef MECRAFT_BLOCK_INTERACTION_OVERLAY_RENDERER_H
#define MECRAFT_BLOCK_INTERACTION_OVERLAY_RENDERER_H

#include <glad/glad.h>
#include <glm/glm.hpp>

class ResourceMgr;
class Shader;
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
    void init(ResourceMgr& resourceMgr);
    void shutdown();

    /// Render both block outline and break overlay.
    /// @param world The world containing block data
    /// @param viewProj Combined view-projection matrix
    /// @param target Block selection target data
    /// @param blockBreak Block break progress data
    void render(const IWorldView& worldView,
                const glm::mat4& viewProj,
                const BlockTargetRenderData& target,
                const BlockBreakRenderData& blockBreak);

private:
    void initOutlineMesh();
    void initBreakOverlayMesh();
    void renderBlockOutline(const IWorldView& worldView, const glm::mat4& viewProj, const BlockTargetRenderData& target);
    void renderBlockBreakOverlay(const IWorldView& worldView, const glm::mat4& viewProj, const BlockBreakRenderData& blockBreak);

    Shader* m_outlineShader = nullptr;
    Shader* m_breakOverlayShader = nullptr;

    GLuint m_outlineVao = 0;
    GLuint m_outlineVbo = 0;
    GLuint m_breakOverlayVao = 0;
    GLuint m_breakOverlayVbo = 0;
    GLsizei m_breakOverlayVertexCount = 0;
    GLuint m_breakOverlayCrossVao = 0;
    GLuint m_breakOverlayCrossVbo = 0;
    GLsizei m_breakOverlayCrossVertexCount = 0;
};

#endif // MECRAFT_BLOCK_INTERACTION_OVERLAY_RENDERER_H
