#include "PlayerRuntimeUpdateSystem.h"

#include <algorithm>
#include <cmath>

#include "../components/Components.h"
#include "../../player/Inventory.h"

namespace ecs {
namespace {

float lerp(const float a, const float b, const float t) {
    return a + (b - a) * t;
}

void updateCameraVectors(CameraStateComponent& camera) {
    const glm::vec3 front = {
        cos(glm::radians(camera.yaw)) * cos(glm::radians(camera.pitch)),
        sin(glm::radians(camera.pitch)),
        sin(glm::radians(camera.yaw)) * cos(glm::radians(camera.pitch))
    };

    camera.front = glm::normalize(front);
    camera.right = glm::normalize(glm::cross(camera.front, glm::vec3(0.0f, 1.0f, 0.0f)));
    camera.up = glm::normalize(glm::cross(camera.right, camera.front));
}

void applyHotbarIntent(const HotbarIntentComponent& hotbar, InventoryComponent& inventory) {
    for (int i = 0; i < Inventory::HOTBAR_SIZE; ++i) {
        if (hotbar.slotSelected[i]) {
            inventory.selectedHotbarSlot = i;
        }
    }
    if (hotbar.scrollUp) {
        inventory.selectedHotbarSlot = (inventory.selectedHotbarSlot + Inventory::HOTBAR_SIZE - 1) % Inventory::HOTBAR_SIZE;
    }
    if (hotbar.scrollDown) {
        inventory.selectedHotbarSlot = (inventory.selectedHotbarSlot + 1) % Inventory::HOTBAR_SIZE;
    }
}

} // namespace

void PlayerRuntimeUpdateSystem::update(GameplayRegistry& registry, const float dt) {
    auto view = registry.view<LocalPlayerTag,
                              LookIntentComponent,
                              HotbarIntentComponent,
                              CameraStateComponent,
                              InventoryComponent>();
    for (auto e : view) {
        const auto& lookIntent = view.get<LookIntentComponent>(e);
        const auto& hotbarIntent = view.get<HotbarIntentComponent>(e);
        auto& camera = view.get<CameraStateComponent>(e);
        auto& inventory = view.get<InventoryComponent>(e);

        applyHotbarIntent(hotbarIntent, inventory);

        camera.yaw += lookIntent.deltaX * camera.sensitivity;
        camera.pitch = std::clamp(camera.pitch + lookIntent.deltaY * camera.sensitivity, -89.0f, 89.0f);
        updateCameraVectors(camera);

        const auto* sprintFov = registry.try_get<SprintFovComponent>(e);
        const auto* moveIntent = registry.try_get<MoveIntentComponent>(e);
        const auto* physicsBody = registry.try_get<PhysicsBodyComponent>(e);
        if (sprintFov == nullptr || moveIntent == nullptr || physicsBody == nullptr) {
            continue;
        }

        const glm::vec2 horizontalVelocity(physicsBody->body.velocity.x, physicsBody->body.velocity.z);
        const bool isSprinting = moveIntent->wantsSprint && glm::length(horizontalVelocity) > 0.001f;
        const float targetFov = isSprinting ? sprintFov->sprintFov : sprintFov->walkFov;
        camera.fov = lerp(camera.fov, targetFov, dt * sprintFov->lerpSpeed);
    }
}

} // namespace ecs
