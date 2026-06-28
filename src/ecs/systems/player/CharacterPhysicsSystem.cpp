#include "CharacterPhysicsSystem.h"

#include <algorithm>

#include "../../components/Components.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../util/SimulationDistance.h"
#include "../../../physics/PhysicsSystem.h"

namespace ecs {
namespace {

float lerp(const float a, const float b, const float t) {
    return a + (b - a) * t;
}

bool isCreativeModeActive(const GameplayRegistry& registry) {
    if (!registry.ctxHas<GameplayRuntimeContext>()) {
        return false;
    }
    return registry.ctxGet<GameplayRuntimeContext>().gameplayMode == GameplayMode::Creative;
}

MoveIntent toLegacyIntent(const MoveIntentComponent& moveIntent, const bool isFlying) {
    MoveIntent legacyIntent{};
    legacyIntent.move = moveIntent.move;
    legacyIntent.wantsJump = moveIntent.wantsJump;
    legacyIntent.wantsSprint = moveIntent.wantsSprint;
    legacyIntent.wantsCrouch = moveIntent.wantsCrouch;
    legacyIntent.isFlying = isFlying;
    return legacyIntent;
}

void updateEyeHeight(const MoveIntentComponent& moveIntent,
                     const bool isFlying,
                     const PhysicsBodyComponent& physicsBody,
                     const CharacterControllerComponent& controller,
                     TransformComponent& transform,
                     const float dt) {
    if (!controller.crouchChangesEyeHeight) {
        transform.eyeHeight = controller.standEyeHeight;
        return;
    }

    const float targetEyeHeight = physicsBody.body.isGrounded && moveIntent.wantsCrouch && !isFlying
        ? controller.crouchEyeHeight
        : controller.standEyeHeight;
    transform.eyeHeight = lerp(transform.eyeHeight, targetEyeHeight, dt * controller.eyeHeightLerpSpeed);
}

void capturePreviousTransform(TransformInterpolationComponent* interpolation,
                              const TransformComponent& transform) {
    if (interpolation == nullptr) {
        return;
    }
    interpolation->previousPosition = transform.position;
    interpolation->previousEyeHeight = transform.eyeHeight;
    interpolation->initialized = true;
}

} // namespace

void CharacterPhysicsSystem::update(SystemContext& ctx) {
    if (!ctx.services.physicsSystem) return;
    auto& registry = ctx.registry;
    auto& physicsSystem = *ctx.services.physicsSystem;
    const float dt = ctx.dt;

    auto view = registry.view<MoveIntentComponent, TransformComponent, PhysicsBodyComponent>();
    for (const auto entity : view) {
        if (!simulation::isEntityTicking(ctx, entity)) {
            continue;
        }

        const auto& moveIntent = view.get<MoveIntentComponent>(entity);
        auto& transform = view.get<TransformComponent>(entity);
        auto& physicsBody = view.get<PhysicsBodyComponent>(entity);
        const auto* controller = registry.try_get<CharacterControllerComponent>(entity);
        auto* landing = registry.try_get<LandingStateComponent>(entity);
        auto* grounded = registry.try_get<GroundedStateComponent>(entity);
        auto* velocity = registry.try_get<VelocityComponent>(entity);
        auto* flightState = registry.try_get<FlightStateComponent>(entity);
        auto* transformInterpolation = registry.try_get<TransformInterpolationComponent>(entity);

        const bool wasGrounded = physicsBody.body.isGrounded;
        const bool creativeModeActive = isCreativeModeActive(registry);
        bool isFlying = flightState != nullptr && flightState->isFlying;

        if (flightState != nullptr) {
            if (!creativeModeActive) {
                flightState->isFlying = false;
            } else if (moveIntent.toggleFlightMode) {
                // Toggling flight clears carry-over vertical velocity so the mode switch feels intentional.
                flightState->isFlying = !flightState->isFlying;
                physicsBody.body.velocity.y = 0.0f;
                if (flightState->isFlying) {
                    physicsBody.body.isGrounded = false;
                }
            }
            isFlying = flightState->isFlying;
        }

        capturePreviousTransform(transformInterpolation, transform);

        physicsBody.body.position = transform.position;
        physicsBody.body.eyeOffsetY = transform.eyeHeight;

        if (controller != nullptr) {
            physicsSystem.updateBody(physicsBody.body, toLegacyIntent(moveIntent, isFlying), dt, controller->tuning);
            updateEyeHeight(moveIntent, isFlying, physicsBody, *controller, transform, dt);
            physicsBody.body.eyeOffsetY = transform.eyeHeight;
        } else {
            physicsSystem.updateBody(physicsBody.body, toLegacyIntent(moveIntent, isFlying), dt);
        }

        transform.position = physicsBody.body.position;

        if (landing != nullptr) {
            landing->justLanded = physicsBody.body.isGrounded && !wasGrounded;
            landing->impactSpeed = landing->justLanded ? physicsBody.body.landingImpactSpeed : 0.0f;
        }
        if (grounded != nullptr) {
            grounded->grounded = physicsBody.body.isGrounded;
        }
        if (velocity != nullptr) {
            velocity->velocity = physicsBody.body.velocity;
        }
    }
}

} // namespace ecs
