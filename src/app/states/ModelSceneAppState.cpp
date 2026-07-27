#include "ModelSceneAppState.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nfd.h>

#include "ImGuizmo.h"
#include "imgui_internal.h"

#include "MainMenuAppState.h"
#include "Diagnostics.h"
#include "ecs/components/TransformComponents.h"
#include "engine/input/InputManager.h"
#include "engine/platform/Time.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiCommandListPool.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/core/RenderSettings.h"
#include "scene/ModelSceneComponents.h"
#include "ui/imgui/RenderSettingsImGui.h"

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kCameraLookSensitivity = 0.25f;
constexpr float kCameraMoveSpeed = 3.0f;
constexpr float kCameraFastMoveMultiplier = 4.0f;

[[nodiscard]] uint32_t viewportDimension(const float logicalSize,
                                         const float framebufferScale) {
    return static_cast<uint32_t>(std::max(
        1.0f, std::floor(logicalSize * framebufferScale)));
}

[[nodiscard]] ImGuizmo::OPERATION gizmoOperation(const int operation) {
    switch (operation) {
        case 0: return ImGuizmo::TRANSLATE;
        case 1: return ImGuizmo::ROTATE;
        case 2: return ImGuizmo::SCALE;
        default: std::abort();
    }
}
} // namespace

ModelSceneAppState::ModelSceneAppState(AppStateDependencies deps)
    : m_deps(deps) {}

void ModelSceneAppState::onEnter() {
    m_deps.contextManager.pushContext(InputContextType::UI);
    m_cameraControlActive = false;
    m_deps.input.captureMouse(false);
    m_returnRequested = false;
    if (!m_imguiRenderer.init(m_deps.window, m_deps.rhiDevice, true,
                              "model_scene_imgui.ini")) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[ModelSceneAppState] Failed to initialize ImGui\n");
        return;
    }
    if (!m_scene.init(m_deps.resourceMgr, m_deps.rhiDevice,
                      m_deps.commandListPool, m_imguiRenderer)) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[ModelSceneAppState] " << m_scene.lastError() << '\n');
        return;
    }
    if (m_scene.importModel(
            "assets/models/showcase/DamagedHelmet.glb") == entt::null) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[ModelSceneAppState] " << m_scene.lastError() << '\n');
        m_scene.shutdown();
        return;
    }
    m_initialized = true;
}

void ModelSceneAppState::onExit() {
    m_cameraControlActive = false;
    m_deps.input.captureMouse(false);
    m_scene.shutdown();
    m_imguiRenderer.shutdown();
    m_deps.contextManager.popContext();
    m_initialized = false;
}

void ModelSceneAppState::requestReturnToMenu() {
    if (!m_returnRequested) {
        m_returnRequested = true;
        m_deps.appFsm.changeState(std::make_unique<MainMenuAppState>(m_deps));
    }
}

void ModelSceneAppState::update(const double frameTime, double& accumulator) {
    accumulator = 0.0;
    if (!m_initialized) {
        requestReturnToMenu();
        return;
    }
    m_deps.input.update();
    const InputSnapshot& input = m_deps.input.snapshot();
    if (input.isKeyJustPressed(GLFW_KEY_ESCAPE)) {
        if (m_cameraControlActive) {
            setCameraControlActive(false);
        } else {
            requestReturnToMenu();
        }
        return;
    }
    updateCamera(input, frameTime);
    if (m_viewportHovered && !m_cameraControlActive &&
        !ImGuizmo::IsUsing() &&
        !ImGui::GetIO().WantTextInput) {
        if (input.isKeyJustPressed(GLFW_KEY_W)) m_gizmoOperation = 0;
        if (input.isKeyJustPressed(GLFW_KEY_E)) m_gizmoOperation = 1;
        if (input.isKeyJustPressed(GLFW_KEY_R)) {
            m_gizmoOperation = 2;
            m_gizmoMode = 0;
        }
    }
    m_scene.syncTransforms();
}

glm::vec3 ModelSceneAppState::cameraPosition() const {
    const float yaw = glm::radians(m_cameraYaw);
    const float pitch = glm::radians(m_cameraPitch);
    const glm::vec3 direction{
        std::cos(pitch) * std::cos(yaw),
        std::sin(pitch),
        std::cos(pitch) * std::sin(yaw)};
    return m_cameraTarget + direction * m_cameraDistance;
}

