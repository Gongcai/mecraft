#ifndef MECRAFT_MODEL_SCENE_APP_STATE_H
#define MECRAFT_MODEL_SCENE_APP_STATE_H

#include "AppStateDependencies.h"
#include "IAppState.h"

#include <glm/glm.hpp>

#include <array>

#include "scene/ModelSceneRuntime.h"
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
    void buildEditorUi();
    void buildInitialDockLayout(ImGuiID dockspaceId);
    void showHierarchyPanel();
    void showInspectorPanel();
    void showRenderSettingsPanel();
    void showAssetsPanel();
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
    bool m_cameraControlActive = false;
    bool m_initialized = false;
    bool m_returnRequested = false;
    std::array<char, 512> m_importPath{
        "assets/models/showcase/DamagedHelmet.glb"};
};

#endif // MECRAFT_MODEL_SCENE_APP_STATE_H
