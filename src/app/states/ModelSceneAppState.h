#ifndef MECRAFT_MODEL_SCENE_APP_STATE_H
#define MECRAFT_MODEL_SCENE_APP_STATE_H

#include "AppStateDependencies.h"
#include "IAppState.h"

#include <glm/glm.hpp>

#include <array>

#include "scene/ModelSceneRuntime.h"
#include "scene/ModelSceneCommandHistory.h"
#include "ui/imgui/ImGuiRhiRenderer.h"

struct InputSnapshot;

class ModelSceneAppState : public IAppState {
public:
    explicit ModelSceneAppState(AppStateDependencies deps);

    void onEnter() override;
    void onExit() override;
    void update(double frameTime, double& accumulator) override;
    void render(double frameTime) override;

private:
    enum class PendingSceneAction {
        None,
        NewScene,
        OpenScene,
        ReturnToMenu,
    };

    enum class PendingEntityAction {
        None,
        Duplicate,
        Delete,
        Focus,
    };

    void buildEditorUi();
    void buildInitialDockLayout(ImGuiID dockspaceId);
    void showHierarchyPanel();
    void showHierarchyEntity(entt::entity entity);
    void showInspectorPanel();
    void showRenderSettingsPanel();
    void showAssetsPanel();
    void browseAndImportModel();
    void importModelPath(const std::string& path);
    void showUnsavedChangesModal();
    void requestSceneAction(PendingSceneAction action);
    void executeSceneAction(PendingSceneAction action);
    void newScene();
    void openSceneDialog();
    [[nodiscard]] bool loadScenePath(const std::string& path);
    [[nodiscard]] bool saveScene();
    [[nodiscard]] bool saveSceneAs();
    [[nodiscard]] bool saveSceneToPath(const std::string& path);
    [[nodiscard]] scene::SceneEditorCameraDocument captureEditorCamera() const;
    void applyEditorCamera(const scene::SceneEditorCameraDocument& camera);
    void markSceneDirty();
    void refreshSceneDirty();
    void recordCreatedEntity(entt::entity entity);
    void beginTransformCommand(
        entt::entity entity,
        const scene::SceneEntityDocument& before,
        bool fromGizmo);
    void finishTransformCommand();
    void undoSceneCommand();
    void redoSceneCommand();
    void handleEditorShortcuts(const InputSnapshot& input);
    void queueEntityAction(PendingEntityAction action,
                           scene::SceneEntityId entityId);
    void executePendingEntityAction();
    void duplicateSelectedEntity();
    void deleteSelectedEntity();
    void focusSelectedEntity();
    void showViewportPanel();
    [[nodiscard]] bool showGizmoToolbar();
    void updateCamera(const InputSnapshot& input, double frameTime);
    void setCameraControlActive(bool active);
    void selectFromViewport(const ImVec2& mousePosition);
    void requestReturnToMenu();
    [[nodiscard]] glm::vec3 cameraPosition() const;

    AppStateDependencies m_deps;
    ImGuiRhiRenderer m_imguiRenderer;
    ModelSceneRuntime m_scene;
    scene::ModelSceneCommandHistory m_history;
    glm::mat4 m_view{1.0f};
    glm::mat4 m_projection{1.0f};
    ImVec2 m_viewportPosition{0.0f, 0.0f};
    ImVec2 m_viewportSize{1.0f, 1.0f};
    float m_cameraYaw = 35.0f;
    float m_cameraPitch = 18.0f;
    float m_cameraDistance = 5.0f;
    glm::vec3 m_cameraTarget{0.0f};
    int m_gizmoOperation = 0;
    int m_gizmoMode = 0;
    bool m_viewportHovered = false;
    bool m_hierarchyDropPending = false;
    bool m_cameraControlActive = false;
    bool m_initialized = false;
    bool m_returnRequested = false;
    bool m_sceneDirty = false;
    bool m_nonHistoryDirty = false;
    bool m_openUnsavedPopup = false;
    bool m_transformCommandActive = false;
    bool m_transformCommandFromGizmo = false;
    PendingSceneAction m_pendingSceneAction = PendingSceneAction::None;
    PendingEntityAction m_pendingEntityAction = PendingEntityAction::None;
    scene::SceneEntityId m_pendingEntityId = scene::kInvalidSceneEntityId;
    scene::SceneEntityId m_entityNameEditorId = scene::kInvalidSceneEntityId;
    scene::SceneEntityId m_transformCommandEntityId =
        scene::kInvalidSceneEntityId;
    scene::SceneEntityDocument m_transformCommandBefore;
    scene::SceneEntityId m_hierarchyDropChild =
        scene::kInvalidSceneEntityId;
    scene::SceneEntityId m_hierarchyDropParent =
        scene::kInvalidSceneEntityId;
    std::array<char, 4096> m_importPath{
        "assets/models/showcase/DamagedHelmet.glb"};
    std::array<char, 512> m_entityNameBuffer{};
    std::string m_importDialogError;
    std::string m_scenePath;
    std::string m_sceneIoError;
};

#endif // MECRAFT_MODEL_SCENE_APP_STATE_H
