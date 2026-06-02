#include "PhysicsSystem.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "PhysicsInfo.h"
#include "../world/IWorldView.h"
#include "../world/block/Block.h"
#include "../world/fluid/FluidFlow.h"
#include "../world/fluid/FluidState.h"
#include "../world/World.h"
#include "engine/platform/Time.h"

namespace {

constexpr float kContactEpsilon = 0.0005f;
constexpr float kAxisStepLength = 0.45f;

struct AABB {
    glm::vec3 min{};
    glm::vec3 max{};
};

AABB makeBodyAABBAt(const PhysicsBody& body, const glm::vec3& position) {
    const glm::vec3 center = position + body.colliderOffset;
    return AABB{center - body.halfExtents, center + body.halfExtents};
}

bool isSolidBlock(const IWorldView& world, const int x, const int y, const int z) {
    const BlockID id = world.getBlock(x, y, z);
    if (id == 0) {
        return false;
    }
    return BlockRegistry::get(id).isSolid;
}

bool isWaterBlock(const IWorldView& world, const int x, const int y, const int z) {
    // Check fluid layer first (waterlogged blocks), then block layer (pure water)
    const StateID fluidState = world.getFluidState(x, y, z);
    return FluidState::isWater(fluidState);
}

float waterTopY(const IWorldView& world, const int x, const int y, const int z) {
    const StateID fluidState = world.getFluidState(x, y, z);
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

bool overlapsSolid(const IWorldView& world, const AABB& box) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kContactEpsilon));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kContactEpsilon));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kContactEpsilon));

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                if (isSolidBlock(world, x, y, z)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool queryEyesInWater(const PhysicsBody& body, const IWorldView& world) {
    const glm::vec3 eyePos = body.position + glm::vec3(0.0f, body.eyeOffsetY, 0.0f);
    const int blockX = static_cast<int>(std::floor(eyePos.x));
    const int blockY = static_cast<int>(std::floor(eyePos.y));
    const int blockZ = static_cast<int>(std::floor(eyePos.z));
    const StateID fluidState = world.getFluidState(blockX, blockY, blockZ);
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
        glm::vec2(centerX, centerZ),
        glm::vec2(minX, minZ),
        glm::vec2(minX, maxZ),
        glm::vec2(maxX, minZ),
        glm::vec2(maxX, maxZ),
    };

    for (const glm::vec2& probe : probes) {
        const int bx = static_cast<int>(std::floor(probe.x));
        const int bz = static_cast<int>(std::floor(probe.y));
        for (int by = supportMinY; by <= supportMaxY; ++by) {
            if (isSolidBlock(world, bx, by, bz)) {
                return true;
            }
        }
    }

    return false;
}

float moveTowards(const float current, const float target, const float maxDelta) {
    if (current < target) {
        return std::min(current + maxDelta, target);
    }
    return std::max(current - maxDelta, target);
}

void applyHorizontalControl(PhysicsBody& body, const MoveIntent& intent, const PhysicsTuning& tuning,
                            const bool wasGrounded, const float dt) {
    glm::vec2 input = intent.move;
    const float len = glm::length(input);
    if (len > 1.0f) {
        input /= len;
    }
    else if (len < 0.001f && wasGrounded && !body.isInWater) {
        // Keep grounded body stable without destroying vertical state.
        body.velocity.x = 0.0f;
        body.velocity.z = 0.0f;
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

    const float targetX = input.x * speed;
    const float targetZ = input.y * speed;

    float control = tuning.groundFriction * 2.0f;
    if (intent.isFlying) {
        control = tuning.groundFriction * 2.0f;
    } else if (body.isInWater) {
        control = tuning.waterDrag * 1.4f;
    } else if (!wasGrounded) {
        control = std::max(0.1f, tuning.groundFriction * tuning.airControl);
    }

    const float maxDelta = control * dt;
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

        body.velocity.y = 0.0f;
        if (intent.wantsJump) {
            body.velocity.y += verticalSpeed;
        }
        if (intent.wantsCrouch) {
            body.velocity.y -= verticalSpeed;
        }
        return;
    }

    const float gravityScale = body.isInWater ? tuning.waterGravityScale : 1.0f;
    body.velocity.y -= tuning.gravity * gravityScale * dt;

    if (body.isInWater && intent.wantsJump) {
        if (body.isFullySubmerged) {
            body.velocity.y += tuning.swimUpAccel * dt;
        } else {
            body.velocity.y += static_cast<float>(std::sin(tuning.swimUpAccel * Time::currentGameTime)) + 1;
        }
    } else if (wasGrounded && intent.wantsJump) {
        // Hold-to-bounce: keep jumping as soon as we are grounded again.
        body.velocity.y = tuning.jumpSpeed;
    }

    body.velocity.y = std::clamp(body.velocity.y, -tuning.terminalVelocity, tuning.terminalVelocity);
}

void applyDrag(PhysicsBody& body, const MoveIntent& intent, const PhysicsTuning& tuning, const float dt) {
    const float drag = (body.isInWater && !intent.isFlying) ? tuning.waterDrag : tuning.airDrag;
    const float factor = std::max(0.0f, 1.0f - drag * dt);
    body.velocity *= factor;
}

void applyFluidFlow(PhysicsBody& body, const IWorldView& world, const MoveIntent& intent,
                    const PhysicsTuning& tuning, const float waterFillRatio, const float dt,
                    const World* concreteWorld) {
    if (intent.isFlying || !body.isInWater || waterFillRatio <= 0.0f) {
        return;
    }

    const glm::vec3 flow = queryWaterFlowVector(body, world, concreteWorld);
    if (glm::length(flow) <= 0.0001f) {
        return;
    }

    const float pushScale = body.isFullySubmerged
        ? 1.0f
        : std::max(0.8f, std::clamp(waterFillRatio, 0.0f, 1.0f));
    body.velocity += flow * (tuning.waterFlowPush * pushScale * dt);
}

void moveAndCollideAxis(PhysicsBody& body, const IWorldView& world, const MoveIntent& intent, const float dt, const int axis) {
    const float delta = body.velocity[axis] * dt;
    if (std::abs(delta) <= 0.0f) {
        return;
    }

    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / kAxisStepLength)));
    const float stepDelta = delta / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i) {
        const glm::vec3 prevPos = body.position;
        const bool protectLedge = axis != 1 && intent.wantsCrouch && !intent.isFlying &&
                                  body.isGrounded && !body.isInWater;
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

        if (!overlapsSolid(world, makeBodyAABBAt(body, body.position))) {
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
    : m_worldView(worldView),
      m_concreteWorld(worldView ? worldView->asWorld() : nullptr) {}

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

    applyHorizontalControl(body, intent, tuningOverride, wasGrounded, dt);
    applyVerticalForces(body, intent, tuningOverride, wasGrounded, dt);
    applyDrag(body, intent, tuningOverride, dt);
    applyFluidFlow(body, *m_worldView, intent, tuningOverride, waterFillRatio, dt, m_concreteWorld);

    body.isGrounded = false;
    moveAndCollideAxis(body, *m_worldView, intent, dt, 1); // Y
    moveAndCollideAxis(body, *m_worldView, intent, dt, 0); // X
    moveAndCollideAxis(body, *m_worldView, intent, dt, 2); // Z

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

