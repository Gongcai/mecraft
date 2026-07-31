#include "PhysicsSystem.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "PhysicsInfo.h"
#include "../world/IWorldView.h"
#include "../world/block/Block.h"
#include "../world/block/BlockCollision.h"
#include "../world/block/BlockStateRegistry.h"
#include "../world/fluid/FluidFlow.h"
#include "../world/fluid/FluidState.h"
#include "../world/World.h"

namespace {

constexpr float kContactEpsilon = 0.0005f;
constexpr float kAxisStepLength = 0.45f;
constexpr float kStepLiftEpsilon = 0.001f;
constexpr float kPenetrationScoreEpsilon = 0.000001f;

struct AABB {
    glm::vec3 min{};
    glm::vec3 max{};
};

struct SurfacePhysics {
    float friction = 1.0f;
    float speedFactor = 1.0f;
    float damping = 0.0f;
};

AABB makeBodyAABBAt(const PhysicsBody& body, const glm::vec3& position) {
    const glm::vec3 center = position + body.colliderOffset;
    return AABB{center - body.halfExtents, center + body.halfExtents};
}

bool isWaterBlock(const IWorldView& world, const int x, const int y, const int z) {
    // Check fluid layer first (waterlogged blocks), then block layer (pure water)
    const BlockStateId fluidState = world.getFluidState(x, y, z);
    return FluidState::isWater(fluidState);
}

float waterTopY(const IWorldView& world, const int x, const int y, const int z) {
    const BlockStateId fluidState = world.getFluidState(x, y, z);
    if (!FluidState::isWater(fluidState)) {
        return static_cast<float>(y);
    }
    return static_cast<float>(y) + FluidState::surfaceHeight(fluidState);
}

float overlapLen(float aMin, float aMax, float bMin, float bMax);

float queryWaterFillRatio(const PhysicsBody& body, const IWorldView& world) {
    const AABB box = makeBodyAABBAt(body, body.position);
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kContactEpsilon));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kContactEpsilon));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kContactEpsilon));

    const float totalVolume = (box.max.x - box.min.x) * (box.max.y - box.min.y) * (box.max.z - box.min.z);
    if (totalVolume <= 0.0f) {
        return 0.0f;
    }

    float waterVolume = 0.0f;
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                if (!isWaterBlock(world, x, y, z)) {
                    continue;
                }
                const float ox = overlapLen(box.min.x, box.max.x, static_cast<float>(x), static_cast<float>(x + 1));
                const float oy = overlapLen(box.min.y, box.max.y, static_cast<float>(y), waterTopY(world, x, y, z));
                const float oz = overlapLen(box.min.z, box.max.z, static_cast<float>(z), static_cast<float>(z + 1));
                waterVolume += ox * oy * oz;
            }
        }
    }

    return std::clamp(waterVolume / totalVolume, 0.0f, 1.0f);
}

glm::vec3 queryWaterFlowVector(const PhysicsBody& body, const IWorldView& world, const World* concreteWorld) {
    if (concreteWorld == nullptr) {
        return glm::vec3(0.0f);
    }
    const AABB box = makeBodyAABBAt(body, body.position);
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kContactEpsilon));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kContactEpsilon));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kContactEpsilon));

    glm::vec3 weightedFlow(0.0f);
    float weightSum = 0.0f;
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                if (!isWaterBlock(world, x, y, z)) {
                    continue;
                }
                const float ox = overlapLen(box.min.x, box.max.x, static_cast<float>(x), static_cast<float>(x + 1));
                const float oy = overlapLen(box.min.y, box.max.y, static_cast<float>(y), waterTopY(world, x, y, z));
                const float oz = overlapLen(box.min.z, box.max.z, static_cast<float>(z), static_cast<float>(z + 1));
                const float overlap = ox * oy * oz;
                if (overlap <= 0.0f) {
                    continue;
                }

                weightedFlow += computeFluidFlowVector(*concreteWorld, glm::ivec3(x, y, z), FluidKind::Water) * overlap;
                weightSum += overlap;
            }
        }
    }

    if (weightSum <= 0.0f) {
        return glm::vec3(0.0f);
    }
    return weightedFlow / weightSum;
}

