#include "FallingBlockInterpolateSystem.h"

#include <algorithm>

#include "../../components/Components.h"
#include "../../util/GameTickClock.h"

namespace ecs {

namespace {
// Match GameTickClock::DEFAULT_TICK_RATE (20 TPS). FallingBlockComponent.tickAccumulator
// counts seconds since the last tick; alpha = accumulator / tickInterval in [0,1].
constexpr float kTickIntervalSeconds = static_cast<float>(1.0 / GameTickClock::DEFAULT_TICK_RATE);
} // namespace

void FallingBlockInterpolateSystem::update(SystemContext& ctx) {
    if (ctx.dt <= 0.0f)
        return;
    auto& reg = ctx.registry.registry();

    auto view = reg.view<FallingBlockComponent, TransformComponent>();
    for (const entt::entity entity : view) {
        auto& block = view.get<FallingBlockComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);

        // Accumulate time since the last tick (capped at one tick interval).
        block.tickAccumulator = std::min(block.tickAccumulator + ctx.dt, kTickIntervalSeconds);
        const float alpha = block.tickAccumulator / kTickIntervalSeconds;

        // Lerp from the previous cell center toward the current cell center.
        const glm::vec3 prevCenter = glm::vec3(block.prevGridPosition) + glm::vec3(0.5f);
        const glm::vec3 currCenter = glm::vec3(block.gridPosition) + glm::vec3(0.5f);
        transform.position = prevCenter + (currCenter - prevCenter) * alpha;
    }
}

} // namespace ecs
