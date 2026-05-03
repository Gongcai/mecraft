#include "ItemPhysicsSystem.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "../../components/Components.h"
#include "../../util/DropPhysicsHelpers.h"
#include "../../../world/World.h"

namespace ecs {

namespace {
constexpr float kGravity = 20.0f;
constexpr float kTerminalVelocity = 25.0f;
constexpr float kHorizontalDamping = 0.92f;
constexpr float kGroundFriction = 0.86f;
}

void ItemPhysicsSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) return;
    auto& registry = ctx.registry;
    const World& world = *ctx.services.world;
    const float dt = ctx.dt;

    if (dt <= 0.0f) {
        return;
    }

    auto view = registry.view<DropItemTag,
                              TransformComponent,
                              VelocityComponent,
                              BoundsComponent,
                              SpinVisualComponent,
                              GroundedStateComponent>();
    for (const entt::entity e : view) {
        auto& transform = view.get<TransformComponent>(e);
        auto& velocity = view.get<VelocityComponent>(e);
        const auto& bounds = view.get<BoundsComponent>(e);
        auto& spin = view.get<SpinVisualComponent>(e);
        auto& grounded = view.get<GroundedStateComponent>(e);

        spin.yawRadians = std::fmod(spin.yawRadians + spin.spinSpeedRadians * dt, glm::two_pi<float>());
        grounded.grounded = false;
        velocity.velocity.y = std::max(velocity.velocity.y - kGravity * dt, -kTerminalVelocity);

        drop_detail::moveAndCollideAxis(transform, velocity, bounds, grounded, world, 1, dt);
        drop_detail::moveAndCollideAxis(transform, velocity, bounds, grounded, world, 0, dt);
        drop_detail::moveAndCollideAxis(transform, velocity, bounds, grounded, world, 2, dt);

        if (grounded.grounded) {
            velocity.velocity.x *= kGroundFriction;
            velocity.velocity.z *= kGroundFriction;
        } else {
            velocity.velocity.x *= kHorizontalDamping;
            velocity.velocity.z *= kHorizontalDamping;
        }
    }
}

} // namespace ecs