float overlapLen(const float aMin, const float aMax, const float bMin, const float bMax) {
    return std::max(0.0f, std::min(aMax, bMax) - std::max(aMin, bMin));
}

bool overlapsCollision(const IWorldView& world, const AABB& box) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kContactEpsilon));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kContactEpsilon));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kContactEpsilon));

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                const BlockStateId stateId = world.getBlockState(x, y, z);
                if (BlockCollision::intersects(stateId, glm::ivec3(x, y, z), box.min, box.max)) {
                    return true;
                }
            }
        }
    }
    return false;
}

float collisionOverlapScore(const IWorldView& world, const AABB& box) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kContactEpsilon));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kContactEpsilon));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kContactEpsilon));

    float score = 0.0f;
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                const BlockStateId stateId = world.getBlockState(x, y, z);
                const glm::vec3 blockOffset(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                for (const BlockCollisionBox& localBox : BlockCollision::getBoxes(stateId)) {
                    const glm::vec3 obstacleMin = blockOffset + localBox.min;
                    const glm::vec3 obstacleMax = blockOffset + localBox.max;
                    const float ox = overlapLen(box.min.x, box.max.x, obstacleMin.x, obstacleMax.x);
                    const float oy = overlapLen(box.min.y, box.max.y, obstacleMin.y, obstacleMax.y);
                    const float oz = overlapLen(box.min.z, box.max.z, obstacleMin.z, obstacleMax.z);
                    score += ox * oy * oz;
                }
            }
        }
    }
    return score;
}

bool aabbIntersects(const glm::vec3& aMin, const glm::vec3& aMax, const glm::vec3& bMin, const glm::vec3& bMax) {
    return aMin.x < bMax.x && aMax.x > bMin.x && aMin.y < bMax.y && aMax.y > bMin.y && aMin.z < bMax.z &&
           aMax.z > bMin.z;
}

bool computeRequiredStepLift(const IWorldView& world, const AABB& box, const float maxStepHeight, float& outLift) {
    outLift = 0.0f;
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kContactEpsilon));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kContactEpsilon));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kContactEpsilon));

    bool hasContact = false;
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                const BlockStateId stateId = world.getBlockState(x, y, z);
                const glm::vec3 blockOffset(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                for (const BlockCollisionBox& localBox : BlockCollision::getBoxes(stateId)) {
                    const glm::vec3 obstacleMin = blockOffset + localBox.min;
                    const glm::vec3 obstacleMax = blockOffset + localBox.max;
                    if (!aabbIntersects(box.min, box.max, obstacleMin, obstacleMax)) {
                        continue;
                    }

                    hasContact = true;
                    const float requiredLift = obstacleMax.y - box.min.y;
                    if (requiredLift <= 0.0f) {
                        continue;
                    }
                    if (requiredLift > maxStepHeight + kStepLiftEpsilon) {
                        return false;
                    }
                    outLift = std::max(outLift, requiredLift + kStepLiftEpsilon);
                }
            }
        }
    }

    return hasContact && outLift > 0.0f && outLift <= maxStepHeight + kStepLiftEpsilon;
}

bool queryEyesInWater(const PhysicsBody& body, const IWorldView& world) {
    const glm::vec3 eyePos = body.position + glm::vec3(0.0f, body.eyeOffsetY, 0.0f);
    const int blockX = static_cast<int>(std::floor(eyePos.x));
    const int blockY = static_cast<int>(std::floor(eyePos.y));
    const int blockZ = static_cast<int>(std::floor(eyePos.z));
    const BlockStateId fluidState = world.getFluidState(blockX, blockY, blockZ);
    if (!FluidState::isWater(fluidState)) {
        return false;
    }
    const float localEyeY = eyePos.y - static_cast<float>(blockY);
    return localEyeY <= FluidState::surfaceHeight(fluidState) + 0.0001f;
}

