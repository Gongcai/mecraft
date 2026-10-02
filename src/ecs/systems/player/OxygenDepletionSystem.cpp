#include "OxygenDepletionSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../../components/Components.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../../world/World.h"
#include "../../../world/fluid/FluidState.h"

namespace ecs {
namespace {

constexpr double kGameTickSeconds = 1.0 / 20.0;
constexpr double kDrowningDamageIntervalTicks = 20.0;
constexpr int kAirConsumedPerTick = 1;
constexpr int kAirRecoveredPerTick = 4;
constexpr int kDrowningDamage = 2;

/// Determine whether a player is creative in the current runtime or its per-player state.
/// @param registry ECS registry containing runtime and player mode state.
/// @param entity Player entity to inspect.
/// @return True when the player is in creative mode.
bool isCreativePlayer(const GameplayRegistry& registry, const entt::entity entity) {
    if (registry.ctxHas<GameplayRuntimeContext>() &&
        registry.ctxGet<GameplayRuntimeContext>().gameplayMode == GameplayMode::Creative) {
        return true;
    }
    const auto* mode = registry.try_get<PlayerModeComponent>(entity);
    return mode != nullptr && mode->creative;
}

/// Determine whether the player's eyes are below the water surface in their current cell.
/// @param world World used to sample the fluid cell.
/// @param transform Player position and eye height.
/// @return True when water covers the player's eye position.
bool isEyesSubmerged(const World& world, const TransformComponent& transform) {
    const glm::vec3 eyePosition = transform.position + glm::vec3(0.0f, transform.eyeHeight, 0.0f);
    const int blockX = static_cast<int>(std::floor(eyePosition.x));
    const int blockY = static_cast<int>(std::floor(eyePosition.y));
    const int blockZ = static_cast<int>(std::floor(eyePosition.z));
    const BlockStateId fluidState = world.getFluidState(blockX, blockY, blockZ);
    if (fluidState == NULL_BLOCK_STATE) {
        return false;
    }

    const DecodedFluid fluid = FluidState::decode(fluidState);
    const float eyeHeightInBlock = eyePosition.y - static_cast<float>(blockY);
    return fluid.kind == FluidKind::Water && eyeHeightInBlock < FluidState::surfaceHeight(fluidState);
}

} // namespace

/// Update player air supply, surface recovery, and drowning damage.
/// @param ctx Fixed-step system context containing the registry, world, and elapsed time.
void OxygenDepletionSystem::update(SystemContext& ctx) {
    // Consume one air unit per game tick underwater, recover four at the surface, and drown for two health every
    // twenty exhausted ticks. Fractional tick time is retained so fixed-step frame rates do not change the rate.
    const World* world = ctx.services.world.get();
    auto& registry = ctx.registry;
    auto view = registry.view<LocalPlayerTag, TransformComponent, AirSupplyComponent>();

    for (const entt::entity entity : view) {
        auto& air = view.get<AirSupplyComponent>(entity);
        air.max = std::max(1, air.max);
        air.current = std::clamp(air.current, 0, air.max);

        if (isCreativePlayer(registry, entity)) {
            air.current = air.max;
            air.gameTickRemainder = 0.0;
            air.drowningTickRemainder = 0.0;
            continue;
        }

        const auto* health = registry.try_get<HealthComponent>(entity);
        if (health != nullptr && health->current <= 0) {
            continue;
        }
        if (world == nullptr) {
            continue;
        }

        const bool submerged = isEyesSubmerged(*world, view.get<TransformComponent>(entity));
        air.gameTickRemainder += std::max(0.0f, ctx.dt);
        const auto elapsedTicks = static_cast<int64_t>(std::floor(air.gameTickRemainder / kGameTickSeconds));
        if (elapsedTicks == 0) {
            if (!submerged) {
                air.drowningTickRemainder = 0.0;
            }
            continue;
        }
        air.gameTickRemainder -= static_cast<double>(elapsedTicks) * kGameTickSeconds;

        if (!submerged) {
            air.current = static_cast<int>(std::min<int64_t>(air.max,
                static_cast<int64_t>(air.current) + elapsedTicks * kAirRecoveredPerTick));
            air.drowningTickRemainder = 0.0;
            continue;
        }

        const int64_t ticksUntilEmpty = std::min<int64_t>(elapsedTicks, air.current / kAirConsumedPerTick);
        air.current -= static_cast<int>(ticksUntilEmpty * kAirConsumedPerTick);
        const int64_t drowningTicks = elapsedTicks - ticksUntilEmpty;
        if (air.current > 0) {
            air.drowningTickRemainder = 0.0;
            continue;
        }

        air.drowningTickRemainder += static_cast<double>(drowningTicks);
        const int damageCount = static_cast<int>(air.drowningTickRemainder / kDrowningDamageIntervalTicks);
        if (damageCount <= 0) {
            continue;
        }
        air.drowningTickRemainder -= static_cast<double>(damageCount) * kDrowningDamageIntervalTicks;

        if (auto* mutableHealth = registry.try_get<HealthComponent>(entity)) {
            mutableHealth->current = std::max(0, mutableHealth->current - damageCount * kDrowningDamage);
        }
        if (auto* hurt = registry.try_get<HurtEffectComponent>(entity)) {
            hurt->triggerClassicHurt();
        }
    }
}

} // namespace ecs
