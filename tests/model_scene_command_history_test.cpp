#include <cstdlib>
#include <iostream>
#include <vector>

#include "ecs/components/TransformComponents.h"
#include "scene/ModelSceneCommandHistory.h"
#include "scene/ModelSceneComponents.h"
#include "scene/ModelSceneRuntime.h"

namespace {

int fail(const char* message) {
    std::cerr << "[model_scene_command_history_test] FAIL: "
              << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main() {
    ModelSceneRuntime runtime;
    scene::ModelSceneCommandHistory history;

    const entt::entity root = runtime.createEmptyEntity("Root");
    const entt::entity child = runtime.createEmptyEntity("Child");
    if (root == entt::null || child == entt::null ||
        !runtime.setParent(child, root)) {
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
    runtime.registry()
        .get<ecs::LocalTransformComponent>(child)
        .localPosition = {3.0f, 4.0f, 5.0f};
    runtime.syncTransforms();
    if (!runtime.captureEntityState(child, transformAfter)) {
        return fail("failed to capture transform command end state");
    }
    history.recordEntityState(transformBefore, transformAfter);
    if (!history.canUndo() || history.isAtSavedState() ||
        !history.undo(runtime)) {
        return fail("transform command could not be undone");
    }
    if (runtime.registry()
            .get<ecs::LocalTransformComponent>(runtime.findEntity(childId))
            .localPosition != transformBefore.transform.position ||
        !history.redo(runtime)) {
        return fail("transform command did not restore exact local state");
    }
    if (runtime.registry()
            .get<ecs::LocalTransformComponent>(runtime.findEntity(childId))
            .localPosition != transformAfter.transform.position) {
        return fail("transform command could not be redone");
    }

    scene::SceneEntityDocument nameBefore;
    scene::SceneEntityDocument nameAfter;
    if (!runtime.captureEntityState(child, nameBefore) ||
        !runtime.renameEntity(child, "Renamed Child") ||
        !runtime.captureEntityState(child, nameAfter)) {
        return fail("failed to prepare rename command");
    }
    history.recordEntityState(nameBefore, nameAfter);
    if (!history.undo(runtime) ||
        runtime.registry()
            .get<scene::NameComponent>(runtime.findEntity(childId))
            .value != nameBefore.name ||
        !history.redo(runtime) ||
        runtime.registry()
            .get<scene::NameComponent>(runtime.findEntity(childId))
            .value != nameAfter.name) {
        return fail("rename command did not round trip");
    }

    scene::SceneEntityDocument parentBefore;
    scene::SceneEntityDocument parentAfter;
    if (!runtime.captureEntityState(child, parentBefore) ||
        !runtime.setParent(child, entt::null) ||
        !runtime.captureEntityState(child, parentAfter)) {
        return fail("failed to prepare reparent command");
    }
    history.recordEntityState(parentBefore, parentAfter);
    if (!history.undo(runtime)) {
        return fail("reparent command could not be undone");
    }
    const entt::entity restoredChild = runtime.findEntity(childId);
    if (restoredChild == entt::null ||
        runtime.registry().get<ecs::ParentComponent>(restoredChild).parent !=
            runtime.findEntity(rootId)) {
        return fail("reparent undo did not restore the stable parent");
    }

    const entt::entity created = runtime.createEmptyEntity("Created");
    std::vector<scene::SceneEntityDocument> createdStates;
    if (created == entt::null ||
        !runtime.captureEntitySubtree(created, createdStates)) {
        return fail("failed to capture created entity command");
    }
    const scene::SceneEntityId createdId = runtime.entityId(created);
    history.recordCreatedSubtree(createdStates);
    if (!history.undo(runtime) || runtime.findEntity(createdId) != entt::null ||
        !history.redo(runtime) || runtime.findEntity(createdId) == entt::null) {
        return fail("created entity presence command did not round trip");
    }

    const entt::entity currentRoot = runtime.findEntity(rootId);
    std::vector<scene::SceneEntityDocument> deletedStates;
    if (!runtime.captureEntitySubtree(currentRoot, deletedStates) ||
        deletedStates.size() != 2u) {
        return fail("failed to capture deleted hierarchy command");
    }
    runtime.destroyEntity(currentRoot);
    history.recordDeletedSubtree(deletedStates);
    if (runtime.findEntity(rootId) != entt::null ||
        runtime.findEntity(childId) != entt::null ||
        !history.undo(runtime)) {
        return fail("deleted hierarchy could not be restored");
    }
    const entt::entity recoveredRoot = runtime.findEntity(rootId);
    const entt::entity recoveredChild = runtime.findEntity(childId);
    if (recoveredRoot == entt::null || recoveredChild == entt::null ||
        runtime.registry().get<ecs::ParentComponent>(recoveredChild).parent !=
            recoveredRoot ||
        !history.redo(runtime) || runtime.findEntity(rootId) != entt::null ||
        runtime.findEntity(childId) != entt::null) {
        return fail("deleted hierarchy redo did not remove the recovered subtree");
    }

    history.markSaved();
    if (!history.isAtSavedState() || !history.undo(runtime) ||
        history.isAtSavedState() || !history.redo(runtime) ||
        !history.isAtSavedState()) {
        return fail("history savepoint did not follow undo and redo cursor state");
    }

    std::cout << "[model_scene_command_history_test] PASS\n";
    return EXIT_SUCCESS;
}