bool hasGroundSupportAt(const PhysicsBody& body, const IWorldView& world, const glm::vec3& position) {
    const AABB box = makeBodyAABBAt(body, position);
    constexpr float kSupportProbeDepth = 0.08f;
    const int supportMinY = static_cast<int>(std::floor(box.min.y - kSupportProbeDepth));
    const int supportMaxY = static_cast<int>(std::floor(box.min.y - kContactEpsilon));

    constexpr float kProbeInset = 0.02f;
    const float minX = box.min.x + kProbeInset;
    const float maxX = box.max.x - kProbeInset;
    const float minZ = box.min.z + kProbeInset;
    const float maxZ = box.max.z - kProbeInset;
    const float centerX = (minX + maxX) * 0.5f;
    const float centerZ = (minZ + maxZ) * 0.5f;

    const std::array<glm::vec2, 5> probes = {
        glm::vec2(centerX, centerZ), glm::vec2(minX, minZ), glm::vec2(minX, maxZ),
        glm::vec2(maxX, minZ),       glm::vec2(maxX, maxZ),
    };

    for (const glm::vec2& probe : probes) {
        const int bx = static_cast<int>(std::floor(probe.x));
        const int bz = static_cast<int>(std::floor(probe.y));
        constexpr float kProbeRadius = 0.005f;
        const glm::vec3 probeMin(probe.x - kProbeRadius, box.min.y - kSupportProbeDepth, probe.y - kProbeRadius);
        const glm::vec3 probeMax(probe.x + kProbeRadius, box.min.y, probe.y + kProbeRadius);
        for (int by = supportMinY; by <= supportMaxY; ++by) {
            const BlockStateId stateId = world.getBlockState(bx, by, bz);
            if (BlockCollision::intersects(stateId, glm::ivec3(bx, by, bz), probeMin, probeMax)) {
                return true;
            }
        }
    }

    return false;
}

SurfacePhysics querySurfacePhysics(const PhysicsBody& body, const IWorldView& world, const bool grounded) {
    SurfacePhysics surface{};
    if (!grounded) {
        return surface;
    }

    const AABB box = makeBodyAABBAt(body, body.position);
    constexpr float kSupportProbeDepth = 0.08f;
    const int supportMinY = static_cast<int>(std::floor(box.min.y - kSupportProbeDepth));
    const int supportMaxY = static_cast<int>(std::floor(box.min.y - kContactEpsilon));

    constexpr float kProbeInset = 0.02f;
    const float minX = box.min.x + kProbeInset;
    const float maxX = box.max.x - kProbeInset;
    const float minZ = box.min.z + kProbeInset;
    const float maxZ = box.max.z - kProbeInset;
    const float centerX = (minX + maxX) * 0.5f;
    const float centerZ = (minZ + maxZ) * 0.5f;

    const std::array<glm::vec2, 5> probes = {
        glm::vec2(centerX, centerZ), glm::vec2(minX, minZ), glm::vec2(minX, maxZ),
        glm::vec2(maxX, minZ),       glm::vec2(maxX, maxZ),
    };

    float frictionSum = 0.0f;
    float speedFactorSum = 0.0f;
    float dampingSum = 0.0f;
    int contactCount = 0;
    for (const glm::vec2& probe : probes) {
        const int bx = static_cast<int>(std::floor(probe.x));
        const int bz = static_cast<int>(std::floor(probe.y));
        constexpr float kProbeRadius = 0.005f;
        const glm::vec3 probeMin(probe.x - kProbeRadius, box.min.y - kSupportProbeDepth, probe.y - kProbeRadius);
        const glm::vec3 probeMax(probe.x + kProbeRadius, box.min.y, probe.y + kProbeRadius);
        for (int by = supportMinY; by <= supportMaxY; ++by) {
            const BlockStateId stateId = world.getBlockState(bx, by, bz);
            if (stateId == NULL_BLOCK_STATE ||
                !BlockCollision::intersects(stateId, glm::ivec3(bx, by, bz), probeMin, probeMax)) {
                continue;
            }

            const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
            const BlockDef& def = BlockRegistry::getFast(blockId);
            frictionSum += def.surfaceFriction;
            speedFactorSum += def.surfaceSpeedFactor;
            dampingSum += def.surfaceDamping;
            ++contactCount;
        }
    }

    if (contactCount > 0) {
        const float invCount = 1.0f / static_cast<float>(contactCount);
        surface.friction = frictionSum * invCount;
        surface.speedFactor = speedFactorSum * invCount;
        surface.damping = dampingSum * invCount;
    }
    return surface;
}

