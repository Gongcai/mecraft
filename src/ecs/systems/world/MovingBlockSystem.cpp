#include "MovingBlockSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "../../GameplayRegistry.h"
#include "../../components/Components.h"
#include "../../../world/World.h"
#include "../../../world/block/BlockCollision.h"

namespace ecs {
namespace {

[[noreturn]] void failMovingBlockSystem(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

constexpr float kMovingBlockPushEpsilon = 0.001f;
constexpr float kMovingBlockSupportContactTolerance = 0.02f;
constexpr int kMaxMovingBlockDepenetrationIterations = 12;

struct CollisionAabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

struct MovingBlockFrameCollision {
    CollisionAabb currentBox;
    CollisionAabb previousBox;
    CollisionAabb sweptBox;
};

CollisionAabb physicsBodyAabb(const PhysicsBody& body) {
    const glm::vec3 center = body.position + body.colliderOffset;
    return {center - body.halfExtents, center + body.halfExtents};
}

bool aabbIntersects(const CollisionAabb& lhs, const CollisionAabb& rhs) {
    return lhs.min.x < rhs.max.x && lhs.max.x > rhs.min.x && lhs.min.y < rhs.max.y && lhs.max.y > rhs.min.y &&
           lhs.min.z < rhs.max.z && lhs.max.z > rhs.min.z;
}

CollisionAabb translatedAabb(const CollisionAabb& box, const glm::vec3& delta) {
    return {box.min + delta, box.max + delta};
}

glm::vec3 minVec3(const glm::vec3& lhs, const glm::vec3& rhs) {
    return {
        std::min(lhs.x, rhs.x),
        std::min(lhs.y, rhs.y),
        std::min(lhs.z, rhs.z),
    };
}

glm::vec3 maxVec3(const glm::vec3& lhs, const glm::vec3& rhs) {
    return {
        std::max(lhs.x, rhs.x),
        std::max(lhs.y, rhs.y),
        std::max(lhs.z, rhs.z),
    };
}

CollisionAabb mergedAabb(const CollisionAabb& lhs, const CollisionAabb& rhs) {
    return {minVec3(lhs.min, rhs.min), maxVec3(lhs.max, rhs.max)};
}

float axisTranslationMagnitude(const glm::vec3& delta) {
    return std::abs(delta.x) + std::abs(delta.y) + std::abs(delta.z);
}

bool rangesOverlap(const float aMin, const float aMax, const float bMin, const float bMax) {
    return aMin < bMax && aMax > bMin;
}

template <typename Fn> void forEachWorldCollisionBox(const World& world, const glm::ivec3& blockPosition, Fn&& fn) {
    const BlockStateId stateId = world.getBlockState(blockPosition.x, blockPosition.y, blockPosition.z);
    const glm::vec3 blockOffset(blockPosition);
    for (const BlockCollisionBox& localBox : BlockCollision::getBoxes(stateId)) {
        fn(CollisionAabb{blockOffset + localBox.min, blockOffset + localBox.max});
    }
}

template <typename Fn>
void forEachIntersectingWorldCollisionBox(const World& world, const CollisionAabb& box, Fn&& fn) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kMovingBlockPushEpsilon));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kMovingBlockPushEpsilon));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kMovingBlockPushEpsilon));

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                forEachWorldCollisionBox(world, glm::ivec3(x, y, z), [&](const CollisionAabb& obstacle) {
                    if (aabbIntersects(box, obstacle)) {
                        fn(obstacle);
                    }
                });
            }
        }
    }
}

bool worldCollisionIntersects(const World& world, const CollisionAabb& box) {
    bool intersects = false;
    forEachIntersectingWorldCollisionBox(world, box, [&](const CollisionAabb&) { intersects = true; });
    return intersects;
}

void appendSeparationCandidates(const CollisionAabb& body, const CollisionAabb& obstacle,
                                std::vector<glm::vec3>& candidates) {
    candidates.push_back(glm::vec3(obstacle.max.x - body.min.x + kMovingBlockPushEpsilon, 0.0f, 0.0f));
    candidates.push_back(glm::vec3(obstacle.min.x - body.max.x - kMovingBlockPushEpsilon, 0.0f, 0.0f));
    candidates.push_back(glm::vec3(0.0f, obstacle.max.y - body.min.y + kMovingBlockPushEpsilon, 0.0f));
    candidates.push_back(glm::vec3(0.0f, obstacle.min.y - body.max.y - kMovingBlockPushEpsilon, 0.0f));
    candidates.push_back(glm::vec3(0.0f, 0.0f, obstacle.max.z - body.min.z + kMovingBlockPushEpsilon));
    candidates.push_back(glm::vec3(0.0f, 0.0f, obstacle.min.z - body.max.z - kMovingBlockPushEpsilon));
}

