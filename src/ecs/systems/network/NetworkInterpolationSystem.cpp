#include "NetworkInterpolationSystem.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace ecs {
namespace {

constexpr float kTwoPi = 6.28318530718f;

float smoothingAlpha(const float speed, const float dt) {
    if (speed <= 0.0f) {
        return 1.0f;
    }
    return std::clamp(1.0f - std::exp(-speed * dt), 0.0f, 1.0f);
}

float lerp(const float a, const float b, const float t) {
    return a + (b - a) * t;
}

float shortestDeltaDegrees(const float from, const float to) {
    float delta = std::fmod(to - from + 180.0f, 360.0f);
    if (delta < 0.0f) {
        delta += 360.0f;
    }
    return delta - 180.0f;
}

float shortestDeltaRadians(const float from, const float to) {
    float delta = std::fmod(to - from + 3.14159265359f, kTwoPi);
    if (delta < 0.0f) {
        delta += kTwoPi;
    }
    return delta - 3.14159265359f;
}

float lerpAngleDegrees(const float from, const float to, const float t) {
    return from + shortestDeltaDegrees(from, to) * t;
}

float lerpAngleRadians(const float from, const float to, const float t) {
    return from + shortestDeltaRadians(from, to) * t;
}

} // namespace

void NetworkInterpolationSystem::update(SystemContext& ctx) {
    if (ctx.dt <= 0.0f) {
        return;
    }

    auto& reg = ctx.registry.registry();
    auto view = reg.view<NetworkInterpolationComponent, TransformComponent>();
    for (const entt::entity entity : view) {
        auto& interpolation = view.get<NetworkInterpolationComponent>(entity);
        if (!interpolation.hasTarget) {
            continue;
        }

        auto& transform = view.get<TransformComponent>(entity);
        const float distance = glm::length(interpolation.targetPosition - transform.position);
        if (distance > interpolation.snapDistance || distance < 0.001f || interpolation.positionLerpSpeed <= 0.0f) {
            transform.position = interpolation.targetPosition;
        } else {
            const float positionAlpha = smoothingAlpha(interpolation.positionLerpSpeed, ctx.dt);
            transform.position += (interpolation.targetPosition - transform.position) * positionAlpha;
        }

        const float rotationAlpha = smoothingAlpha(interpolation.rotationLerpSpeed, ctx.dt);
        if (auto* spin = reg.try_get<SpinVisualComponent>(entity)) {
            spin->yawRadians = lerpAngleRadians(spin->yawRadians, interpolation.targetYaw, rotationAlpha);
        }
        if (auto* mobAI = reg.try_get<MobAIComponent>(entity)) {
            mobAI->yaw = lerpAngleDegrees(mobAI->yaw, interpolation.targetYaw, rotationAlpha);
        }
        if (auto* camera = reg.try_get<CameraStateComponent>(entity)) {
            camera->yaw = lerpAngleDegrees(camera->yaw, interpolation.targetYaw, rotationAlpha);
            camera->pitch = lerp(camera->pitch, interpolation.targetPitch, rotationAlpha);
        }
    }
}

} // namespace ecs