bool tryStepUp(PhysicsBody& body, const IWorldView& world, const MoveIntent& intent, const float maxStepHeight) {
    const bool canGroundStep = body.isGrounded && !body.isInWater;
    const bool canWaterLedgeStep = body.isInWater && intent.wantsJump;
    if (intent.isFlying || (!canGroundStep && !canWaterLedgeStep) || maxStepHeight <= 0.0f) {
        return false;
    }

    const AABB collisionBox = makeBodyAABBAt(body, body.position);
    float lift = 0.0f;
    if (!computeRequiredStepLift(world, collisionBox, maxStepHeight, lift)) {
        return false;
    }

    glm::vec3 steppedPosition = body.position;
    steppedPosition.y += lift;
    if (overlapsCollision(world, makeBodyAABBAt(body, steppedPosition))) {
        return false;
    }

    body.position = steppedPosition;
    body.velocity.y = std::max(body.velocity.y, 0.0f);
    body.isGrounded = hasGroundSupportAt(body, world, steppedPosition);
    return true;
}

float moveTowards(const float current, const float target, const float maxDelta) {
    if (current < target) {
        return std::min(current + maxDelta, target);
    }
    return std::max(current - maxDelta, target);
}

void applyHorizontalControl(PhysicsBody& body, const MoveIntent& intent, const PhysicsTuning& tuning,
                            const SurfacePhysics& surface, const bool wasGrounded, const float dt) {
    glm::vec2 input = intent.move;
    const float len = glm::length(input);
    const bool hasInput = len >= 0.001f;
    if (len > 1.0f) {
        input /= len;
    } else if (!hasInput) {
        input = glm::vec2(0.0f);
    }

    float speed = intent.isFlying ? tuning.moveSpeed : (body.isInWater ? tuning.swimSpeed : tuning.moveSpeed);
    if (!body.isInWater || intent.isFlying) {
        const float sprintSpeed = tuning.moveSpeed * tuning.sprintMultiplier;
        if (!intent.isFlying && intent.wantsCrouch) {
            speed = sprintSpeed * 0.5f;
        } else if (intent.wantsSprint) {
            speed = sprintSpeed;
        }
    }
    if (wasGrounded && !body.isInWater && !intent.isFlying) {
        speed *= surface.speedFactor;
    }

    const float targetX = input.x * speed;
    const float targetZ = input.y * speed;

    if (hasInput) {
        float acceleration = tuning.groundAcceleration * surface.friction;
        if (intent.isFlying) {
            acceleration = tuning.flyingAcceleration;
        } else if (body.isInWater) {
            acceleration = tuning.waterAcceleration;
        } else if (!wasGrounded) {
            acceleration = tuning.airAcceleration * tuning.airControl;
        }
        const float maxDelta = acceleration * dt;
        body.velocity.x = moveTowards(body.velocity.x, targetX, maxDelta);
        body.velocity.z = moveTowards(body.velocity.z, targetZ, maxDelta);
        return;
    }

    float braking = 0.0f;
    if (intent.isFlying) {
        braking = tuning.flyingAcceleration;
    } else if (wasGrounded && !body.isInWater) {
        braking = tuning.groundFriction * surface.friction;
    }
    const float maxDelta = braking * dt;
    body.velocity.x = moveTowards(body.velocity.x, targetX, maxDelta);
    body.velocity.z = moveTowards(body.velocity.z, targetZ, maxDelta);
}

