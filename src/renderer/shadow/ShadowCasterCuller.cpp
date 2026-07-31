#include "ShadowCasterCuller.h"

#include <cmath>

namespace shadow {

void ShadowCasterCuller::setup(float halfPlaneLength, float renderDistanceMul, const glm::vec3& cameraPos) {
    // Iris ShadowRenderer.createShadowFrustum:
    //   distance = halfPlaneLength * renderDistanceMul
    //   if (renderDistanceMul < 0) distance = userVideoSettings.shadowDistance * 16
    //   if (distance <= 0 || distance > renderDistance*16) → NonCullingFrustum
    //   else → BoxCuller(distance)
    //
    // Phase 1a: only implement the normal path (renderDistanceMul >= 0).
    // The NonCullingFrustum fallback (distance <= 0 or too large) is deferred.

    m_maxDistance = halfPlaneLength * renderDistanceMul;

    // Iris BoxCuller.setPosition:
    //   minAllowedX = cameraX - maxDistance
    //   maxAllowedX = cameraX + maxDistance
    //   ... per axis
    m_minAllowedX = cameraPos.x - m_maxDistance;
    m_maxAllowedX = cameraPos.x + m_maxDistance;
    m_minAllowedY = cameraPos.y - m_maxDistance;
    m_maxAllowedY = cameraPos.y + m_maxDistance;
    m_minAllowedZ = cameraPos.z - m_maxDistance;
    m_maxAllowedZ = cameraPos.z + m_maxDistance;

    m_cullingMode = "BoxCulling";
}

bool ShadowCasterCuller::isAabbVisible(const glm::vec3& aabbMin, const glm::vec3& aabbMax) const {
    // Iris BoxCuller.isCulled:
    //   if (maxX < minAllowedX || minX > maxAllowedX) return true;
    //   if (maxY < minAllowedY || minY > maxAllowedY) return true;
    //   return maxZ < minAllowedZ || minZ > maxAllowedZ;
    //
    // Returns true if culled. We return true if VISIBLE (not culled).

    if (aabbMax.x < m_minAllowedX || aabbMin.x > m_maxAllowedX)
        return false;
    if (aabbMax.y < m_minAllowedY || aabbMin.y > m_maxAllowedY)
        return false;
    if (aabbMax.z < m_minAllowedZ || aabbMin.z > m_maxAllowedZ)
        return false;
    return true;
}

void ShadowCasterCuller::resetCounters() {
    m_visibleCount = 0;
    m_culledCount = 0;
    m_maxCasterDistance = 0.0f;
}

void ShadowCasterCuller::recordVisible(float distance) {
    ++m_visibleCount;
    if (distance > m_maxCasterDistance) {
        m_maxCasterDistance = distance;
    }
}

void ShadowCasterCuller::recordCulled() {
    ++m_culledCount;
}

} // namespace shadow
