#include <cmath>
#include <cstdlib>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

#include "ecs/components/TransformComponents.h"
#include "scene/ModelSceneComponents.h"
#include "scene/ModelSceneRuntime.h"

namespace {

int fail(const char* message) {
    std::cerr << "[model_scene_hierarchy_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool near(const float lhs, const float rhs) {
    return std::abs(lhs - rhs) <= 1e-3f;
}

bool nearPosition(const glm::mat4& matrix, const glm::vec3& expected) {
    const glm::vec3 actual(matrix[3]);
    return near(actual.x, expected.x) && near(actual.y, expected.y) && near(actual.z, expected.z);
}

} // namespace

int main() {
    ModelSceneRuntime scene;
    const entt::entity root = scene.createEmptyEntity("Group");
    const entt::entity child = scene.createEmptyEntity("Group");
    if (root == entt::null || child == entt::null) {
        return fail("empty scene entities were not created");
    }
    if (scene.entityId(root) == scene::kInvalidSceneEntityId || scene.entityId(child) == scene::kInvalidSceneEntityId ||
        scene.entityId(root) == scene.entityId(child)) {
        return fail("scene entity IDs are not stable and unique");
    }
    if (scene.findEntity(scene.entityId(child)) != child) {
        return fail("stable scene entity lookup failed");
    }
    const auto& childName = scene.registry().get<scene::NameComponent>(child).value;
    if (childName != "Group (2)") {
        return fail("empty scene entity names are not unique");
    }

    auto& rootTransform = scene.registry().get<ecs::LocalTransformComponent>(root);
    auto& childTransform = scene.registry().get<ecs::LocalTransformComponent>(child);
    rootTransform.localPosition = {3.0f, 0.0f, 0.0f};
    childTransform.localPosition = {0.0f, 2.0f, 0.0f};
    scene.syncTransforms();
    const glm::mat4 childWorldBeforeParenting = scene.registry().get<ecs::WorldTransformComponent>(child).worldMatrix;

    if (!scene.setParent(child, root)) {
        return fail("valid scene reparenting was rejected");
    }
    const glm::mat4 childWorldAfterParenting = scene.registry().get<ecs::WorldTransformComponent>(child).worldMatrix;
    if (!nearPosition(childWorldBeforeParenting, {0.0f, 2.0f, 0.0f}) ||
        !nearPosition(childWorldAfterParenting, {0.0f, 2.0f, 0.0f})) {
        return fail("reparenting did not preserve the child world transform");
    }
    if (scene.registry().get<ecs::ParentComponent>(child).parent != root ||
        scene.registry().get<ecs::ChildrenComponent>(root).children.size() != 1u) {
        return fail("parent and child components are inconsistent");
    }

    rootTransform.localPosition = {4.0f, 0.0f, 0.0f};
    scene.syncTransforms();
    const glm::mat4 movedChildWorld = scene.registry().get<ecs::WorldTransformComponent>(child).worldMatrix;
    if (!nearPosition(movedChildWorld, {1.0f, 2.0f, 0.0f})) {
        return fail("parent movement did not propagate to the child");
    }
    if (scene.setParent(root, child)) {
        return fail("hierarchy cycle was accepted");
    }

    const glm::mat4 requestedWorld = glm::translate(glm::mat4(1.0f), glm::vec3(7.0f, 4.0f, -2.0f));
    if (!scene.setWorldTransform(child, requestedWorld)) {
        return fail("child world transform editing failed");
    }
    if (!nearPosition(scene.registry().get<ecs::WorldTransformComponent>(child).worldMatrix, {7.0f, 4.0f, -2.0f})) {
        return fail("child world transform was not converted into parent space");
    }
    if (!scene.setParent(child, entt::null)) {
        return fail("moving a child back to the scene root failed");
    }
    if (scene.registry().all_of<ecs::ParentComponent>(child) ||
        !nearPosition(scene.registry().get<ecs::WorldTransformComponent>(child).worldMatrix, {7.0f, 4.0f, -2.0f})) {
        return fail("unparenting did not preserve the child world transform");
    }

    if (!scene.setParent(child, root)) {
        return fail("second valid scene reparenting was rejected");
    }

    scene.registry().emplace<scene::StaticMeshComponent>(child, scene::StaticMeshComponent{42u});
    scene.registry().emplace<scene::PickableComponent>(child,
                                                       scene::PickableComponent{glm::vec3(-1.0f), glm::vec3(1.0f)});
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    if (!scene.entityWorldBounds(root, boundsMin, boundsMax) || !near(boundsMin.x, 4.0f) || !near(boundsMin.y, 0.0f) ||
        !near(boundsMin.z, -3.0f) || !near(boundsMax.x, 8.0f) || !near(boundsMax.y, 5.0f) || !near(boundsMax.z, 0.0f)) {
        return fail("hierarchy world bounds did not include pivots and mesh bounds");
    }

    const entt::entity duplicatedRoot = scene.duplicateEntity(root);
    if (duplicatedRoot == entt::null || duplicatedRoot == root ||
        scene.entityId(duplicatedRoot) == scene.entityId(root) || scene.selectedEntity() != duplicatedRoot) {
        return fail("duplicating a hierarchy did not create and select a stable root");
    }
    const auto& duplicatedChildren = scene.registry().get<ecs::ChildrenComponent>(duplicatedRoot).children;
    if (duplicatedChildren.size() != 1u) {
        return fail("duplicating a hierarchy did not preserve its descendants");
    }
    const entt::entity duplicatedChild = duplicatedChildren.front();
    if (!scene.registry().all_of<scene::StaticMeshComponent, scene::PickableComponent, ecs::ParentComponent>(
            duplicatedChild) ||
        scene.registry().get<ecs::ParentComponent>(duplicatedChild).parent != duplicatedRoot ||
        scene.registry().get<scene::StaticMeshComponent>(duplicatedChild).assetId != 42u ||
        !nearPosition(scene.registry().get<ecs::WorldTransformComponent>(duplicatedChild).worldMatrix,
                      {7.0f, 4.0f, -2.0f})) {
        return fail("duplicating a hierarchy changed components or world transforms");
    }
    if (!scene.renameEntity(duplicatedRoot, "Renamed Group") ||
        scene.registry().get<scene::NameComponent>(duplicatedRoot).value != "Renamed Group") {
        return fail("valid scene entity rename failed");
    }
    if (scene.renameEntity(duplicatedRoot, "")) {
        return fail("empty scene entity name was accepted");
    }
    const std::string collidingName = scene.registry().get<scene::NameComponent>(child).value;
    if (!scene.renameEntity(duplicatedRoot, collidingName) ||
        scene.registry().get<scene::NameComponent>(duplicatedRoot).value == collidingName) {
        return fail("entity rename did not preserve global name uniqueness");
    }

    scene.destroyEntity(root);
    if (scene.registry().valid(root) || scene.registry().valid(child)) {
        return fail("destroying a parent did not destroy its subtree");
    }
    if (!scene.registry().valid(duplicatedRoot) || !scene.registry().valid(duplicatedChild)) {
        return fail("destroying a hierarchy also removed its duplicate");
    }

    std::cout << "[model_scene_hierarchy_test] PASS\n";
    return EXIT_SUCCESS;
}
