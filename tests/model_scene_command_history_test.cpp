#include <cstdlib>
#include <iostream>
#include <vector>

#include "ecs/components/TransformComponents.h"
#include "scene/ModelSceneCommandHistory.h"
#include "scene/ModelSceneComponents.h"
#include "scene/ModelSceneRuntime.h"

namespace {

int fail(const char* message) {
    std::cerr << "[model_scene_command_history_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main() {
    ModelSceneRuntime runtime;
    scene::ModelSceneCommandHistory history;

    const entt::entity root = runtime.createEmptyEntity("Root");
    const entt::entity child = runtime.createEmptyEntity("Child");
    if (root == entt::null || child == entt::null || !runtime.setParent(child, root)) {
        return fail("failed to create command history fixture hierarchy");
    }
    const scene::SceneEntityId rootId = runtime.entityId(root);
    const scene::SceneEntityId childId = runtime.entityId(child);

    history.clear();
    if (!history.isAtSavedState() || history.canUndo() || history.canRedo()) {
        return fail("cleared history did not begin at its saved state");
    }

    scene::SceneEntityDocument transformBefore;
    scene::SceneEntityDocument transformAfter;
    if (!runtime.captureEntityState(child, transformBefore)) {
        return fail("failed to capture transform command start state");
    }
    runtime.registry().get<ecs::LocalTransformComponent>(child).localPosition = {3.0f, 4.0f, 5.0f};
    runtime.syncTransforms();
    if (!runtime.captureEntityState(child, transformAfter)) {
        return fail("failed to capture transform command end state");
    }
    history.recordEntityState(transformBefore, transformAfter);
    if (!history.canUndo() || history.isAtSavedState() || !history.undo(runtime)) {
        return fail("transform command could not be undone");
    }
    if (runtime.registry().get<ecs::LocalTransformComponent>(runtime.findEntity(childId)).localPosition !=
            transformBefore.transform.position ||
        !history.redo(runtime)) {
        return fail("transform command did not restore exact local state");
    }
    if (runtime.registry().get<ecs::LocalTransformComponent>(runtime.findEntity(childId)).localPosition !=
        transformAfter.transform.position) {
        return fail("transform command could not be redone");
    }

    scene::SceneEntityDocument nameBefore;
    scene::SceneEntityDocument nameAfter;
    if (!runtime.captureEntityState(child, nameBefore) || !runtime.renameEntity(child, "Renamed Child") ||
        !runtime.captureEntityState(child, nameAfter)) {
        return fail("failed to prepare rename command");
    }
    history.recordEntityState(nameBefore, nameAfter);
    if (!history.undo(runtime) ||
        runtime.registry().get<scene::NameComponent>(runtime.findEntity(childId)).value != nameBefore.name ||
        !history.redo(runtime) ||
        runtime.registry().get<scene::NameComponent>(runtime.findEntity(childId)).value != nameAfter.name) {
        return fail("rename command did not round trip");
    }

    scene::SceneEntityDocument parentBefore;
    scene::SceneEntityDocument parentAfter;
    if (!runtime.captureEntityState(child, parentBefore) || !runtime.setParent(child, entt::null) ||
        !runtime.captureEntityState(child, parentAfter)) {
        return fail("failed to prepare reparent command");
    }
    history.recordEntityState(parentBefore, parentAfter);
    if (!history.undo(runtime)) {
        return fail("reparent command could not be undone");
    }
    const entt::entity restoredChild = runtime.findEntity(childId);
    if (restoredChild == entt::null ||
        runtime.registry().get<ecs::ParentComponent>(restoredChild).parent != runtime.findEntity(rootId)) {
        return fail("reparent undo did not restore the stable parent");
    }

    const entt::entity created = runtime.createEmptyEntity("Created");
    std::vector<scene::SceneEntityDocument> createdStates;
    if (created == entt::null || !runtime.captureEntitySubtree(created, createdStates)) {
        return fail("failed to capture created entity command");
    }
    const scene::SceneEntityId createdId = runtime.entityId(created);
    history.recordCreatedSubtree(createdStates);
    if (!history.undo(runtime) || runtime.findEntity(createdId) != entt::null || !history.redo(runtime) ||
        runtime.findEntity(createdId) == entt::null) {
        return fail("created entity presence command did not round trip");
    }

    const entt::entity pointLight = runtime.createPointLight({2.0f, 3.0f, 4.0f});
    scene::SceneEntityDocument pointLightBefore;
    scene::SceneEntityDocument pointLightAfter;
    if (pointLight == entt::null || !runtime.captureEntityState(pointLight, pointLightBefore) ||
        !pointLightBefore.manualPointLight.has_value()) {
        return fail("failed to create a Point-light scene entity");
    }
    scene::SceneManualPointLightDocument editedLight = *pointLightBefore.manualPointLight;
    editedLight.intensityCandela = 1450.0f;
    editedLight.colorLinear = {1.0f, 0.4f, 0.15f};
    if (!runtime.updatePointLight(pointLight, editedLight) ||
        !runtime.captureEntityState(pointLight, pointLightAfter)) {
        return fail("failed to edit the Point-light entity payload");
    }
    history.recordEntityState(pointLightBefore, pointLightAfter);
    if (!history.undo(runtime)) {
        return fail("Point-light property command could not be undone");
    }
    const entt::entity restoredPointLight = runtime.findEntity(pointLightBefore.id);
    scene::SceneEntityDocument restoredPointLightState;
    if (restoredPointLight == entt::null ||
        !runtime.captureEntityState(restoredPointLight, restoredPointLightState) ||
        !restoredPointLightState.manualPointLight.has_value() ||
        restoredPointLightState.manualPointLight->intensityCandela !=
            pointLightBefore.manualPointLight->intensityCandela ||
        !history.redo(runtime)) {
        return fail("Point-light property command did not restore exact state");
    }

    const entt::entity duplicatedPointLight = runtime.duplicateEntity(restoredPointLight);
    scene::SceneEntityDocument duplicatedPointLightState;
    if (duplicatedPointLight == entt::null ||
        !runtime.captureEntityState(duplicatedPointLight, duplicatedPointLightState) ||
        !duplicatedPointLightState.manualPointLight.has_value() ||
        duplicatedPointLightState.manualPointLight->intensityCandela != editedLight.intensityCandela) {
        return fail("Point-light entity duplication lost its light payload");
    }
    const auto restoredPointLightStableId =
        runtime.registry().get<scene::ManualPointLightComponent>(restoredPointLight).stableId;
    const auto duplicatedPointLightStableId =
        runtime.registry().get<scene::ManualPointLightComponent>(duplicatedPointLight).stableId;
    if (!restoredPointLightStableId.isValid() || !duplicatedPointLightStableId.isValid() ||
        restoredPointLightStableId == duplicatedPointLightStableId) {
        return fail("Point-light duplication reused a stable GPU-light identity");
    }

    std::vector<scene::SceneEntityDocument> deletedPointLightStates;
    const scene::SceneEntityId duplicatedPointLightEntityId = runtime.entityId(duplicatedPointLight);
    if (!runtime.captureEntitySubtree(duplicatedPointLight, deletedPointLightStates)) {
        return fail("failed to capture a deleted Point-light command");
    }
    runtime.destroyEntity(duplicatedPointLight);
    history.recordDeletedSubtree(deletedPointLightStates);
    if (!history.undo(runtime)) {
        return fail("deleted Point-light command could not be undone");
    }
    const entt::entity recoveredPointLight = runtime.findEntity(duplicatedPointLightEntityId);
    if (recoveredPointLight == entt::null ||
        !runtime.registry().all_of<scene::ManualPointLightComponent>(recoveredPointLight)) {
        return fail("Point-light delete undo did not restore its component");
    }
    const auto recoveredPointLightStableId =
        runtime.registry().get<scene::ManualPointLightComponent>(recoveredPointLight).stableId;
    if (!recoveredPointLightStableId.isValid() || recoveredPointLightStableId == duplicatedPointLightStableId ||
        !history.redo(runtime) || runtime.findEntity(duplicatedPointLightEntityId) != entt::null ||
        !history.undo(runtime)) {
        return fail("Point-light delete command did not round trip with a fresh stable GPU identity");
    }

    const entt::entity currentRoot = runtime.findEntity(rootId);
    std::vector<scene::SceneEntityDocument> deletedStates;
    if (!runtime.captureEntitySubtree(currentRoot, deletedStates) || deletedStates.size() != 2u) {
        return fail("failed to capture deleted hierarchy command");
    }
    runtime.destroyEntity(currentRoot);
    history.recordDeletedSubtree(deletedStates);
    if (runtime.findEntity(rootId) != entt::null || runtime.findEntity(childId) != entt::null ||
        !history.undo(runtime)) {
        return fail("deleted hierarchy could not be restored");
    }
    const entt::entity recoveredRoot = runtime.findEntity(rootId);
    const entt::entity recoveredChild = runtime.findEntity(childId);
    if (recoveredRoot == entt::null || recoveredChild == entt::null ||
        runtime.registry().get<ecs::ParentComponent>(recoveredChild).parent != recoveredRoot ||
        !history.redo(runtime) || runtime.findEntity(rootId) != entt::null ||
        runtime.findEntity(childId) != entt::null) {
        return fail("deleted hierarchy redo did not remove the recovered subtree");
    }

    history.markSaved();
    if (!history.isAtSavedState() || !history.undo(runtime) || history.isAtSavedState() || !history.redo(runtime) ||
        !history.isAtSavedState()) {
        return fail("history savepoint did not follow undo and redo cursor state");
    }

    std::cout << "[model_scene_command_history_test] PASS\n";
    return EXIT_SUCCESS;
}
