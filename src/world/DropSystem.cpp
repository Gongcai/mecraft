#include "DropSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

#include <glm/gtc/constants.hpp>

#include "World.h"

namespace {
constexpr float kGravity = 20.0f;
constexpr float kTerminalVelocity = 25.0f;
constexpr float kAxisStep = 0.2f;
constexpr float kContactEpsilon = 0.0005f;
constexpr float kHorizontalDamping = 0.92f;
constexpr float kGroundFriction = 0.86f;
constexpr float kMergeRadius = 1.75f;
constexpr float kMergeRadiusSq = kMergeRadius * kMergeRadius;
constexpr float kMergeIntervalSeconds = 0.2f;

bool canMergeDrops(const DropEntity& a, const DropEntity& b) {
    return a.kind == DropKind::Block && b.kind == DropKind::Block &&
           a.blockId == b.blockId && a.stackCount > 0 && b.stackCount > 0;
}

void absorbDrop(DropEntity& target, const DropEntity& source) {
    const uint32_t totalCount = target.stackCount + source.stackCount;
    if (totalCount == 0) {
        return;
    }

    const float targetWeight = static_cast<float>(target.stackCount);
    const float sourceWeight = static_cast<float>(source.stackCount);
    const float invTotal = 1.0f / static_cast<float>(totalCount);

    target.position = (target.position * targetWeight + source.position * sourceWeight) * invTotal;
    target.velocity = (target.velocity * targetWeight + source.velocity * sourceWeight) * invTotal;
    target.ageSeconds = std::min(target.ageSeconds, source.ageSeconds);
    target.grounded = target.grounded || source.grounded;
    target.stackCount = totalCount;
}

bool tryMergeDropAtSpawn(std::vector<DropEntity>& drops,
                         const BlockID blockId,
                         const glm::vec3& spawnPos) {
    DropEntity* bestMatch = nullptr;
    float bestDistSq = kMergeRadiusSq;

    for (DropEntity& drop : drops) {
        if (drop.kind != DropKind::Block || drop.blockId != blockId || drop.stackCount == 0) {
            continue;
        }
        const glm::vec3 delta = drop.position - spawnPos;
        const float distSq = glm::dot(delta, delta);
        if (distSq > bestDistSq) {
            continue;
        }
        bestDistSq = distSq;
        bestMatch = &drop;
    }

    if (bestMatch == nullptr) {
        return false;
    }

    bestMatch->stackCount += 1;
    bestMatch->position = (bestMatch->position + spawnPos) * 0.5f;
    bestMatch->ageSeconds = std::max(0.0f, bestMatch->ageSeconds - 1.0f);
    return true;
}

void mergeNearbyDrops(std::vector<DropEntity>& drops) {
    if (drops.size() < 2) {
        return;
    }

    for (size_t i = 0; i < drops.size(); ++i) {
        DropEntity& base = drops[i];
        if (base.stackCount == 0) {
            continue;
        }

        for (size_t j = i + 1; j < drops.size(); ++j) {
            DropEntity& candidate = drops[j];
            if (!canMergeDrops(base, candidate)) {
                continue;
            }

            const glm::vec3 delta = base.position - candidate.position;
            const float distSq = glm::dot(delta, delta);
            if (distSq > kMergeRadiusSq) {
                continue;
            }

            absorbDrop(base, candidate);
            candidate.stackCount = 0;
        }
    }

    drops.erase(std::remove_if(drops.begin(), drops.end(),
                               [](const DropEntity& drop) {
                                   return drop.stackCount == 0;
                               }),
                drops.end());
}

bool isSolidBlock(const World& world, const int x, const int y, const int z) {
    const BlockID id = world.getBlock(x, y, z);
    if (id == BlockType::AIR) {
        return false;
    }
    return BlockRegistry::get(id).isSolid;
}

bool overlapsSolid(const World& world, const glm::vec3& center, const glm::vec3& halfExtents) {
    const glm::vec3 minPos = center - halfExtents;
    const glm::vec3 maxPos = center + halfExtents;

    const int minX = static_cast<int>(std::floor(minPos.x));
    const int maxX = static_cast<int>(std::floor(maxPos.x - kContactEpsilon));
    const int minY = static_cast<int>(std::floor(minPos.y));
    const int maxY = static_cast<int>(std::floor(maxPos.y - kContactEpsilon));
    const int minZ = static_cast<int>(std::floor(minPos.z));
    const int maxZ = static_cast<int>(std::floor(maxPos.z - kContactEpsilon));

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

bool overlapsBlockAabb(const DropEntity& drop, const glm::ivec3& blockPos) {
    const glm::vec3 minPos = drop.position - drop.halfExtents;
    const glm::vec3 maxPos = drop.position + drop.halfExtents;

    const float blockMinX = static_cast<float>(blockPos.x);
    const float blockMinY = static_cast<float>(blockPos.y);
    const float blockMinZ = static_cast<float>(blockPos.z);
    const float blockMaxX = blockMinX + 1.0f;
    const float blockMaxY = blockMinY + 1.0f;
    const float blockMaxZ = blockMinZ + 1.0f;

    return minPos.x < blockMaxX && maxPos.x > blockMinX &&
           minPos.y < blockMaxY && maxPos.y > blockMinY &&
           minPos.z < blockMaxZ && maxPos.z > blockMinZ;
}

void moveAndCollideAxis(DropEntity& drop, const World& world, const int axis, const float dt) {
    const float delta = drop.velocity[axis] * dt;
    if (std::abs(delta) <= 0.0f) {
        return;
    }

    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / kAxisStep)));
    const float stepDelta = delta / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i) {
        const glm::vec3 previousPos = drop.position;
        drop.position[axis] += stepDelta;

        if (!overlapsSolid(world, drop.position, drop.halfExtents)) {
            continue;
        }

        drop.position = previousPos;
        drop.velocity[axis] = 0.0f;

        if (axis == 1 && stepDelta < 0.0f) {
            drop.grounded = true;
        }
        return;
    }
}
}

