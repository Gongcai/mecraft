#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/GameplayServices.h"
#include "../src/ecs/SystemContext.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/systems/player/OxygenDepletionSystem.h"
#include "../src/ecs/util/GameplayRuntimeContext.h"
#include "../src/world/World.h"
#include "../src/world/fluid/FluidState.h"

namespace {

class AirTestWorld final : public World {
public:
    BlockStateId fluidState = NULL_BLOCK_STATE;

    /// Return the configured fluid state for deterministic player air tests.
    /// @param x World-space block coordinate on the X axis.
    /// @param y World-space block coordinate on the Y axis.
    /// @param z World-space block coordinate on the Z axis.
    /// @return The test fluid state.
    [[nodiscard]] BlockStateId getFluidState(int, int, int) const override { return fluidState; }
};

/// Report a failed condition and return a test failure code.
/// @param message Explanation shown when an assertion fails.
/// @return The process failure code.
int fail(const char* message) {
    std::cerr << "[oxygen_depletion_system_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

/// Exercise water consumption, surface recovery, drowning damage, and creative immunity.
/// @return EXIT_SUCCESS when all oxygen behavior checks pass, otherwise EXIT_FAILURE.
int main() {
    BlockRegistry::init(nullptr);

    AirTestWorld world;
    world.fluidState = FluidState::makeWater(0, false);

    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    services.world = &world;

    const entt::entity player = registry.create();
    registry.emplace<ecs::LocalPlayerTag>(player);
    auto& transform = registry.emplace<ecs::TransformComponent>(player);
    transform.position = glm::vec3(0.5f, 0.0f, 0.5f);
    transform.eyeHeight = 1.62f;
    auto& air = registry.emplace<ecs::AirSupplyComponent>(player);
    auto& health = registry.emplace<ecs::HealthComponent>(player);
    auto& hurt = registry.emplace<ecs::HurtEffectComponent>(player);

    ecs::OxygenDepletionSystem system;
    uint64_t tick = 0;
    const auto update = [&](const float dt) {
        ecs::SystemContext context{registry, services, dt, tick++};
        system.update(context);
    };

    update(1.0f);
    if (air.current != 280) {
        return fail("water should consume one air unit per game tick");
    }

    world.fluidState = NULL_BLOCK_STATE;
    air.current = 250;
    air.gameTickRemainder = 0.0;
    update(0.5f);
    if (air.current != 290) {
        return fail("surface recovery should restore four air units per game tick");
    }

    air.current = 1;
    air.gameTickRemainder = 0.0;
    air.drowningTickRemainder = 0.0;
    world.fluidState = FluidState::makeWater(0, false);
    update(0.05f);
    if (air.current != 0 || health.current != 20) {
        return fail("drowning damage should wait until the air supply is exhausted");
    }
    for (int i = 0; i < 19; ++i) {
        update(0.05f);
    }
    if (health.current != 20) {
        return fail("drowning damage should wait twenty exhausted game ticks");
    }
    update(0.05f);
    if (health.current != 18 || !hurt.classicHurtEffectPending) {
        return fail("an exhausted player should take two health points of drowning damage");
    }

    auto& runtime = registry.ctxSet<ecs::GameplayRuntimeContext>();
    runtime.gameplayMode = GameplayMode::Creative;
    air.current = 0;
    air.gameTickRemainder = 0.0;
    air.drowningTickRemainder = 0.0;
    const int creativeHealth = health.current;
    update(1.0f);
    if (air.current != air.max || health.current != creativeHealth) {
        return fail("creative players should keep full air and ignore drowning damage");
    }

    std::cout << "[oxygen_depletion_system_test] PASS\n";
    return EXIT_SUCCESS;
}