void ModelSceneAppState::setCameraControlActive(const bool active) {
    if (m_cameraControlActive == active) {
        return;
    }
    m_cameraControlActive = active;
    m_deps.input.captureMouse(active);
}

void ModelSceneAppState::updateCamera(const InputSnapshot& input,
                                      const double frameTime) {
    const bool canStartControl =
        m_viewportHovered && !ImGuizmo::IsUsing() &&
        !ImGui::GetIO().WantTextInput;
    const bool startingControl =
        !m_cameraControlActive && canStartControl &&
        input.isMouseButtonJustPressed(GLFW_MOUSE_BUTTON_RIGHT);
    if (startingControl) {
        setCameraControlActive(true);
    }
    if (m_cameraControlActive &&
        input.isMouseButtonJustReleased(GLFW_MOUSE_BUTTON_RIGHT)) {
        setCameraControlActive(false);
        return;
    }

    if (m_cameraControlActive) {
        if (!startingControl) {
            m_cameraYaw = std::remainder(
                m_cameraYaw + input.mouseDelta.x * kCameraLookSensitivity,
                360.0f);
            m_cameraPitch = std::clamp(
                m_cameraPitch + input.mouseDelta.y * kCameraLookSensitivity,
                -85.0f, 85.0f);
        }

        const glm::vec3 position = cameraPosition();
        const glm::vec3 forward = glm::normalize(m_cameraTarget - position);
        const glm::vec3 right = glm::normalize(glm::cross(
            forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 movement{0.0f};
        if (input.isKeyHeld(GLFW_KEY_W)) movement += forward;
        if (input.isKeyHeld(GLFW_KEY_S)) movement -= forward;
        if (input.isKeyHeld(GLFW_KEY_D)) movement += right;
        if (input.isKeyHeld(GLFW_KEY_A)) movement -= right;
        if (glm::dot(movement, movement) > 0.0f) {
            const bool fastMove =
                input.isKeyHeld(GLFW_KEY_LEFT_SHIFT) ||
                input.isKeyHeld(GLFW_KEY_RIGHT_SHIFT);
            const float speed = kCameraMoveSpeed *
                (fastMove ? kCameraFastMoveMultiplier : 1.0f);
            m_cameraTarget += glm::normalize(movement) * speed *
                static_cast<float>(frameTime);
        }
    }

    if ((m_viewportHovered || m_cameraControlActive) &&
        input.scrollDelta != 0.0) {
        const float zoomFactor = std::exp(
            static_cast<float>(-input.scrollDelta) * 0.12f);
        m_cameraDistance = std::clamp(
            m_cameraDistance * zoomFactor, 0.6f, 80.0f);
    }
}

void ModelSceneAppState::buildInitialDockLayout(const ImGuiID dockspaceId) {
    ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspaceId);
    if (root != nullptr && !root->IsEmpty()) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(
        dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);
    ImGuiID center = dockspaceId;
    ImGuiID left = 0u;
    ImGuiID right = 0u;
    ImGuiID bottom = 0u;
    ImGui::DockBuilderSplitNode(
        center, ImGuiDir_Left, 0.20f, &left, &center);
    ImGui::DockBuilderSplitNode(
        center, ImGuiDir_Right, 0.25f, &right, &center);
    ImGui::DockBuilderSplitNode(
        center, ImGuiDir_Down, 0.24f, &bottom, &center);
    ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Render Settings", right);
    ImGui::DockBuilderDockWindow("Assets", bottom);
    ImGui::DockBuilderDockWindow("Scene Viewport", center);
    ImGui::DockBuilderFinish(dockspaceId);
}

void ModelSceneAppState::buildEditorUi() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Return to Main Menu")) {
                requestReturnToMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    const ImGuiID dockspaceId = ImGui::GetID("ModelSceneDockspaceV2");
    ImGui::DockSpaceOverViewport(
        dockspaceId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_None);
    buildInitialDockLayout(dockspaceId);
    showHierarchyPanel();
    showInspectorPanel();
    showRenderSettingsPanel();
    showAssetsPanel();
    showViewportPanel();
}