void DropSystem::spawnBlockDrop(const BlockID blockId, const glm::ivec3& blockPos) {
    if (blockId == BlockType::AIR) {
        return;
    }

    const glm::vec3 spawnPos = glm::vec3(blockPos) + glm::vec3(0.5f, 0.42f, 0.5f);
    if (tryMergeDropAtSpawn(m_drops, blockId, spawnPos)) {
        return;
    }

    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> yawDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> spinDist(1.8f, 3.2f);
    std::uniform_real_distribution<float> horizontalDist(-1.1f, 1.1f);
    std::uniform_real_distribution<float> upwardDist(2.4f, 3.3f);

    DropEntity drop;
    drop.id = m_nextId++;
    drop.kind = DropKind::Block;
    drop.blockId = blockId;
    drop.position = spawnPos;
    drop.velocity = glm::vec3(horizontalDist(rng), upwardDist(rng), horizontalDist(rng));
    drop.yawRadians = yawDist(rng);
    drop.spinSpeedRadians = spinDist(rng);

    m_drops.push_back(drop);
}

void DropSystem::onBlockPlaced(const glm::ivec3& blockPos, const World& world) {
    if (!isSolidBlock(world, blockPos.x, blockPos.y, blockPos.z) || m_drops.empty()) {
        return;
    }

    for (DropEntity& drop : m_drops) {
        if (!overlapsBlockAabb(drop, blockPos)) {
            continue;
        }

        const float baseY = static_cast<float>(blockPos.y + 1) + drop.halfExtents.y + kContactEpsilon;
        glm::vec3 resolvedPos = drop.position;
        resolvedPos.y = baseY;

        // If a low ceiling exists, keep lifting until the drop no longer intersects solids.
        constexpr int kMaxLiftSteps = 8;
        int liftSteps = 0;
        while (liftSteps < kMaxLiftSteps && overlapsSolid(world, resolvedPos, drop.halfExtents)) {
            resolvedPos.y += 1.0f;
            ++liftSteps;
        }

        drop.position = resolvedPos;
        drop.velocity.y = 0.0f;
        drop.grounded = true;
    }
}

void DropSystem::update(const float dt, const World& world) {
    if (dt <= 0.0f || m_drops.empty()) {
        return;
    }

    for (DropEntity& drop : m_drops) {
        drop.ageSeconds += dt;
        drop.yawRadians = std::fmod(drop.yawRadians + drop.spinSpeedRadians * dt, glm::two_pi<float>());

        drop.grounded = false;
        drop.velocity.y = std::max(drop.velocity.y - kGravity * dt, -kTerminalVelocity);

        moveAndCollideAxis(drop, world, 1, dt);
        moveAndCollideAxis(drop, world, 0, dt);
        moveAndCollideAxis(drop, world, 2, dt);

        if (drop.grounded) {
            drop.velocity.x *= kGroundFriction;
            drop.velocity.z *= kGroundFriction;
        } else {
            drop.velocity.x *= kHorizontalDamping;
            drop.velocity.z *= kHorizontalDamping;
        }
    }

    m_drops.erase(std::remove_if(m_drops.begin(), m_drops.end(),
                                 [](const DropEntity& drop) {
                                     return drop.ageSeconds >= drop.lifeTimeSeconds;
                                 }),
                  m_drops.end());

    if (m_drops.size() < 2) {
        return;
    }

    m_mergeAccumulator += dt;
    if (m_mergeAccumulator < kMergeIntervalSeconds) {
        return;
    }

    m_mergeAccumulator = 0.0f;
    mergeNearbyDrops(m_drops);
}

void DropSystem::clear() {
    m_drops.clear();
}

const std::vector<DropEntity>& DropSystem::getDrops() const {
    return m_drops;
}