void applyVerticalForces(PhysicsBody& body, const MoveIntent& intent, const PhysicsTuning& tuning,
                         const bool wasGrounded, const float dt) {
    if (intent.isFlying) {
        float verticalSpeed = tuning.moveSpeed;
        if (intent.wantsSprint) {
            verticalSpeed *= tuning.sprintMultiplier;
        }

        float targetVelocityY = 0.0f;
        if (intent.wantsJump) {
            targetVelocityY += verticalSpeed;
        }
        if (intent.wantsCrouch) {
            targetVelocityY -= verticalSpeed;
        }
        body.velocity.y = moveTowards(body.velocity.y, targetVelocityY, tuning.flyingAcceleration * dt);
        return;
    }

    const float gravityScale = body.isInWater ? tuning.waterGravityScale : 1.0f;
    body.velocity.y -= tuning.gravity * gravityScale * dt;

    if (body.isInWater && intent.wantsJump) {
        body.velocity.y += tuning.swimUpAccel * dt;
    } else if (wasGrounded && intent.wantsJump) {
        // Hold-to-bounce: keep jumping as soon as we are grounded again.
        body.velocity.y = tuning.jumpSpeed;
    }

    body.velocity.y = std::clamp(body.velocity.y, -tuning.terminalVelocity, tuning.terminalVelocity);
}

void applyDrag(PhysicsBody& body, const MoveIntent& intent, const PhysicsTuning& tuning, const SurfacePhysics& surface,
               const bool wasGrounded, const float dt) {
    if (intent.isFlying) {
        const float factor = std::exp(-tuning.flyingDrag * dt);
        body.velocity *= factor;
        return;
    }
    if (body.isInWater) {
        const float factor = std::exp(-tuning.waterDrag * dt);
        body.velocity *= factor;
        return;
    }
    if (wasGrounded) {
        const float factor = std::exp(-(tuning.groundDamping + surface.damping) * dt);
        body.velocity.x *= factor;
        body.velocity.z *= factor;
        return;
    }
    const float factor = std::exp(-tuning.airDrag * dt);
    body.velocity *= factor;
}

void applyFluidFlow(PhysicsBody& body, const IWorldView& world, const MoveIntent& intent, const PhysicsTuning& tuning,
                    const float waterFillRatio, const float dt, const World* concreteWorld) {
    if (intent.isFlying || !body.isInWater || waterFillRatio <= 0.0f) {
        return;
    }

    const glm::vec3 flow = queryWaterFlowVector(body, world, concreteWorld);
    if (glm::length(flow) <= 0.0001f) {
        return;
    }

    const float pushScale = body.isFullySubmerged ? 1.0f : std::max(0.8f, std::clamp(waterFillRatio, 0.0f, 1.0f));
    body.velocity += flow * (tuning.waterFlowPush * pushScale * dt);
}