void ModelSceneAppState::showRenderSettingsPanel() {
    ImGui::Begin("Render Settings");
    RenderSettings settings = m_scene.renderSettings();
    bool changed = false;

    if (ImGui::CollapsingHeader(
            "Debug View", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("DebugViewSettings");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= render_settings_imgui::showDeferredDebugView(settings);
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Shadows")) {
        ImGui::PushID("ShadowSettings");
        changed |= render_settings_imgui::showShadowSettings(settings);
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Volumetric Light / Fog")) {
        ImGui::PushID("VolumetricSettings");
        changed |= render_settings_imgui::showVolumetricSettings(settings);
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("SSAO")) {
        ImGui::PushID("SsaoSettings");
        changed |= render_settings_imgui::showSsaoSettings(settings);
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("SSGI")) {
        ImGui::PushID("SsgiSettings");
        changed |= render_settings_imgui::showSsgiSettings(settings);
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader(
            "Post Process / Picture", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("PostProcessSettings");
        changed |= render_settings_imgui::showPostProcessSettings(settings);
        ImGui::PopID();
    }

    if (changed) {
        m_scene.setRenderSettings(settings);
    }
    ImGui::End();
}

void ModelSceneAppState::showHierarchyPanel() {
    ImGui::Begin("Scene Hierarchy");
    if (ImGui::Button("Create Empty")) {
        static_cast<void>(m_scene.createEmptyEntity("Empty Entity"));
    }
    ImGui::Separator();

    m_hierarchyDropPending = false;
    const ImGuiTreeNodeFlags rootFlags =
        ImGuiTreeNodeFlags_DefaultOpen |
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;
    const bool rootOpen = ImGui::TreeNodeEx("Scene##SceneRoot", rootFlags);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                "MODEL_SCENE_ENTITY_ID")) {
            if (payload->DataSize == sizeof(scene::SceneEntityId)) {
                m_hierarchyDropChild =
                    *static_cast<const scene::SceneEntityId*>(payload->Data);
                m_hierarchyDropParent = scene::kInvalidSceneEntityId;
                m_hierarchyDropPending = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (rootOpen) {
        std::vector<entt::entity> roots;
        const auto rootView = m_scene.registry().view<
            scene::SceneEntityIdComponent,
            scene::NameComponent,
            ecs::ChildrenComponent>(entt::exclude<ecs::ParentComponent>);
        roots.assign(rootView.begin(), rootView.end());
        std::sort(
            roots.begin(), roots.end(), [this](const entt::entity lhs,
                                               const entt::entity rhs) {
                return m_scene.entityId(lhs) < m_scene.entityId(rhs);
            });
        for (const entt::entity entity : roots) {
            showHierarchyEntity(entity);
        }
        ImGui::TreePop();
    }
    if (m_hierarchyDropPending) {
        const entt::entity child =
            m_scene.findEntity(m_hierarchyDropChild);
        const entt::entity parent =
            m_scene.findEntity(m_hierarchyDropParent);
        static_cast<void>(m_scene.setParent(child, parent));
    }
    if (!m_scene.lastError().empty()) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.30f, 1.0f),
            "%s", m_scene.lastError().c_str());
    }
    ImGui::End();
}

