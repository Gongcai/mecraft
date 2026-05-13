#ifndef MECRAFT_SHADOW_RENDER_CONTEXT_H
#define MECRAFT_SHADOW_RENDER_CONTEXT_H

#include "ShadowMatrices.h"

#include <glm/glm.hpp>
#include <array>

// CSM shadow render context.
//
// This is the data contract that deferred lighting, volumetric fog, and debug
// views should consume. The legacy single-matrix fields mirror cascade 0 only
// while older debug/preview paths are still being retired.

namespace shadow {

struct ShadowRenderContext {
    static constexpr int CASCADE_COUNT = ShadowMatrices::CASCADE_COUNT;

    std::array<ShadowMatrices::Cascade, CASCADE_COUNT> cascades{};
    int cascadeCount = CASCADE_COUNT;

    // Legacy cascade-0 compatibility for debug/history paths.
    glm::mat4 modelView         = glm::mat4(1.0f);
    glm::mat4 projection        = glm::mat4(1.0f);
    glm::mat4 projectionInverse = glm::mat4(1.0f);
    glm::mat4 viewProj          = glm::mat4(1.0f);

    glm::vec3 lightDirection    = glm::vec3(0.0f, 1.0f, 0.0f);

    float shadowDistance = 1.0f;
    float texelWorldSize = 1.0f;

    struct DebugInfo {
        int   terrainChunkCount   = 0;    // chunks submitted to shadow pass
        int   culledChunkCount    = 0;    // chunks rejected by culler
        float maxCasterDistance   = 0.0f; // farthest caster distance from camera
        float renderDistance      = 0.0f; // shadowDistance * renderDistanceMul / 16
        float shadowDistanceRenderMul = 1.0f;
        const char* cullingMode   = "unknown";
    } debug;
};

} // namespace shadow

#endif // MECRAFT_SHADOW_RENDER_CONTEXT_H