void moveAndCollideAxis(PhysicsBody& body, const IWorldView& world, const MoveIntent& intent,
                        const PhysicsTuning& tuning, const float dt, const int axis) {
    const float delta = body.velocity[axis] * dt;
    if (std::abs(delta) <= 0.0f) {
        return;
    }

    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / kAxisStepLength)));
    const float stepDelta = delta / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i) {
        const glm::vec3 prevPos = body.position;
        const bool protectLedge =
            axis != 1 && intent.wantsCrouch && !intent.isFlying && body.isGrounded && !body.isInWater;
        if (protectLedge) {
            glm::vec3 candidatePos = body.position;
            candidatePos[axis] += stepDelta;
            if (!hasGroundSupportAt(body, world, candidatePos)) {
                body.velocity[axis] = 0.0f;
                body.hitWall = true;
                return;
            }
        }

        body.position[axis] += stepDelta;

        const AABB candidateBox = makeBodyAABBAt(body, body.position);
        if (!overlapsCollision(world, candidateBox)) {
            continue;
        }

        if (axis != 1 && body.isGrounded && !body.isInWater && tryStepUp(body, world, intent, tuning.stepHeight)) {
            continue;
        }

        const bool canWaterLedgeStep = body.isInWater && intent.wantsJump;
        if (axis != 1 && canWaterLedgeStep && tryStepUp(body, world, intent, tuning.waterLedgeStepHeight)) {
            continue;
        }

        const AABB previousBox = makeBodyAABBAt(body, prevPos);
        const float previousPenetrationScore = collisionOverlapScore(world, previousBox);
        const float candidatePenetrationScore = collisionOverlapScore(world, candidateBox);
        if (previousPenetrationScore > kPenetrationScoreEpsilon &&
            candidatePenetrationScore <= previousPenetrationScore + kPenetrationScoreEpsilon) {
            continue;
        }

        // Roll back to last valid position to avoid per-frame contact jitter.
        body.position = prevPos;

        if (axis == 1 && stepDelta < 0.0f) {
            body.landingImpactSpeed = -body.velocity.y;
            body.isGrounded = true;
        }
        body.velocity[axis] = 0.0f;
        if (axis != 1) {
            body.hitWall = true;
        }
        return;
    }
}

} // namespace

namespace physics {

PhysicsSystem::PhysicsSystem(const IWorldView* worldView)
    : m_worldView(worldView), m_concreteWorld(worldView ? worldView->asWorld() : nullptr) {}

void PhysicsSystem::updateBody(PhysicsBody& body, const MoveIntent& intent, const float dt) {
    updateBody(body, intent, dt, tuning);
}

void PhysicsSystem::updateBody(PhysicsBody& body, const MoveIntent& intent, const float dt,
                               const PhysicsTuning& tuningOverride) {
    if (dt <= 0.0f || m_worldView == nullptr) {
        return;
    }

    const bool wasGrounded = body.isGrounded;

    body.hitWall = false;
    body.landingImpactSpeed = 0.0f;
    const float waterFillRatio = queryWaterFillRatio(body, *m_worldView);
    body.isInWater = waterFillRatio > 0.2f;
    body.isFullySubmerged = waterFillRatio > 0.95f;
    body.isEyesInWater = queryEyesInWater(body, *m_worldView);
    const SurfacePhysics surface = querySurfacePhysics(body, *m_worldView, wasGrounded);

    applyHorizontalControl(body, intent, tuningOverride, surface, wasGrounded, dt);
    applyVerticalForces(body, intent, tuningOverride, wasGrounded, dt);
    applyDrag(body, intent, tuningOverride, surface, wasGrounded, dt);
    applyFluidFlow(body, *m_worldView, intent, tuningOverride, waterFillRatio, dt, m_concreteWorld);

    body.isGrounded = false;
    moveAndCollideAxis(body, *m_worldView, intent, tuningOverride, dt, 1); // Y
    moveAndCollideAxis(body, *m_worldView, intent, tuningOverride, dt, 0); // X
    moveAndCollideAxis(body, *m_worldView, intent, tuningOverride, dt, 2); // Z

    // Keep grounded state stable while resting on solid support to avoid
    // one-frame false negatives that can retrigger landing events.
    if (!intent.isFlying && !body.isGrounded && wasGrounded && body.velocity.y <= 0.0f &&
        hasGroundSupportAt(body, *m_worldView, body.position)) {
        body.isGrounded = true;
        body.velocity.y = 0.0f;
    }

    const float postMoveWaterFillRatio = queryWaterFillRatio(body, *m_worldView);
    body.isInWater = postMoveWaterFillRatio > 0.2f;
    body.isFullySubmerged = postMoveWaterFillRatio > 0.95f;
    body.isEyesInWater = queryEyesInWater(body, *m_worldView);
}

} // namespace physics
