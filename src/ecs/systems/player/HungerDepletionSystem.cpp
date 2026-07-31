#include "HungerDepletionSystem.h"

#include "../../components/Components.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../../world/World.h"

namespace ecs {

namespace {
constexpr double kHungerInterval = 100.0; // game seconds per food point
} // namespace

void HungerDepletionSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;

    if (!ctx.services.world)
        return;

    // No hunger depletion in creative mode
    if (registry.ctxHas<GameplayRuntimeContext>()) {
        if (registry.ctxGet<GameplayRuntimeContext>().gameplayMode == GameplayMode::Creative) {
            return;
        }
    }

    const double gameTime = ctx.services.world->getDayNightSystem().getTotalGameTime();

    auto view = registry.view<LocalPlayerTag, FoodComponent>();
    for (auto e : view) {
        auto& food = view.get<FoodComponent>(e);

        if (food.current <= 0)
            continue;

        const double elapsed = gameTime - food.lastHungerTick;
        const int ticks = static_cast<int>(elapsed / kHungerInterval);

        if (ticks > 0) {
            food.current = std::max(0, food.current - ticks);
            food.lastHungerTick += ticks * kHungerInterval;
        }
    }
}

} // namespace ecs
