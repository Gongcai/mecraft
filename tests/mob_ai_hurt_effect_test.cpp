#include "ecs/GameplayRegistry.h"
#include "ecs/GameplayServices.h"
#include "ecs/SystemContext.h"
#include "ecs/components/Components.h"
#include "ecs/systems/combat/DamageSystem.h"
#include "ecs/systems/combat/HurtEffectDecaySystem.h"
#include "ecs/systems/mob/MobAISystem.h"
#include "ecs/util/DamageEventBuffer.h"
#include "world/World.h"
#include "world/block/Block.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::abort();
    }
}

static ecs::SystemContext makeContext(ecs::GameplayRegistry& registry,
                                      ecs::GameplayServices& services,
                                      const float dt,
                                      const uint64_t tickIndex = 0) {
    return ecs::SystemContext{registry, services, dt, tickIndex};
}

static void loadWorldAround(World& world, const glm::vec3& center) {
    world.setRenderDistance(1);
    world.init(20260630);
    world.updateForInitialLoad(center, 0.05f);
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
    ai.target = player;
    ai.targetMemoryRemaining = ai.lineOfSightMemorySeconds;
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

static void testPassiveMobDoesNotTargetPlayer() {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    auto& raw = registry.registry();

    const entt::entity player = raw.create();
    raw.emplace<ecs::LocalPlayerTag>(player);
    raw.emplace<ecs::TransformComponent>(player, glm::vec3(2.0f, 64.0f, 0.0f), 1.62f);
    raw.emplace<ecs::HealthComponent>(player, 20, 20);

    const entt::entity mob = raw.create();
    raw.emplace<ecs::MobTag>(mob);
    raw.emplace<ecs::TransformComponent>(mob, glm::vec3(0.0f, 64.0f, 0.0f), 1.1f);
    auto& ai = raw.emplace<ecs::MobAIComponent>(mob);
    ai.targetsPlayers = false;
    ai.target = player;
    ai.wanderTimer = 10.0f;
    ai.wanderDir = glm::vec2(0.0f);
    raw.emplace<ecs::MoveIntentComponent>(mob);

    ecs::MobAISystem system;
    auto ctx = makeContext(registry, services, 0.05f);
    system.update(ctx);

    const auto& updatedAi = raw.get<ecs::MobAIComponent>(mob);
    const auto& move = raw.get<ecs::MoveIntentComponent>(mob);
    require(updatedAi.target == entt::null,
            "passive mob AI should clear player targets");
    require(updatedAi.state != ecs::MobAIComponent::State::Pursue &&
            updatedAi.state != ecs::MobAIComponent::State::Attack,
            "passive mob AI should not pursue or attack players");
    require(std::fabs(move.move.x) < 0.001f && std::fabs(move.move.y) < 0.001f,
            "passive mob AI should not move toward a cleared player target");
}

static void testMobAISkipsEntitiesOutsideSimulationDistance() {
    World world;
    world.setSimulationDistance(1);
    world.ticketManager().updatePlayerPosition(0, 0);

    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    services.world = &world;
    auto& raw = registry.registry();

    const entt::entity player = raw.create();
    raw.emplace<ecs::LocalPlayerTag>(player);
    raw.emplace<ecs::TransformComponent>(player, glm::vec3(50.0f, 64.0f, 0.0f), 1.62f);
    raw.emplace<ecs::HealthComponent>(player, 20, 20);

    const entt::entity mob = raw.create();
    raw.emplace<ecs::MobTag>(mob);
    raw.emplace<ecs::TransformComponent>(mob, glm::vec3(48.0f, 64.0f, 0.0f), 1.62f);
    auto& ai = raw.emplace<ecs::MobAIComponent>(mob);
    ai.acquisitionRange = 32.0f;
    ai.loseTargetRange = 40.0f;
    ai.wanderTimer = -1.0f;
    raw.emplace<ecs::MoveIntentComponent>(mob);

    ecs::MobAISystem system;
    auto ctx = makeContext(registry, services, 0.05f);
    system.update(ctx);

    const auto& updatedAi = raw.get<ecs::MobAIComponent>(mob);
    const auto& move = raw.get<ecs::MoveIntentComponent>(mob);
    require(updatedAi.target == entt::null,
            "mob AI should not acquire targets outside simulation distance");
    require(updatedAi.wanderTimer == -1.0f,
            "mob AI timers should not advance outside simulation distance");
    require(std::fabs(move.move.x) < 0.001f && std::fabs(move.move.y) < 0.001f,
            "mob AI should not write movement outside simulation distance");
}

static void testMobDoesNotAcquireTargetBehindWall() {
    World world;
    loadWorldAround(world, glm::vec3(0.5f, 64.0f, 0.5f));
    world.setBlock(2, 65, 0, BlockRegistry::requireIdByName("minecraft:stone"));

    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    services.worldView = &world;
    auto& raw = registry.registry();

    const entt::entity player = raw.create();
    raw.emplace<ecs::LocalPlayerTag>(player);
    raw.emplace<ecs::TransformComponent>(player, glm::vec3(4.5f, 64.0f, 0.5f), 1.62f);
    raw.emplace<ecs::HealthComponent>(player, 20, 20);

    const entt::entity mob = raw.create();
    raw.emplace<ecs::MobTag>(mob);
    raw.emplace<ecs::TransformComponent>(mob, glm::vec3(0.5f, 64.0f, 0.5f), 1.62f);
    auto& ai = raw.emplace<ecs::MobAIComponent>(mob);
    ai.acquisitionRange = 10.0f;
    ai.loseTargetRange = 12.0f;
    ai.wanderTimer = 10.0f;
    ai.wanderDir = glm::vec2(0.0f);
    raw.emplace<ecs::MoveIntentComponent>(mob);

    ecs::MobAISystem system;
    auto ctx = makeContext(registry, services, 0.05f);
    system.update(ctx);

    const auto& updatedAi = raw.get<ecs::MobAIComponent>(mob);
    const auto& move = raw.get<ecs::MoveIntentComponent>(mob);
    require(updatedAi.target == entt::null,
            "mob AI should not acquire players hidden behind solid blocks");
    require(std::fabs(move.move.x) < 0.001f && std::fabs(move.move.y) < 0.001f,
            "mob AI should not pursue a player it cannot see");
}

static void testMobRetaliatesAgainstDamageSource() {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    auto& raw = registry.registry();

    const entt::entity player = raw.create();
    raw.emplace<ecs::LocalPlayerTag>(player);
    raw.emplace<ecs::TransformComponent>(player, glm::vec3(3.0f, 64.0f, 0.0f), 1.62f);
    raw.emplace<ecs::HealthComponent>(player, 20, 20);

    const entt::entity mob = raw.create();
    raw.emplace<ecs::MobTag>(mob);
    raw.emplace<ecs::TransformComponent>(mob, glm::vec3(0.0f, 64.0f, 0.0f), 1.62f);
    auto& ai = raw.emplace<ecs::MobAIComponent>(mob);
    ai.targetsPlayers = false;
    ai.retaliates = true;
    ai.hearingRange = 8.0f;
    ai.wanderTimer = 10.0f;
    raw.emplace<ecs::MoveIntentComponent>(mob);
    raw.emplace<ecs::HealthComponent>(mob, 20, 20);

    ecs::ensureDamageEventBus(registry).push({mob, player, 4});

    ecs::DamageSystem damageSystem;
    auto damageCtx = makeContext(registry, services, 0.05f, 21);
    damageSystem.update(damageCtx);

    require(raw.get<ecs::HealthComponent>(mob).current == 16,
            "damage system should apply the incoming hit before retaliation");
    require(raw.get<ecs::LastDamageSourceComponent>(mob).source == player,
            "damage system should remember the source of a successful hit");

    ecs::MobAISystem aiSystem;
    auto aiCtx = makeContext(registry, services, 0.05f, 22);
    aiSystem.update(aiCtx);

    const auto& updatedAi = raw.get<ecs::MobAIComponent>(mob);
    const auto& move = raw.get<ecs::MoveIntentComponent>(mob);
    require(updatedAi.target == player,
            "retaliating mob should target the player that damaged it");
    require(updatedAi.state == ecs::MobAIComponent::State::Pursue,
            "retaliating mob should pursue a remembered damage source outside attack range");
    require(move.move.x > 0.0f && std::fabs(move.move.y) < 0.001f,
            "retaliating mob should move toward the damage source");
}

static void testMobJumpsWhenBlockedDuringPursuit() {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    auto& raw = registry.registry();

    const entt::entity player = raw.create();
    raw.emplace<ecs::LocalPlayerTag>(player);
    raw.emplace<ecs::TransformComponent>(player, glm::vec3(5.0f, 64.0f, 0.0f), 1.62f);
    raw.emplace<ecs::HealthComponent>(player, 20, 20);

    const entt::entity mob = raw.create();
    raw.emplace<ecs::MobTag>(mob);
    raw.emplace<ecs::TransformComponent>(mob, glm::vec3(0.0f, 64.0f, 0.0f), 1.62f);
    auto& ai = raw.emplace<ecs::MobAIComponent>(mob);
    ai.target = player;
    ai.targetMemoryRemaining = ai.lineOfSightMemorySeconds;
    ai.loseTargetRange = 12.0f;
    raw.emplace<ecs::MoveIntentComponent>(mob);
    auto& physics = raw.emplace<ecs::PhysicsBodyComponent>(mob);
    physics.body.isGrounded = true;
    physics.body.hitWall = true;

    ecs::MobAISystem system;
    auto ctx = makeContext(registry, services, 0.05f);
    system.update(ctx);

    const auto& move = raw.get<ecs::MoveIntentComponent>(mob);
    require(move.wantsJump,
            "mob AI should request a jump when a pursuing mob is blocked by a wall");
    require(move.move.x > 0.0f,
            "blocked mob should keep pursuing its target while trying to get unstuck");
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
    BlockRegistry::init(nullptr);
    testMobYawUsesCameraConvention();
    testPassiveMobDoesNotTargetPlayer();
    testMobAISkipsEntitiesOutsideSimulationDistance();
    testMobDoesNotAcquireTargetBehindWall();
    testMobRetaliatesAgainstDamageSource();
    testMobJumpsWhenBlockedDuringPursuit();
    testHurtEffectDecayClearsExpiredPendingState();
    std::printf("All Mob AI / HurtEffect tests passed!\n");
    return 0;
}
