#ifndef MECRAFT_SHADOW_CASTER_CULLER_H
#define MECRAFT_SHADOW_CASTER_CULLER_H

#include <glm/glm.hpp>

// Shadow caster culler — mirrors Iris BoxCuller + BoxCullingFrustum.
//
// Iris BoxCuller tests whether an AABB intersects a camera-centered cube:
//   minAllowed = camera - maxDistance (per axis)
//   maxAllowed = camera + maxDistance (per axis)
//   culled = any axis (aabb.max < minAllowed || aabb.min > maxAllowed)
//
// This is NOT a spherical distance check. It's an axis-aligned cube test.
//
// DerivativeMain shadow.culling=false disables *advanced* shadow frustum culling,
// but Iris still bounds shadow caster submission through shadow render distance
// / BoxCuller fallback. This culler replicates that behavior.
//
// Reference:
//   Iris-26.1/.../shadows/frustum/BoxCuller.java
//   Iris-26.1/.../shadows/frustum/fallback/BoxCullingFrustum.java
//   Iris-26.1/.../shadows/ShadowRenderer.java:295-374 (createShadowFrustum)

namespace shadow {

class ShadowCasterCuller {
public:
    // Setup the culler with shadow distance parameters.
    // halfPlaneLength: shadow distance (DerivativeMain: 192.0)
    // renderDistanceMul: shadowDistanceRenderMul (DerivativeMain: 1.0)
    //   When < 0, uses user video settings (not implemented in Phase 1a).
    // cameraPos: current camera position in world space.
    void setup(float halfPlaneLength, float renderDistanceMul,
               const glm::vec3& cameraPos);

    // Test whether an AABB is visible within the shadow caster domain.
    // Returns false if the AABB is completely outside the camera-centered cube.
    // Iris BoxCuller.isCulled semantics: any axis non-overlap → culled.
    bool isAabbVisible(const glm::vec3& aabbMin, const glm::vec3& aabbMax) const;

    // Debug counters
    void resetCounters();
    void recordVisible(float distance);
    void recordCulled();

    int   getVisibleCount() const      { return m_visibleCount; }
    int   getCulledCount() const       { return m_culledCount; }
    float getMaxCasterDistance() const { return m_maxCasterDistance; }
    const char* getCullingMode() const { return m_cullingMode; }
    float getMaxDistance() const       { return m_maxDistance; }

private:
    // Camera-centered cube bounds (Iris BoxCuller minAllowed/maxAllowed)
    float m_minAllowedX = 0.0f;
    float m_maxAllowedX = 0.0f;
    float m_minAllowedY = 0.0f;
    float m_maxAllowedY = 0.0f;
    float m_minAllowedZ = 0.0f;
    float m_maxAllowedZ = 0.0f;

    float m_maxDistance = 0.0f;

    // Debug
    int   m_visibleCount = 0;
    int   m_culledCount = 0;
    float m_maxCasterDistance = 0.0f;
    const char* m_cullingMode = "None";
};

} // namespace shadow

#endif // MECRAFT_SHADOW_CASTER_CULLER_H