void ModelSceneAppState::showHierarchyEntity(const entt::entity entity) {
    const scene::SceneEntityId id = m_scene.entityId(entity);
    const auto& name =
        m_scene.registry().get<scene::NameComponent>(entity).value;
    const auto& children =
        m_scene.registry().get<ecs::ChildrenComponent>(entity).children;
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf |
                 ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (entity == m_scene.selectedEntity()) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const std::string label =
        name + "###SceneEntity" + std::to_string(id);
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked()) {
        m_scene.setSelectedEntity(entity);
    }
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(
            "MODEL_SCENE_ENTITY_ID", &id, sizeof(id));
        ImGui::TextUnformatted(name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                "MODEL_SCENE_ENTITY_ID")) {
            if (payload->DataSize == sizeof(scene::SceneEntityId)) {
                m_hierarchyDropChild =
                    *static_cast<const scene::SceneEntityId*>(payload->Data);
                m_hierarchyDropParent = id;
                m_hierarchyDropPending = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (open && !children.empty()) {
        for (const entt::entity child : children) {
            showHierarchyEntity(child);
        }
        ImGui::TreePop();
    }
}

void ModelSceneAppState::showInspectorPanel() {
    ImGui::Begin("Inspector");
    ImGui::TextUnformatted("Environment");
    float timeOfDay = m_scene.timeOfDay();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat(
            "Time of Day", &timeOfDay, 0.0f, 1199.0f, "%.0f s")) {
        m_scene.setTimeOfDay(timeOfDay);
    }
    ImGui::Separator();

    const entt::entity selected = m_scene.selectedEntity();
    if (selected == entt::null || !m_scene.registry().valid(selected)) {
        ImGui::TextUnformatted("No entity selected");
        ImGui::End();
        return;
    }
    const auto* name = m_scene.registry().try_get<scene::NameComponent>(selected);
    auto* transform =
        m_scene.registry().try_get<ecs::LocalTransformComponent>(selected);
    if (name != nullptr) {
        ImGui::TextUnformatted(name->value.c_str());
        ImGui::TextDisabled(
            "Entity ID: %llu",
            static_cast<unsigned long long>(m_scene.entityId(selected)));
        ImGui::Separator();
    }
    if (transform != nullptr) {
        bool changed = false;
        changed |= ImGui::DragFloat3(
            "Position", glm::value_ptr(transform->localPosition), 0.02f);
        changed |= ImGui::DragFloat3(
            "Rotation", glm::value_ptr(transform->localRotation), 0.25f);
        changed |= ImGui::DragFloat3(
            "Scale", glm::value_ptr(transform->localScale), 0.01f, 0.001f, 1000.0f);
        if (changed) {
            m_scene.syncTransforms();
        }
    }
    const auto* pickable =
        m_scene.registry().try_get<scene::PickableComponent>(selected);
    if (pickable != nullptr && ImGui::CollapsingHeader("Local Bounds")) {
        ImGui::Text("Min: %.3f, %.3f, %.3f",
                    pickable->localBoundsMin.x,
                    pickable->localBoundsMin.y,
                    pickable->localBoundsMin.z);
        ImGui::Text("Max: %.3f, %.3f, %.3f",
                    pickable->localBoundsMax.x,
                    pickable->localBoundsMax.y,
                    pickable->localBoundsMax.z);
    }
    ImGui::Separator();
    if (ImGui::Button("Delete Entity")) {
        m_scene.destroyEntity(selected);
    }
    ImGui::End();
}

void ModelSceneAppState::showAssetsPanel() {
    ImGui::Begin("Assets");
    const ImGuiStyle& style = ImGui::GetStyle();
    const float browseButtonWidth =
        ImGui::CalcTextSize("Browse...").x + style.FramePadding.x * 2.0f;
    const float importButtonWidth =
        ImGui::CalcTextSize("Import Model").x + style.FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(
        browseButtonWidth + importButtonWidth +
        style.ItemSpacing.x * 2.0f));
    ImGui::InputText("##ModelPath", m_importPath.data(), m_importPath.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        browseAndImportModel();
    }
    ImGui::SameLine();
    if (ImGui::Button("Import Model")) {
        importModelPath(m_importPath.data());
    }
    if (!m_importDialogError.empty()) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.30f, 1.0f),
            "%s", m_importDialogError.c_str());
    }
    if (!m_scene.lastError().empty()) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.30f, 1.0f),
            "%s", m_scene.lastError().c_str());
    }
    ImGui::Separator();
    for (size_t index = 0u; index < m_scene.assetCount(); ++index) {
        ImGui::TextUnformatted(m_scene.assetName(index).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("glTF / GLB");
        ImGui::TextDisabled("%s", m_scene.assetPath(index).c_str());
    }
    ImGui::End();
}

void ModelSceneAppState::browseAndImportModel() {
    m_importDialogError.clear();
    if (NFD_Init() != NFD_OKAY) {
        m_importDialogError = NFD_GetError();
        return;
    }

    const nfdfilteritem_t filters[] = {{"glTF Model", "gltf,glb"}};
    nfdchar_t* selectedPath = nullptr;
    const nfdresult_t result = NFD_OpenDialog(
        &selectedPath, filters, std::size(filters), nullptr);
    if (result == NFD_OKAY) {
        const std::string path(selectedPath);
        NFD_FreePath(selectedPath);
        NFD_Quit();
        if (path.size() >= m_importPath.size()) {
            m_importDialogError = "Selected model path exceeds the import field capacity";
            return;
        }
        std::fill(m_importPath.begin(), m_importPath.end(), '\0');
        std::copy(path.begin(), path.end(), m_importPath.begin());
        importModelPath(path);
        return;
    }
    if (result == NFD_ERROR) {
        m_importDialogError = NFD_GetError();
    }
    NFD_Quit();
}

