#include "TransformHierarchySystem.h"
#include "../../components/Components.h"
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace ecs {

void TransformHierarchySystem::update(GameplayRegistry& registry) {
    auto& reg = registry.registry();

    // 1. Update root entities (those with WorldTransformComponent but no ParentComponent).
    auto roots = reg.view<WorldTransformComponent>(entt::exclude<ParentComponent>);
    for (auto entity : roots) {
        auto& world = reg.get<WorldTransformComponent>(entity);

        if (reg.all_of<TransformComponent>(entity)) {
            auto& transform = reg.get<TransformComponent>(entity);
            world.worldMatrix = glm::translate(glm::mat4(1.0f), transform.position);
        } else if (reg.all_of<LocalTransformComponent>(entity)) {
            auto& local = reg.get<LocalTransformComponent>(entity);
            world.worldMatrix = local.toMatrix();
        } else {
            world.worldMatrix = glm::mat4(1.0f);
        }
    }

    // 2. BFS through the hierarchy to compute child world transforms.
    std::vector<entt::entity> queue;
    for (auto entity : roots) {
        if (reg.all_of<ChildrenComponent>(entity)) {
            auto& children = reg.get<ChildrenComponent>(entity);
            for (auto child : children.children) {
                queue.push_back(child);
            }
        }
    }

    size_t front = 0;
    while (front < queue.size()) {
        auto entity = queue[front++];
        if (!reg.all_of<LocalTransformComponent, WorldTransformComponent, ParentComponent>(entity)) {
            continue;
        }

        auto& local = reg.get<LocalTransformComponent>(entity);
        auto& world = reg.get<WorldTransformComponent>(entity);
        auto& parent = reg.get<ParentComponent>(entity);

        if (reg.all_of<WorldTransformComponent>(parent.parent)) {
            auto& parentWorld = reg.get<WorldTransformComponent>(parent.parent);
            world.worldMatrix = parentWorld.worldMatrix * local.toMatrix();
        } else {
            world.worldMatrix = local.toMatrix();
        }

        if (reg.all_of<ChildrenComponent>(entity)) {
            auto& children = reg.get<ChildrenComponent>(entity);
            for (auto child : children.children) {
                queue.push_back(child);
            }
        }
    }
}

} // namespace ecs
