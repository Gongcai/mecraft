#include "ecs/GameplayRegistry.h"
#include "ecs/GameplayServices.h"
#include "ecs/SystemContext.h"
#include "ecs/components/Components.h"
#include "ecs/systems/combat/HurtEffectDecaySystem.h"
#include "ecs/systems/mob/MobAISystem.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::abort();
    }
}

static ecs::SystemContext makeContext(ecs::GameplayRegistry& registry, ecs::GameplayServices& services, const float dt) {
    return ecs::SystemContext{registry, services, dt, 0};
}

static void testMobYawUsesCameraConvention() {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    auto& raw = registry.registry();

    const entt::entity player = raw.create();
    raw.emplace<ecs::LocalPlayerTag>(player);
    raw.emplace<ecs::TransformComponent>(player, glm::vec3(10.0f, 64.0f, 0.0f), 1.62f);
    raw.emplace<ecs::HealthComponent>(player, 20, 20);

    const entt::entity mob = raw.create();
    raw.emplace<ecs::MobTag>(mob);
    raw.emplace<ecs::TransformComponent>(mob, glm::vec3(0.0f, 64.0f, 0.0f), 1.62f);
    auto& ai = raw.emplace<ecs::MobAIComponent>(mob);
    ai.acquisitionRange = 32.0f;
    ai.loseTargetRange = 40.0f;
    raw.emplace<ecs::MoveIntentComponent>(mob);

    ecs::MobAISystem system;
    auto ctx = makeContext(registry, services, 0.05f);
    system.update(ctx);

    const auto& updatedAi = raw.get<ecs::MobAIComponent>(mob);
    const auto& move = raw.get<ecs::MoveIntentComponent>(mob);
    require(std::fabs(updatedAi.yaw - 0.0f) < 0.001f,
            "mob yaw should face +X target using camera yaw convention");
    require(move.move.x > 0.0f && std::fabs(move.move.y) < 0.001f,
            "mob should move toward +X target");
}

static void testHurtEffectDecayClearsExpiredPendingState() {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    auto& raw = registry.registry();

    const entt::entity entity = raw.create();
    auto& hurt = raw.emplace<ecs::HurtEffectComponent>(entity);
    hurt.flashDurationSeconds = 0.18f;
    hurt.triggerClassicHurt();

    ecs::HurtEffectDecaySystem system;
    auto ctx = makeContext(registry, services, 0.25f);
    system.update(ctx);

    const auto& decayed = raw.get<ecs::HurtEffectComponent>(entity);
    require(decayed.flashSecondsRemaining == 0.0f,
            "hurt flash should decay to zero");
    require(!decayed.classicHurtEffectPending,
            "expired hurt flash should clear pending state");
}

int main() {
    testMobYawUsesCameraConvention();
    testHurtEffectDecayClearsExpiredPendingState();
    std::printf("All Mob AI / HurtEffect tests passed!\n");
    return 0;
}
