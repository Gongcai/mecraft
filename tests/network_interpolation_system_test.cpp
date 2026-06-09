#include "ecs/GameplayRegistry.h"
#include "ecs/GameplayServices.h"
#include "ecs/SystemContext.h"
#include "ecs/components/CameraComponents.h"
#include "ecs/components/DropComponents.h"
#include "ecs/components/NetworkComponents.h"
#include "ecs/components/SteveComponents.h"
#include "ecs/components/TransformComponents.h"
#include "ecs/systems/network/NetworkInterpolationSystem.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        std::abort();
    }
}

static void testMovesTowardTargetWithoutSnapping() {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    auto& raw = registry.registry();

    const entt::entity entity = raw.create();
    raw.emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f), 0.0f);
    auto& interpolation = raw.emplace<ecs::NetworkInterpolationComponent>(entity);
    interpolation.targetPosition = glm::vec3(4.0f, 0.0f, 0.0f);
    interpolation.positionLerpSpeed = 10.0f;
    interpolation.snapDistance = 100.0f;
    interpolation.hasTarget = true;

    ecs::NetworkInterpolationSystem system;
    ecs::SystemContext ctx{registry, services, 0.1f, 1};
    system.update(ctx);

    const float x = raw.get<ecs::TransformComponent>(entity).position.x;
    require(x > 0.0f, "interpolation should move toward target");
    require(x < 4.0f, "nearby interpolation should not snap to target");
}

static void testSnapsWhenTargetIsTooFar() {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    auto& raw = registry.registry();

    const entt::entity entity = raw.create();
    raw.emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f), 0.0f);
    auto& interpolation = raw.emplace<ecs::NetworkInterpolationComponent>(entity);
    interpolation.targetPosition = glm::vec3(20.0f, 2.0f, -3.0f);
    interpolation.snapDistance = 8.0f;
    interpolation.hasTarget = true;

    ecs::NetworkInterpolationSystem system;
    ecs::SystemContext ctx{registry, services, 1.0f / 60.0f, 1};
    system.update(ctx);

    const glm::vec3 position = raw.get<ecs::TransformComponent>(entity).position;
    require(std::fabs(position.x - 20.0f) < 0.001f &&
            std::fabs(position.y - 2.0f) < 0.001f &&
            std::fabs(position.z + 3.0f) < 0.001f,
            "interpolation should snap large corrections");
}

static void testInterpolatesHumanoidAnglesOnShortestPath() {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    auto& raw = registry.registry();

    const entt::entity entity = raw.create();
    raw.emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f), 0.0f);
    auto& interpolation = raw.emplace<ecs::NetworkInterpolationComponent>(entity);
    interpolation.targetPosition = glm::vec3(0.0f);
    interpolation.targetYaw = 10.0f;
    interpolation.targetPitch = 30.0f;
    interpolation.rotationLerpSpeed = 10.0f;
    interpolation.hasTarget = true;
    raw.emplace<ecs::MobAIComponent>(entity).yaw = 350.0f;
    auto& camera = raw.emplace<ecs::CameraStateComponent>(entity);
    camera.yaw = 350.0f;
    camera.pitch = 0.0f;

    ecs::NetworkInterpolationSystem system;
    ecs::SystemContext ctx{registry, services, 0.1f, 1};
    system.update(ctx);

    const auto& mobAI = raw.get<ecs::MobAIComponent>(entity);
    const auto& updatedCamera = raw.get<ecs::CameraStateComponent>(entity);
    require(mobAI.yaw > 350.0f && mobAI.yaw < 370.0f,
            "mob yaw should use shortest path across zero degrees");
    require(updatedCamera.yaw > 350.0f && updatedCamera.yaw < 370.0f,
            "camera yaw should use shortest path across zero degrees");
    require(updatedCamera.pitch > 0.0f && updatedCamera.pitch < 30.0f,
            "camera pitch should interpolate toward target");
}

static void testInterpolatesSpinAnglesOnShortestPath() {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    auto& raw = registry.registry();

    const entt::entity entity = raw.create();
    raw.emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f), 0.0f);
    auto& interpolation = raw.emplace<ecs::NetworkInterpolationComponent>(entity);
    interpolation.targetPosition = glm::vec3(0.0f);
    interpolation.targetYaw = 0.1f;
    interpolation.rotationLerpSpeed = 10.0f;
    interpolation.hasTarget = true;
    raw.emplace<ecs::SpinVisualComponent>(entity, 6.2f, 0.0f);

    ecs::NetworkInterpolationSystem system;
    ecs::SystemContext ctx{registry, services, 0.1f, 1};
    system.update(ctx);

    const float yaw = raw.get<ecs::SpinVisualComponent>(entity).yawRadians;
    require(yaw > 6.2f && yaw < 6.4f,
            "spin yaw should use shortest path across zero radians");
}

int main() {
    testMovesTowardTargetWithoutSnapping();
    testSnapsWhenTargetIsTooFar();
    testInterpolatesHumanoidAnglesOnShortestPath();
    testInterpolatesSpinAnglesOnShortestPath();
    std::printf("All NetworkInterpolationSystem tests passed!\n");
    return 0;
}
