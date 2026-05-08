#include "ParticleSpawnSystem.h"

#include <algorithm>
#include <random>
#include <vector>

#include "../../components/Components.h"
#include "../../util/ParticleEventBuffer.h"
#include "../../../world/Block.h"

namespace ecs {

namespace {
constexpr int kMaxParticles = 1000;
constexpr int kParticlesPerBreak = 24;

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

    const size_t incomingParticleCount = particleBus.size() * static_cast<size_t>(kParticlesPerBreak);
    pruneOverflowParticles(registry, incomingParticleCount);

    auto& raw = registry.registry();
    for (const BlockBreakParticleEvent& event : particleBus.events) {
        if (event.blockType == 0) {
            continue;
        }

        const BlockDef& blockDef = BlockRegistry::get(event.blockType);
        const int texIndices[] = {
            blockDef.texTop, blockDef.texBottom,
            blockDef.texLeft, blockDef.texRight,
            blockDef.texFront, blockDef.texBack
        };

        for (int i = 0; i < kParticlesPerBreak; ++i) {
            const entt::entity e = raw.create();
            raw.emplace<ParticleTag>(e);

            int texIdx = texIndices[static_cast<int>(randomFloat(0.0f, 5.999f))];
            if (texIdx < 0) {
                texIdx = 0;
            }

            const float uSubMin = randomFloat(0.0f, 0.5f);
            const float vSubMin = randomFloat(0.0f, 0.5f);

            auto& transform = raw.emplace<TransformComponent>(e);
            transform.position = glm::vec3(event.blockPos) + glm::vec3(
                randomFloat(0.2f, 0.8f),
                randomFloat(0.2f, 0.8f),
                randomFloat(0.2f, 0.8f)
            );
            transform.eyeHeight = 0.0f;

            auto& velocity = raw.emplace<VelocityComponent>(e);
            velocity.velocity = glm::vec3(
                randomFloat(-2.0f, 2.0f),
                randomFloat(0.5f, 3.5f),
                randomFloat(-2.0f, 2.0f)
            );

            auto& particle = raw.emplace<ParticleComponent>(e);
            particle.maxLife = randomFloat(0.4f, 0.8f);
            particle.life = particle.maxLife;
            particle.size = randomFloat(0.06f, 0.14f);
            particle.biomeTintFactor = blockDef.useBiomeTint ? 1.0f : 0.0f;
            particle.layer = static_cast<float>(texIdx);
            particle.uvMin = glm::vec2(uSubMin, vSubMin);
            particle.uvMax = glm::vec2(uSubMin + 0.5f, vSubMin + 0.5f);
        }
    }

    particleBus.clear();
}

} // namespace ecs
