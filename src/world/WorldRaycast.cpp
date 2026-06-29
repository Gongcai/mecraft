#include "WorldRaycast.h"

#include <cmath>

#include "block/Block.h"
#include "block/BlockSelection.h"
#include "fluid/FluidState.h"
#include "redstone/WireFaceGeometry.h"

namespace {

BlockSelectionBox getFluidSelectionBox(const BlockStateId fluidState) {
    return {glm::vec3(0.0f),
            glm::vec3(1.0f, FluidState::surfaceHeight(fluidState), 1.0f)};
}

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

bool isWireContainerState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE || FluidState::decode(stateId).kind != FluidKind::None) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).isWireContainer;
}

bool rayIntersectsWireContainerParts(const IWorldView& worldView,
                                     const glm::ivec3& blockPos,
                                     const glm::vec3& rayOrigin,
                                     const glm::vec3& rayDir,
                                     const float maxDist,
                                     RayHit& hitResult) {
    WireContainerParts parts;
    if (!worldView.copyWireContainerParts(blockPos, parts) || parts.empty()) {
        return false;
    }

    bool found = false;
    float bestDistance = maxDist;
    glm::ivec3 bestNormal(0);
    parts.forEach([&](const WirePart& part) {
        const BlockSelectionBox selectionBox = BlockSelection::getFacePlaneBoxForFacing(part.facing);
        const glm::vec3 boxMin = glm::vec3(blockPos) + selectionBox.min;
        const glm::vec3 boxMax = glm::vec3(blockPos) + selectionBox.max;

        float distance = 0.0f;
        glm::ivec3 normal(0);
        if (!rayIntersectsAabb(rayOrigin, rayDir, boxMin, boxMax, maxDist, distance, normal)) {
            return;
        }
        if (found && distance >= bestDistance) {
            return;
        }

        found = true;
        bestDistance = distance;
        bestNormal = (normal.x != 0 || normal.y != 0 || normal.z != 0)
            ? normal
            : WireFaceGeometry::surfaceNormal(part.facing);
    });

    if (!found) {
        return false;
    }

    hitResult.hit = true;
    hitResult.kind = RayHitKind::Block;
    hitResult.blockPos = blockPos;
    hitResult.normal = bestNormal;
    hitResult.distance = bestDistance;
    hitResult.position = rayOrigin + rayDir * bestDistance;
    return true;
}

} // namespace

RayHit raycastWorldView(const IWorldView& worldView,
                        const PhysicsInfo& ray,
                        const float maxDist,
                        const RaycastFluidMode fluidMode) {
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
        const BlockStateId blockState = worldView.getBlockState(x, y, z);
        const bool blockLayerIsFluid = FluidState::isWater(blockState);
        const bool hasSolidSelection = blockState != NULL_BLOCK_STATE && !blockLayerIsFluid;
        const BlockStateId fluidState = (hasSolidSelection || fluidMode == RaycastFluidMode::Ignore)
            ? NULL_BLOCK_STATE
            : (blockLayerIsFluid ? blockState : worldView.getFluidState(x, y, z));
        const bool hasFluidSelection = FluidState::isWater(fluidState);

        if (hasSolidSelection || hasFluidSelection) {
            const bool solidIsWireContainer = hasSolidSelection && isWireContainerState(blockState);
            if (solidIsWireContainer) {
                if (rayIntersectsWireContainerParts(worldView,
                                                    glm::ivec3(x, y, z),
                                                    rayOri,
                                                    rayDir,
                                                    maxDist,
                                                    hitResult)) {
                    return hitResult;
                }
            } else {
                const BlockSelectionBox selectionBox = hasSolidSelection
                    ? BlockSelection::getBox(blockState)
                    : getFluidSelectionBox(fluidState);
                const glm::vec3 boxMin = glm::vec3(x, y, z) + selectionBox.min;
                const glm::vec3 boxMax = glm::vec3(x, y, z) + selectionBox.max;

                float aabbDistance = 0.0f;
                glm::ivec3 aabbNormal(0);
                if (rayIntersectsAabb(rayOri, rayDir, boxMin, boxMax, maxDist, aabbDistance, aabbNormal)) {
                    hitResult.hit = true;
                    hitResult.kind = hasSolidSelection ? RayHitKind::Block : RayHitKind::Fluid;
                    hitResult.blockPos = glm::ivec3(x, y, z);
                    hitResult.normal = (aabbNormal.x != 0 || aabbNormal.y != 0 || aabbNormal.z != 0)
                        ? aabbNormal
                        : hitNormal;
                    hitResult.distance = aabbDistance;
                    hitResult.position = rayOri + rayDir * aabbDistance;
                    return hitResult;
                }
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
