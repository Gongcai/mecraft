#include "ParticleSpawnSystem.h"

#include <algorithm>
#include <random>
#include <vector>

#include "../../components/Components.h"
#include "../../util/ParticleEventBuffer.h"
#include "../../../world/block/Block.h"

namespace ecs {

namespace {
constexpr int kMaxParticles = 1000;

std::mt19937& particleRng() {
    static std::mt19937 rng{std::random_device{}()};
    return rng;
}

float randomFloat(const float minValue, const float maxValue) {
    std::uniform_real_distribution<float> dist(minValue, maxValue);
    return dist(particleRng());
}

void pruneOverflowParticles(GameplayRegistry& registry, const size_t incomingCount) {
    auto view = registry.view<ParticleTag, ParticleComponent>();
    const size_t currentCount = view.size_hint();
    if (currentCount + incomingCount <= static_cast<size_t>(kMaxParticles)) {
        return;
    }

    const size_t removeCount = currentCount + incomingCount - static_cast<size_t>(kMaxParticles);
    std::vector<entt::entity> toRemove;
    toRemove.reserve(removeCount);

    for (const entt::entity e : view) {
        toRemove.push_back(e);
        if (toRemove.size() >= removeCount) {
            break;
        }
    }

    for (const entt::entity e : toRemove) {
        if (registry.registry().valid(e)) {
            registry.destroy(e);
        }
    }
}

} // namespace

void ParticleSpawnSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;

    auto& particleBus = ensureParticleEventBus(registry);
    if (particleBus.empty()) {
        return;
    }

    size_t incomingParticleCount = 0;
    for (const BlockBreakParticleEvent& event : particleBus.events) {
        incomingParticleCount += static_cast<size_t>(std::max(0, event.particleCount));
    }
    pruneOverflowParticles(registry, incomingParticleCount);

    auto& raw = registry.registry();
    for (const BlockBreakParticleEvent& event : particleBus.events) {
        if (event.blockType == 0 || event.particleCount <= 0) {
            continue;
        }

        const BlockDef& blockDef = BlockRegistry::get(event.blockType);
        const int texIndices[] = {blockDef.faceTop.firstLayer,   blockDef.faceBottom.firstLayer,
                                  blockDef.faceLeft.firstLayer,  blockDef.faceRight.firstLayer,
                                  blockDef.faceFront.firstLayer, blockDef.faceBack.firstLayer};

        for (int i = 0; i < event.particleCount; ++i) {
            const entt::entity e = raw.create();
            raw.emplace<ParticleTag>(e);

            int texIdx = texIndices[static_cast<int>(randomFloat(0.0f, 5.999f))];
            if (texIdx < 0) {
                texIdx = 0;
            }

            const float uSubMin = randomFloat(0.0f, 0.5f);
            const float vSubMin = randomFloat(0.0f, 0.5f);

            auto& transform = raw.emplace<TransformComponent>(e);
            if (event.useWorldPos) {
                const float spread = std::max(0.0f, event.spread);
                transform.position =
                    event.worldPos +
                    glm::vec3(randomFloat(-spread, spread), randomFloat(-spread, spread), randomFloat(-spread, spread));
            } else {
                transform.position =
                    glm::vec3(event.blockPos) +
                    glm::vec3(randomFloat(0.2f, 0.8f), randomFloat(0.2f, 0.8f), randomFloat(0.2f, 0.8f));
            }
            transform.eyeHeight = 0.0f;

            auto& velocity = raw.emplace<VelocityComponent>(e);
            const float velocityScale = std::max(0.0f, event.velocityScale);
            velocity.velocity =
                glm::vec3(randomFloat(-2.0f, 2.0f) * velocityScale, randomFloat(0.5f, 3.5f) * velocityScale,
                          randomFloat(-2.0f, 2.0f) * velocityScale);

            auto& particle = raw.emplace<ParticleComponent>(e);
            const float minLife = std::max(0.01f, std::min(event.minLife, event.maxLife));
            const float maxLife = std::max(minLife, std::max(event.minLife, event.maxLife));
            const float minSize = std::max(0.01f, std::min(event.minSize, event.maxSize));
            const float maxSize = std::max(minSize, std::max(event.minSize, event.maxSize));
            particle.maxLife = randomFloat(minLife, maxLife);
            particle.life = particle.maxLife;
            particle.size = randomFloat(minSize, maxSize);
            particle.biomeTintFactor = blockDef.biomeTint != BiomeTintKind::None ? 1.0f : 0.0f;
            particle.layer = static_cast<float>(texIdx);
            particle.uvMin = glm::vec2(uSubMin, vSubMin);
            particle.uvMax = glm::vec2(uSubMin + 0.5f, vSubMin + 0.5f);
        }
    }

    particleBus.clear();
}

} // namespace ecs
