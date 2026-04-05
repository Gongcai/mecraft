#include "PhysicsSystem.h"

#include <algorithm>
#include <cmath>

#include "PhysicsInfo.h"
#include "../world/Block.h"
#include "../world/World.h"
#include "core/Time.h"

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

bool isSolidBlock(const World& world, const int x, const int y, const int z) {
    const BlockID id = world.getBlock(x, y, z);
    if (id == BlockType::AIR) {
        return false;
    }
    return BlockRegistry::get(id).isSolid;
}

bool isWaterBlock(const World& world, const int x, const int y, const int z) {
    const BlockID id = world.getBlock(x, y, z);
    if (id == BlockType::AIR) {
        return false;
    }
    const BlockDef& def = BlockRegistry::get(id);
    return def.name != nullptr && std::string(def.name) == "water";
}

float overlapLen(float aMin, float aMax, float bMin, float bMax);

float queryWaterFillRatio(const PhysicsBody& body, const World& world) {
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
                const float oy = overlapLen(box.min.y, box.max.y, static_cast<float>(y), static_cast<float>(y + 1));
                const float oz = overlapLen(box.min.z, box.max.z, static_cast<float>(z), static_cast<float>(z + 1));
                waterVolume += ox * oy * oz;
            }
        }
    }

    return std::clamp(waterVolume / totalVolume, 0.0f, 1.0f);
}

float overlapLen(const float aMin, const float aMax, const float bMin, const float bMax) {
    return std::max(0.0f, std::min(aMax, bMax) - std::max(aMin, bMin));
}

bool overlapsSolid(const World& world, const AABB& box) {
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

bool queryInWater(const PhysicsBody& body, const World& world) {
    return queryWaterFillRatio(body, world) > 0.2f;
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
    else if (len < 0.001f && wasGrounded) {
        // Keep grounded body stable without destroying vertical state.
        body.velocity.x = 0.0f;
        body.velocity.z = 0.0f;
    }


    float speed = body.isInWater ? tuning.swimSpeed : tuning.moveSpeed;
    if (!body.isInWater && intent.wantsSprint) {
        speed *= tuning.sprintMultiplier;
    }

    const float targetX = input.x * speed;
    const float targetZ = input.y * speed;

    float control = tuning.groundFriction * 2.0f;
    if (body.isInWater) {
        control = tuning.waterDrag * 1.4f;
    } else if (!wasGrounded) {
        control = std::max(0.1f, tuning.groundFriction * tuning.airControl);
    }

    const float maxDelta = control * dt;
    body.velocity.x = moveTowards(body.velocity.x, targetX, maxDelta);
    body.velocity.z = moveTowards(body.velocity.z, targetZ, maxDelta);
}

void applyVerticalForces(PhysicsBody& body, const MoveIntent& intent, const PhysicsTuning& tuning,
                         const bool wasGrounded, const bool isFullySubmerged, const float dt) {
    const float gravityScale = body.isInWater ? tuning.waterGravityScale : 1.0f;
    body.velocity.y -= tuning.gravity * gravityScale * dt;

    if (body.isInWater && intent.wantsJump) {
        if (isFullySubmerged) {
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

void applyDrag(PhysicsBody& body, const PhysicsTuning& tuning, const float dt) {
    const float drag = body.isInWater ? tuning.waterDrag : tuning.airDrag;
    const float factor = std::max(0.0f, 1.0f - drag * dt);
    body.velocity *= factor;
}

void moveAndCollideAxis(PhysicsBody& body, const World& world, const float dt, const int axis) {
    const float delta = body.velocity[axis] * dt;
    if (std::abs(delta) <= 0.0f) {
        return;
    }

    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / kAxisStepLength)));
    const float stepDelta = delta / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i) {
        const glm::vec3 prevPos = body.position;
        body.position[axis] += stepDelta;

        if (!overlapsSolid(world, makeBodyAABBAt(body, body.position))) {
            continue;
        }

        // Roll back to last valid position to avoid per-frame contact jitter.
        body.position = prevPos;

        body.velocity[axis] = 0.0f;
        if (axis == 1 && stepDelta < 0.0f) {
            body.isGrounded = true;
        }
        if (axis != 1) {
            body.hitWall = true;
        }
        return;
    }
}

} // namespace

namespace physics {


PhysicsSystem::PhysicsSystem(World* world) : m_world(world) {}

void PhysicsSystem::updateBody(PhysicsBody& body, const MoveIntent& intent, const float dt) {
    if (dt <= 0.0f || m_world == nullptr) {
        return;
    }

    const bool wasGrounded = body.isGrounded;

    body.hitWall = false;
    const float waterFillRatio = queryWaterFillRatio(body, *m_world);
    body.isInWater = waterFillRatio > 0.2f;
    const bool isFullySubmerged = waterFillRatio > 0.95f;

    applyHorizontalControl(body, intent, tuning, wasGrounded, dt);
    applyVerticalForces(body, intent, tuning, wasGrounded, isFullySubmerged, dt);
    applyDrag(body, tuning, dt);

    body.isGrounded = false;
    moveAndCollideAxis(body, *m_world, dt, 1); // Y
    moveAndCollideAxis(body, *m_world, dt, 0); // X
    moveAndCollideAxis(body, *m_world, dt, 2); // Z

    body.isInWater = queryInWater(body, *m_world);
}

} // namespace physics

