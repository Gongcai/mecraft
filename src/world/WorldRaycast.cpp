#include "WorldRaycast.h"

#include <cmath>

#include "block/BlockSelection.h"
#include "fluid/FluidState.h"

namespace {

bool rayIntersectsAabb(const glm::vec3& rayOrigin,
                       const glm::vec3& rayDir,
                       const glm::vec3& boxMin,
                       const glm::vec3& boxMax,
                       const float maxDist,
                       float& outDistance,
                       glm::ivec3& outNormal) {
    float tMin = 0.0f;
    float tMax = maxDist;
    glm::ivec3 normal(0);

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(rayDir[axis]) < 1e-6f) {
            if (rayOrigin[axis] < boxMin[axis] || rayOrigin[axis] > boxMax[axis]) {
                return false;
            }
            continue;
        }

        const float invD = 1.0f / rayDir[axis];
        float t0 = (boxMin[axis] - rayOrigin[axis]) * invD;
        float t1 = (boxMax[axis] - rayOrigin[axis]) * invD;
        int normalSign = -1;

        if (t0 > t1) {
            std::swap(t0, t1);
            normalSign = 1;
        }

        if (t0 > tMin) {
            tMin = t0;
            normal = glm::ivec3(0);
            normal[axis] = normalSign;
        }
        tMax = std::min(tMax, t1);
        if (tMin > tMax) {
            return false;
        }
    }

    outDistance = tMin;
    outNormal = normal;
    return true;
}

} // namespace

RayHit raycastWorldView(const IWorldView& worldView,
                        const PhysicsInfo& ray,
                        const float maxDist) {
    RayHit hitResult{};

    const glm::vec3 rayDir = glm::normalize(ray.direction);
    const glm::vec3 rayOri = ray.origin;

    int x = static_cast<int>(std::floor(rayOri.x));
    int y = static_cast<int>(std::floor(rayOri.y));
    int z = static_cast<int>(std::floor(rayOri.z));

    const int stepX = (rayDir.x > 0.0f) ? 1 : -1;
    const int stepY = (rayDir.y > 0.0f) ? 1 : -1;
    const int stepZ = (rayDir.z > 0.0f) ? 1 : -1;

    const float tDeltaX = (rayDir.x != 0.0f) ? std::abs(1.0f / rayDir.x) : 1e30f;
    const float tDeltaY = (rayDir.y != 0.0f) ? std::abs(1.0f / rayDir.y) : 1e30f;
    const float tDeltaZ = (rayDir.z != 0.0f) ? std::abs(1.0f / rayDir.z) : 1e30f;

    float tMaxX = (stepX > 0) ? (x + 1.0f - rayOri.x) * tDeltaX : (rayOri.x - x) * tDeltaX;
    float tMaxY = (stepY > 0) ? (y + 1.0f - rayOri.y) * tDeltaY : (rayOri.y - y) * tDeltaY;
    float tMaxZ = (stepZ > 0) ? (z + 1.0f - rayOri.z) * tDeltaZ : (rayOri.z - z) * tDeltaZ;

    float dist = 0.0f;
    glm::ivec3 hitNormal(0);

    while (dist <= maxDist) {
        const BlockID block = worldView.getBlock(x, y, z);
        if (block != BlockIds::AIR && !FluidState::isWater(block)) {
            const StateID stateId = worldView.getBlockState(x, y, z);
            const BlockSelectionBox selectionBox = BlockSelection::getBox(stateId);
            const glm::vec3 boxMin = glm::vec3(x, y, z) + selectionBox.min;
            const glm::vec3 boxMax = glm::vec3(x, y, z) + selectionBox.max;

            float aabbDistance = 0.0f;
            glm::ivec3 aabbNormal(0);
            if (rayIntersectsAabb(rayOri, rayDir, boxMin, boxMax, maxDist, aabbDistance, aabbNormal)) {
                hitResult.hit = true;
                hitResult.blockPos = glm::ivec3(x, y, z);
                hitResult.normal = (aabbNormal.x != 0 || aabbNormal.y != 0 || aabbNormal.z != 0)
                    ? aabbNormal
                    : hitNormal;
                hitResult.distance = aabbDistance;
                return hitResult;
            }
        }

        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                x += stepX;
                dist = tMaxX;
                tMaxX += tDeltaX;
                hitNormal = glm::ivec3(-stepX, 0, 0);
            } else {
                z += stepZ;
                dist = tMaxZ;
                tMaxZ += tDeltaZ;
                hitNormal = glm::ivec3(0, 0, -stepZ);
            }
        } else {
            if (tMaxY < tMaxZ) {
                y += stepY;
                dist = tMaxY;
                tMaxY += tDeltaY;
                hitNormal = glm::ivec3(0, -stepY, 0);
            } else {
                z += stepZ;
                dist = tMaxZ;
                tMaxZ += tDeltaZ;
                hitNormal = glm::ivec3(0, 0, -stepZ);
            }
        }
    }

    return hitResult;
}
