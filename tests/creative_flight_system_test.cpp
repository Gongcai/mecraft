#include <cmath>
#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/core/states/GameplayModeRules.h"
#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/systems/player/CharacterPhysicsSystem.h"
#include "../src/ecs/util/GameplayRuntimeContext.h"
#include "../src/physics/PhysicsSystem.h"
#include "../src/world/World.h"

namespace {

constexpr float kDt = 1.0f / 60.0f;

int fail(const char* message) {
    std::cerr << "[creative_flight_system_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    World world;
    world.init(20260418);
    loadChunks(world);

    physics::PhysicsSystem physicsSystem(&world);

    ecs::GameplayRegistry registry;
    auto& runtime = registry.ctxSet<ecs::GameplayRuntimeContext>();
    runtime.modeRules = &CreativeModeRules::instance();
    runtime.gameplayMode = GameplayMode::Creative;

    const auto entity = registry.create();
    auto& move = registry.emplace<ecs::MoveIntentComponent>(entity);
    auto& transform = registry.emplace<ecs::TransformComponent>(entity);
    auto& physicsBody = registry.emplace<ecs::PhysicsBodyComponent>(entity);
    registry.emplace<ecs::CharacterControllerComponent>(entity);
    auto& flight = registry.emplace<ecs::FlightStateComponent>(entity);

    const int surfaceY = world.getSurfaceY(0, 0);
    transform.position = glm::vec3(0.5f, static_cast<float>(surfaceY) + 8.0f, 0.5f);
    transform.eyeHeight = 1.62f;
    physicsBody.body.position = transform.position;
    physicsBody.body.eyeOffsetY = transform.eyeHeight;

    move.toggleFlightMode = true;
    ecs::CharacterPhysicsSystem::update(registry, physicsSystem, kDt);
    if (!flight.isFlying) {
        return fail("creative double jump should toggle flying on");
    }

    move = {};
    const float hoverStartY = transform.position.y;
    for (int i = 0; i < 120; ++i) {
        ecs::CharacterPhysicsSystem::update(registry, physicsSystem, kDt);
    }
    if (std::abs(transform.position.y - hoverStartY) > 0.08f) {
        return fail("flying player should hover instead of falling");
    }

    move.wantsJump = true;
    for (int i = 0; i < 30; ++i) {
        ecs::CharacterPhysicsSystem::update(registry, physicsSystem, kDt);
    }
    if (transform.position.y <= hoverStartY + 1.0f) {
        return fail("jump should move the flying player upward");
    }

    move = {};
    const float riseY = transform.position.y;
    for (int i = 0; i < 60; ++i) {
        ecs::CharacterPhysicsSystem::update(registry, physicsSystem, kDt);
    }
    if (transform.position.y < riseY - 0.08f) {
        return fail("releasing jump while flying should keep altitude");
    }

    move.wantsCrouch = true;
    for (int i = 0; i < 30; ++i) {
        ecs::CharacterPhysicsSystem::update(registry, physicsSystem, kDt);
    }
    if (transform.position.y >= riseY - 1.0f) {
        return fail("crouch should move the flying player downward");
    }

    runtime.modeRules = &SurvivalModeRules::instance();
    runtime.gameplayMode = GameplayMode::Survival;
    move = {};

    const float fallStartY = transform.position.y;
    for (int i = 0; i < 30; ++i) {
        ecs::CharacterPhysicsSystem::update(registry, physicsSystem, kDt);
    }
    if (flight.isFlying) {
        return fail("leaving creative mode should disable flight");
    }
    if (transform.position.y >= fallStartY - 0.25f) {
        return fail("survival mode should restore gravity after flight");
    }

    std::cout << "[creative_flight_system_test] PASS\n";
    return EXIT_SUCCESS;
}
