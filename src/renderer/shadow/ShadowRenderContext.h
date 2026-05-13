#ifndef MECRAFT_SHADOW_RENDER_CONTEXT_H
#define MECRAFT_SHADOW_RENDER_CONTEXT_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Shadow render context — centralizes all shadow pass state that was previously
// scattered across Renderer member variables (m_shadowModelView, m_shadowProjection, etc.).
//
// Produced by ShadowRenderer after each shadow pass. Consumed by:
//   - bindShadowFrameUniforms() for deferred lighting / volumetric fog
//   - Debug overlay for shadow caster diagnostics

namespace shadow {

struct ShadowRenderContext {
    // Matrices — consumed by deferred lighting, volumetric fog, debug view
    glm::mat4 modelView         = glm::mat4(1.0f);
    glm::mat4 projection        = glm::mat4(1.0f);
    glm::mat4 projectionInverse = glm::mat4(1.0f);
    glm::mat4 viewProj          = glm::mat4(1.0f);

    // Light direction in world space
    glm::vec3 lightDirection    = glm::vec3(0.0f, 1.0f, 0.0f);

    // Spatial parameters
    float extent        = 1.0f;   // halfPlaneLength = shadowDistance
    float texelWorldSize = 1.0f;  // (extent * 2) / resolution

    // Debug / observability — populated every frame by ShadowRenderer
    struct DebugInfo {
        int   terrainChunkCount   = 0;    // chunks submitted to shadow pass
        int   culledChunkCount    = 0;    // chunks rejected by culler
        float maxCasterDistance   = 0.0f; // farthest caster distance from camera
        float renderDistance      = 0.0f; // halfPlaneLength * renderDistanceMul / 16
        float shadowDistanceRenderMul = 1.0f;
        const char* cullingMode   = "unknown";
    } debug;
};

} // namespace shadow

#endif // MECRAFT_SHADOW_RENDER_CONTEXT_H
