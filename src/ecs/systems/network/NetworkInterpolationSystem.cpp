#include "NetworkInterpolationSystem.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace ecs {
namespace {

constexpr float kTwoPi = 6.28318530718f;

struct SampledPose {
    glm::vec3 position{0.0f};
    float yawFrom = 0.0f;
    float yawTo = 0.0f;
    float pitchFrom = 0.0f;
    float pitchTo = 0.0f;
    float alpha = 0.0f;
};

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

void applyPose(entt::registry& reg, const entt::entity entity, TransformComponent& transform, const SampledPose& pose) {
    transform.position = pose.position;
    if (auto* spin = reg.try_get<SpinVisualComponent>(entity)) {
        spin->yawRadians = lerpAngleRadians(pose.yawFrom, pose.yawTo, pose.alpha);
    }
    if (auto* mobAI = reg.try_get<MobAIComponent>(entity)) {
        mobAI->yaw = lerpAngleDegrees(pose.yawFrom, pose.yawTo, pose.alpha);
    }
    if (auto* camera = reg.try_get<CameraStateComponent>(entity)) {
        camera->yaw = lerpAngleDegrees(pose.yawFrom, pose.yawTo, pose.alpha);
        camera->pitch = lerp(pose.pitchFrom, pose.pitchTo, pose.alpha);
    }
}

void applyFallbackTarget(entt::registry& reg, const entt::entity entity, TransformComponent& transform,
                         const NetworkInterpolationComponent& interpolation, const float dt) {
    const float distance = glm::length(interpolation.targetPosition - transform.position);
    if (distance > interpolation.snapDistance || distance < 0.001f || interpolation.positionLerpSpeed <= 0.0f) {
        transform.position = interpolation.targetPosition;
    } else {
        const float positionAlpha = smoothingAlpha(interpolation.positionLerpSpeed, dt);
        transform.position += (interpolation.targetPosition - transform.position) * positionAlpha;
    }

    const float rotationAlpha = smoothingAlpha(interpolation.rotationLerpSpeed, dt);
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

bool sampleSnapshotBuffer(NetworkInterpolationComponent& interpolation, const float dt, SampledPose& outPose) {
    if (interpolation.snapshotCount < 2) {
        return false;
    }

    const auto& oldest = interpolation.snapshots[0];
    const auto& newest = interpolation.snapshots[interpolation.snapshotCount - 1];
    const float oldestTick = static_cast<float>(oldest.serverTick);
    const float newestTick = static_cast<float>(newest.serverTick);
    const float delayedNewestTick =
        std::max(oldestTick, newestTick - std::max(0.0f, interpolation.interpolationDelayTicks));

    if (!interpolation.hasRenderServerTick) {
        interpolation.renderServerTick = delayedNewestTick;
        interpolation.hasRenderServerTick = true;
    } else {
        const float previousRenderTick = interpolation.renderServerTick;
        const float advancedRenderTick = previousRenderTick + std::max(0.0f, interpolation.serverTickRate) * dt;
        interpolation.renderServerTick = delayedNewestTick >= previousRenderTick
                                             ? std::min(advancedRenderTick, delayedNewestTick)
                                             : previousRenderTick;
        interpolation.renderServerTick = std::max(interpolation.renderServerTick, oldestTick);
    }

    for (std::size_t i = 1; i < interpolation.snapshotCount; ++i) {
        const auto& a = interpolation.snapshots[i - 1];
        const auto& b = interpolation.snapshots[i];
        if (interpolation.renderServerTick > static_cast<float>(b.serverTick)) {
            continue;
        }

        const float tickSpan = static_cast<float>(b.serverTick - a.serverTick);
        const float alpha =
            tickSpan > 0.0f
                ? std::clamp((interpolation.renderServerTick - static_cast<float>(a.serverTick)) / tickSpan, 0.0f, 1.0f)
                : 1.0f;

        if (glm::length(b.position - a.position) > interpolation.snapDistance && alpha < 1.0f) {
            outPose.position = a.position;
            outPose.alpha = 0.0f;
        } else {
            outPose.position = a.position + (b.position - a.position) * alpha;
            outPose.alpha = alpha;
        }
        outPose.yawFrom = a.yaw;
        outPose.yawTo = b.yaw;
        outPose.pitchFrom = a.pitch;
        outPose.pitchTo = b.pitch;
        return true;
    }

    return false;
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
        SampledPose pose;
        if (sampleSnapshotBuffer(interpolation, ctx.dt, pose)) {
            applyPose(reg, entity, transform, pose);
        } else {
            applyFallbackTarget(reg, entity, transform, interpolation, ctx.dt);
        }
    }
}

} // namespace ecs