bool findWorldDepenetration(const World& world, const CollisionAabb& body, glm::vec3& outDelta) {
    std::vector<glm::vec3> candidates;
    forEachIntersectingWorldCollisionBox(
        world, body, [&](const CollisionAabb& obstacle) { appendSeparationCandidates(body, obstacle, candidates); });

    if (candidates.empty()) {
        outDelta = glm::vec3(0.0f);
        return false;
    }

    bool foundAny = false;
    glm::vec3 bestAny(0.0f);
    float bestAnyMagnitude = std::numeric_limits<float>::max();
    bool foundValid = false;
    glm::vec3 bestValid(0.0f);
    float bestValidMagnitude = std::numeric_limits<float>::max();

    for (const glm::vec3& candidate : candidates) {
        const float magnitude = axisTranslationMagnitude(candidate);
        if (magnitude <= 0.0f) {
            continue;
        }

        if (magnitude < bestAnyMagnitude) {
            foundAny = true;
            bestAny = candidate;
            bestAnyMagnitude = magnitude;
        }

        if (magnitude >= bestValidMagnitude) {
            continue;
        }

        if (!worldCollisionIntersects(world, translatedAabb(body, candidate))) {
            foundValid = true;
            bestValid = candidate;
            bestValidMagnitude = magnitude;
        }
    }

    if (foundValid) {
        outDelta = bestValid;
        return true;
    }
    if (foundAny) {
        outDelta = bestAny;
        return true;
    }

    outDelta = glm::vec3(0.0f);
    return false;
}

void applyPhysicsBodyTranslation(PhysicsBody& body, const glm::vec3& delta) {
    body.position += delta;
    if ((delta.x > 0.0f && body.velocity.x < 0.0f) || (delta.x < 0.0f && body.velocity.x > 0.0f)) {
        body.velocity.x = 0.0f;
    }
    if ((delta.y > 0.0f && body.velocity.y < 0.0f) || (delta.y < 0.0f && body.velocity.y > 0.0f)) {
        body.velocity.y = 0.0f;
    }
    if ((delta.z > 0.0f && body.velocity.z < 0.0f) || (delta.z < 0.0f && body.velocity.z > 0.0f)) {
        body.velocity.z = 0.0f;
    }
    if (delta.y > 0.0f) {
        body.isGrounded = true;
    }
}

void depenetratePhysicsBodyFromWorld(const World& world, PhysicsBody& body) {
    for (int iteration = 0; iteration < kMaxMovingBlockDepenetrationIterations; ++iteration) {
        glm::vec3 delta(0.0f);
        if (!findWorldDepenetration(world, physicsBodyAabb(body), delta)) {
            return;
        }
        applyPhysicsBodyTranslation(body, delta);
    }
}

std::vector<MovingBlockFrameCollision> collectMovingBlockCollisions(const BlockStateId stateId,
                                                                    const glm::vec3& previousOrigin,
                                                                    const glm::vec3& currentOrigin) {
    std::vector<MovingBlockFrameCollision> collisions;
    for (const BlockCollisionBox& localBox : BlockCollision::getBoxes(stateId)) {
        const CollisionAabb previousBox{previousOrigin + localBox.min, previousOrigin + localBox.max};
        const CollisionAabb currentBox{currentOrigin + localBox.min, currentOrigin + localBox.max};
        collisions.push_back({currentBox, previousBox, mergedAabb(previousBox, currentBox)});
    }
    return collisions;
}

bool pushPhysicsBodyFromMovingBlock(PhysicsBody& body, const MovingBlockFrameCollision& collision,
                                    const glm::ivec3& movementDirection) {
    const CollisionAabb bodyBox = physicsBodyAabb(body);
    if (!aabbIntersects(bodyBox, collision.sweptBox)) {
        return false;
    }

    glm::vec3 delta(0.0f);
    if (movementDirection.x > 0) {
        delta.x = collision.currentBox.max.x - bodyBox.min.x + kMovingBlockPushEpsilon;
    } else if (movementDirection.x < 0) {
        delta.x = collision.currentBox.min.x - bodyBox.max.x - kMovingBlockPushEpsilon;
    } else if (movementDirection.y > 0) {
        delta.y = collision.currentBox.max.y - bodyBox.min.y + kMovingBlockPushEpsilon;
    } else if (movementDirection.y < 0) {
        delta.y = collision.currentBox.min.y - bodyBox.max.y - kMovingBlockPushEpsilon;
    } else if (movementDirection.z > 0) {
        delta.z = collision.currentBox.max.z - bodyBox.min.z + kMovingBlockPushEpsilon;
    } else if (movementDirection.z < 0) {
        delta.z = collision.currentBox.min.z - bodyBox.max.z - kMovingBlockPushEpsilon;
    } else {
        failMovingBlockSystem("Moving block push requires a non-zero movement direction");
    }

    applyPhysicsBodyTranslation(body, delta);
    return true;
}