void ModelSceneAppState::importModelPath(const std::string& path) {
    m_importDialogError.clear();
    const entt::entity imported = m_scene.importModel(path);
    if (imported != entt::null) {
        m_scene.setSelectedEntity(imported);
    }
}

void ModelSceneAppState::showViewportPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Scene Viewport", nullptr,
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x < 1.0f || available.y < 1.0f) {
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }
    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    const uint32_t targetWidth = viewportDimension(available.x, framebufferScale.x);
    const uint32_t targetHeight = viewportDimension(available.y, framebufferScale.y);
    if (!m_scene.ensureViewport(targetWidth, targetHeight)) {
        ImGui::TextUnformatted(m_scene.lastError().c_str());
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }
    m_viewportPosition = ImGui::GetCursorScreenPos();
    m_viewportSize = available;
    const bool flipTextureVertically =
        m_deps.rhiDevice.backend() == RhiBackend::OpenGL;
    const ImVec2 uv0 = flipTextureVertically
        ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
    const ImVec2 uv1 = flipTextureVertically
        ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
    ImGui::Image(
        static_cast<ImTextureID>(m_scene.viewportTextureId()),
        available, uv0, uv1);
    const bool viewportImageHovered = ImGui::IsItemHovered();
    const bool gizmoToolbarHovered = showGizmoToolbar();
    m_viewportHovered = viewportImageHovered && !gizmoToolbarHovered;

    const float aspect = available.x / available.y;
    m_view = glm::lookAt(
        cameraPosition(), m_cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    m_projection = glm::perspective(glm::radians(55.0f), aspect, 0.05f, 500.0f);

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(
        m_viewportPosition.x, m_viewportPosition.y, available.x, available.y);
    const entt::entity selected = m_scene.selectedEntity();
    if (selected != entt::null && m_scene.registry().valid(selected)) {
        auto* world =
            m_scene.registry().try_get<ecs::WorldTransformComponent>(selected);
        if (world != nullptr) {
            glm::mat4 manipulated = world->worldMatrix;
            if (ImGuizmo::Manipulate(
                    glm::value_ptr(m_view), glm::value_ptr(m_projection),
                    gizmoOperation(m_gizmoOperation),
                    m_gizmoMode == 0 ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                    glm::value_ptr(manipulated))) {
                static_cast<void>(
                    m_scene.setWorldTransform(selected, manipulated));
            }
        }
    }
    if (m_viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !gizmoToolbarHovered &&
        !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
        selectFromViewport(ImGui::GetMousePos());
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

bool ModelSceneAppState::showGizmoToolbar() {
    ImGui::SetCursorScreenPos(ImVec2(
        m_viewportPosition.x + 8.0f, m_viewportPosition.y + 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg, ImVec4(0.055f, 0.060f, 0.070f, 0.94f));
    ImGui::BeginChild(
        "##GizmoToolbar", ImVec2(196.0f, 38.0f), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    bool hovered = ImGui::IsWindowHovered();
    const auto operationButton = [&](const char* label,
                                     const int operation,
                                     const char* tooltip) {
        const bool active = m_gizmoOperation == operation;
        if (active) {
            ImGui::PushStyleColor(
                ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.68f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(28.0f, 24.0f))) {
            m_gizmoOperation = operation;
            if (operation == 2) {
                m_gizmoMode = 0;
            }
        }
        hovered = hovered || ImGui::IsItemHovered();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        if (active) {
            ImGui::PopStyleColor();
        }
    };
    operationButton("T", 0, "Translate");
    ImGui::SameLine();
    operationButton("R", 1, "Rotate");
    ImGui::SameLine();
    operationButton("S", 2, "Scale");
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0.0f, 8.0f);

    const auto modeButton = [&](const char* label,
                                const int mode,
                                const char* tooltip) {
        const bool disabled = m_gizmoOperation == 2 && mode == 1;
        const bool active = m_gizmoMode == mode;
        ImGui::BeginDisabled(disabled);
        if (active) {
            ImGui::PushStyleColor(
                ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.68f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(28.0f, 24.0f))) {
            m_gizmoMode = mode;
        }
        hovered = hovered || ImGui::IsItemHovered();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        if (active) {
            ImGui::PopStyleColor();
        }
        ImGui::EndDisabled();
    };
    modeButton("L", 0, "Local coordinates");
    ImGui::SameLine();
    modeButton("W", 1, "World coordinates");

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return hovered;
}

void ModelSceneAppState::selectFromViewport(const ImVec2& mousePosition) {
    const float normalizedX =
        (mousePosition.x - m_viewportPosition.x) / m_viewportSize.x;
    const float normalizedY =
        (mousePosition.y - m_viewportPosition.y) / m_viewportSize.y;
    const float clipX = normalizedX * 2.0f - 1.0f;
    const float clipY = 1.0f - normalizedY * 2.0f;
    const glm::mat4 inverseViewProjection = glm::inverse(m_projection * m_view);
    glm::vec4 nearPoint =
        inverseViewProjection * glm::vec4(clipX, clipY, -1.0f, 1.0f);
    glm::vec4 farPoint =
        inverseViewProjection * glm::vec4(clipX, clipY, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    const glm::vec3 origin = glm::vec3(nearPoint);
    const glm::vec3 direction = glm::normalize(
        glm::vec3(farPoint - nearPoint));
    m_scene.setSelectedEntity(m_scene.pick(origin, direction));
}

void ModelSceneAppState::render(const double frameTime) {
    if (!m_initialized || m_returnRequested) {
        return;
    }
    const Window::FramebufferSize framebufferSize =
        m_deps.window.getFramebufferSize();
    if (framebufferSize.width <= 0 || framebufferSize.height <= 0) {
        return;
    }
    if (!m_deps.rhiDevice.resizeSwapchain(
            static_cast<uint32_t>(framebufferSize.width),
            static_cast<uint32_t>(framebufferSize.height))) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[ModelSceneAppState] Failed to resize swapchain\n");
        return;
    }
    const RhiFrameAcquireResult frame = m_deps.rhiDevice.acquireFrame();
    if (frame.status == RhiFrameStatus::Minimized ||
        frame.status == RhiFrameStatus::OutOfDate) {
        return;
    }
    if (frame.status != RhiFrameStatus::Success &&
        frame.status != RhiFrameStatus::Suboptimal) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[ModelSceneAppState] Failed to acquire frame\n");
        return;
    }
    if (!m_imguiRenderer.beginFrame(
            static_cast<int>(frame.width), static_cast<int>(frame.height))) {
        std::abort();
    }
    ImGuizmo::BeginFrame();
    buildEditorUi();
    if (m_scene.viewportWidth() != 0u &&
        !m_scene.renderViewport(
            m_view, m_projection, cameraPosition(),
            static_cast<float>(frameTime))) {
        std::abort();
    }
    RhiCommandList* commandList =
        m_deps.commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr ||
        !commandList->begin({"ModelScene.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    if (!m_imguiRenderer.prepareDrawData(*commandList)) {
        std::abort();
    }
    commandList->textureBarrier({
        m_deps.rhiDevice.currentSwapchainColorTexture(),
        RhiResourceState::Present,
        RhiResourceState::RenderTarget});
    RhiColorAttachment colorAttachment;
    colorAttachment.view = m_deps.rhiDevice.currentSwapchainColorView();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.035f;
    colorAttachment.clearColor[1] = 0.040f;
    colorAttachment.clearColor[2] = 0.045f;
    colorAttachment.clearColor[3] = 1.0f;
    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view =
        m_deps.rhiDevice.currentSwapchainDepthStencilView();
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "ModelScene.Ui";
    renderingInfo.renderArea = {0, 0, frame.width, frame.height};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;
    commandList->beginRendering(renderingInfo);
    m_imguiRenderer.recordDraws(*commandList);
    commandList->endRendering();
    commandList->textureBarrier({
        m_deps.rhiDevice.currentSwapchainColorTexture(),
        RhiResourceState::RenderTarget,
        RhiResourceState::Present});
    if (!commandList->end()) {
        std::abort();
    }
    RhiCommandList* submitted[] = {commandList};
    if (!m_deps.rhiDevice.submit(
            {"ModelScene.Submit", submitted, 1u, RhiQueueType::Graphics})) {
        std::abort();
    }
    const RhiFrameStatus presentStatus = m_deps.rhiDevice.presentFrame(
        {frame.frameIndex, frame.imageIndex, Time::getFrameIndex()});
    if (presentStatus != RhiFrameStatus::Success &&
        presentStatus != RhiFrameStatus::Suboptimal &&
        presentStatus != RhiFrameStatus::OutOfDate &&
        presentStatus != RhiFrameStatus::Minimized) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[ModelSceneAppState] Failed to present frame\n");
    }
}