bool physicsBodyRidesMovingBlock(const PhysicsBody& body, const MovingBlockFrameCollision& collision) {
    const CollisionAabb bodyBox = physicsBodyAabb(body);
    if (std::abs(bodyBox.min.y - collision.previousBox.max.y) > kMovingBlockSupportContactTolerance) {
        return false;
    }

    return rangesOverlap(bodyBox.min.x, bodyBox.max.x, collision.previousBox.min.x, collision.previousBox.max.x) &&
           rangesOverlap(bodyBox.min.z, bodyBox.max.z, collision.previousBox.min.z, collision.previousBox.max.z);
}

bool carryPhysicsBodyOnMovingBlock(PhysicsBody& body, const MovingBlockFrameCollision& collision,
                                   const glm::vec3& frameDelta) {
    if (!physicsBodyRidesMovingBlock(body, collision)) {
        return false;
    }

    applyPhysicsBodyTranslation(body, frameDelta);
    return true;
}

bool targetCellIsReadyForPlacement(const World& world, const MovingBlockComponent& block) {
    if (!block.placeAtTarget) {
        return true;
    }
    return world.getBlockState(block.targetPosition.x, block.targetPosition.y, block.targetPosition.z) ==
           NULL_BLOCK_STATE;
}

void applyMovingBlockCollisionToEntities(World& world, GameplayRegistry& registry, const BlockStateId stateId,
                                         const glm::vec3& previousOrigin, const glm::vec3& currentOrigin,
                                         const glm::ivec3& movementDirection) {
    const glm::vec3 frameDelta = currentOrigin - previousOrigin;
    if (axisTranslationMagnitude(frameDelta) <= 0.0f) {
        return;
    }

    const std::vector<MovingBlockFrameCollision> collisions =
        collectMovingBlockCollisions(stateId, previousOrigin, currentOrigin);
    if (collisions.empty()) {
        return;
    }

    auto view = registry.registry().view<TransformComponent, PhysicsBodyComponent>();
    for (const entt::entity entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& physicsBody = view.get<PhysicsBodyComponent>(entity);

        physicsBody.body.position = transform.position;
        bool moved = false;
        for (const MovingBlockFrameCollision& collision : collisions) {
            if (pushPhysicsBodyFromMovingBlock(physicsBody.body, collision, movementDirection)) {
                moved = true;
            }
        }
        if (!moved) {
            for (const MovingBlockFrameCollision& collision : collisions) {
                if (carryPhysicsBodyOnMovingBlock(physicsBody.body, collision, frameDelta)) {
                    moved = true;
                    break;
                }
            }
        }

        if (!moved) {
            continue;
        }

        depenetratePhysicsBodyFromWorld(world, physicsBody.body);
        transform.position = physicsBody.body.position;
        if (auto* velocity = registry.try_get<VelocityComponent>(entity)) {
            velocity->velocity = physicsBody.body.velocity;
        }
    }
}

} // namespace

void MovingBlockSystem::update(SystemContext& ctx) {
    if (!ctx.services.world || ctx.dt <= 0.0f) {
        return;
    }

    processWorld(*ctx.services.world, ctx.registry, ctx.dt);
}

std::size_t MovingBlockSystem::processWorld(World& world, GameplayRegistry& registry, const float dt) {
    if (dt <= 0.0f) {
        return 0;
    }

    auto& reg = registry.registry();
    std::vector<entt::entity> completed;

    auto view = reg.view<MovingBlockTag, MovingBlockComponent, TransformComponent>();
    for (const entt::entity entity : view) {
        auto& block = view.get<MovingBlockComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);

        if (block.durationSeconds <= 0.0f) {
            failMovingBlockSystem("Moving block entity has a non-positive duration");
        }

        const float previousProgress = std::clamp(block.elapsedSeconds / block.durationSeconds, 0.0f, 1.0f);
        block.elapsedSeconds = std::min(block.elapsedSeconds + dt, block.durationSeconds);
        const float currentProgress = std::clamp(block.elapsedSeconds / block.durationSeconds, 0.0f, 1.0f);

        const glm::vec3 sourceOrigin(block.sourcePosition);
        const glm::vec3 previousOrigin = sourceOrigin + glm::vec3(block.direction) * previousProgress;
        const glm::vec3 currentOrigin = sourceOrigin + glm::vec3(block.direction) * currentProgress;
        transform.position = currentOrigin + glm::vec3(0.5f);

        applyMovingBlockCollisionToEntities(world, registry, block.stateId, previousOrigin, currentOrigin,
                                            block.direction);

        if (block.elapsedSeconds >= block.durationSeconds) {
            if (!targetCellIsReadyForPlacement(world, block)) {
                failMovingBlockSystem("Moving block target cell is occupied at placement time");
            }
            if (block.placeAtTarget) {
                world.setBlockState(block.targetPosition.x, block.targetPosition.y, block.targetPosition.z,
                                    block.stateId);
            }
            completed.push_back(entity);
        }
    }

    for (const entt::entity entity : completed) {
        if (reg.valid(entity)) {
            registry.destroy(entity);
        }
    }

    return completed.size();
}

} // namespace ecs
