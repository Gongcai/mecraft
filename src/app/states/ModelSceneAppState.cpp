#include "ModelSceneAppState.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nfd.h>

#include "ImGuizmo.h"
#include "imgui_internal.h"

#include "MainMenuAppState.h"
#include "app/AppSettings.h"
#include "app/validation/ValidationRunController.h"
#include "Diagnostics.h"
#include "ecs/components/TransformComponents.h"
#include "engine/input/InputManager.h"
#include "engine/camera/Camera.h"
#include "engine/platform/Time.h"
#include "renderer/capture/TextureCapture.h"
#include "renderer/core/FrameContext.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiCommandListPool.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiDeviceFactory.h"
#include "renderer/core/RenderSettings.h"
#include "scene/ModelSceneComponents.h"
#include "scene/ModelSceneSerializer.h"
#include "ui/imgui/RenderSettingsImGui.h"

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kCameraLookSensitivity = 0.25f;
constexpr float kCameraMoveSpeed = 3.0f;
constexpr float kCameraFastMoveMultiplier = 4.0f;
constexpr const char* kDefaultModelPath = "assets/models/showcase/DamagedHelmet.glb";

[[nodiscard]] std::string normalizedScenePath(const std::string& path, std::string& error) {
    std::error_code filesystemError;
    std::filesystem::path normalized = std::filesystem::absolute(std::filesystem::u8path(path), filesystemError);
    if (filesystemError) {
        error = "Failed to resolve scene path: " + filesystemError.message();
        return {};
    }
    normalized = std::filesystem::weakly_canonical(normalized, filesystemError);
    if (filesystemError) {
        error = "Failed to normalize scene path: " + filesystemError.message();
        return {};
    }
    return normalized.generic_u8string();
}

[[nodiscard]] std::string scenePathWithExtension(const std::string& path) {
    std::filesystem::path result = std::filesystem::u8path(path);
    if (result.extension() != ".scene") {
        result += ".scene";
    }
    return result.generic_u8string();
}

[[nodiscard]] uint32_t viewportDimension(const float logicalSize, const float framebufferScale) {
    return static_cast<uint32_t>(std::max(1.0f, std::floor(logicalSize * framebufferScale)));
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

ModelSceneAppState::ModelSceneAppState(AppStateDependencies deps) : m_deps(deps) {}

void ModelSceneAppState::onEnter() {
    const auto failValidationInitialization = [this](std::string detail) {
        if (m_deps.validationRun.enabled() && !m_deps.validationRun.failed()) {
            m_deps.validationRun.fail(app::validation::ValidationRunError::SceneInitializationFailed,
                                      std::move(detail));
        }
    };
    m_deps.contextManager.pushContext(InputContextType::UI);
    m_cameraControlActive = false;
    m_deps.input.captureMouse(false);
    m_returnRequested = false;
    if (!m_imguiRenderer.init(m_deps.window, m_deps.rhiDevice, true, "model_scene_imgui.ini")) {
        MECRAFT_LOG_STREAM(std::cerr << "[ModelSceneAppState] Failed to initialize ImGui\n");
        failValidationInitialization("model validation ImGui renderer initialization failed");
        return;
    }
    if (!m_scene.init(m_deps.resourceMgr, m_deps.rhiDevice, m_deps.commandListPool, m_imguiRenderer)) {
        MECRAFT_LOG_STREAM(std::cerr << "[ModelSceneAppState] " << m_scene.lastError() << '\n');
        failValidationInitialization(m_scene.lastError());
        return;
    }
    std::string initialModelPath = kDefaultModelPath;
    if (m_deps.validationRun.enabled()) {
        const app::validation::ValidationSceneContract& contract = m_deps.validationRun.sceneContract();
        if (contract.scene != ValidationScene::Model || !contract.modelAsset.has_value()) {
            failValidationInitialization("model validation requires one verified model asset");
            m_scene.shutdown();
            return;
        }
        initialModelPath = contract.modelAsset->resolvedPath.generic_u8string();
    }
    if (m_scene.importModel(initialModelPath) == entt::null) {
        MECRAFT_LOG_STREAM(std::cerr << "[ModelSceneAppState] " << m_scene.lastError() << '\n');
        failValidationInitialization(m_scene.lastError());
        m_scene.shutdown();
        return;
    }
    m_scenePath.clear();
    m_sceneIoError.clear();
    m_history.clear();
    m_nonHistoryDirty = false;
    m_sceneDirty = false;
    m_pendingSceneAction = PendingSceneAction::None;
    m_openUnsavedPopup = false;
    m_pendingEntityAction = PendingEntityAction::None;
    m_pendingEntityId = scene::kInvalidSceneEntityId;
    m_entityNameEditorId = scene::kInvalidSceneEntityId;
    m_transformCommandActive = false;
    m_transformCommandFromGizmo = false;
    m_transformCommandEntityId = scene::kInvalidSceneEntityId;
    if (m_deps.validationRun.enabled()) {
        const app::validation::ValidationSceneContract& contract = m_deps.validationRun.sceneContract();
        if (!m_scene.generateReflectionProbeGrid(8.0f, 1.0f)) {
            failValidationInitialization(m_scene.lastError());
            return;
        }
        if (!m_scene.setRenderSettings(m_deps.validationRun.renderSettingsProfile().settings)) {
            failValidationInitialization(m_scene.lastError());
            return;
        }
        m_scene.setTimeOfDay(static_cast<float>(contract.environment.timeOfDaySeconds));
        m_scene.setTimePaused(true);
        m_scene.setTimeScale(1.0f);
        m_scene.setWeather(WeatherType::Clear, true);
        if (!m_deps.validationRun.beginScene(ValidationScene::Model)) {
            return;
        }
        m_validationActive = true;
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
    m_validationActive = false;
}

void ModelSceneAppState::requestReturnToMenu() {
    if (!m_returnRequested) {
        m_returnRequested = true;
        m_deps.appFsm.changeState(std::make_unique<MainMenuAppState>(m_deps));
    }
}

void ModelSceneAppState::markSceneDirty() {
    m_nonHistoryDirty = true;
    refreshSceneDirty();
}

void ModelSceneAppState::refreshSceneDirty() {
    m_sceneDirty = m_nonHistoryDirty || !m_history.isAtSavedState();
}

void ModelSceneAppState::recordCreatedEntity(const entt::entity entity) {
    std::vector<scene::SceneEntityDocument> states;
    if (!m_scene.captureEntitySubtree(entity, states)) {
        std::abort();
    }
    m_history.recordCreatedSubtree(std::move(states));
    refreshSceneDirty();
}

void ModelSceneAppState::beginTransformCommand(const entt::entity entity, const scene::SceneEntityDocument& before,
                                               const bool fromGizmo) {
    if (m_transformCommandActive) {
        return;
    }
    m_transformCommandActive = true;
    m_transformCommandFromGizmo = fromGizmo;
    m_transformCommandEntityId = m_scene.entityId(entity);
    m_transformCommandBefore = before;
}

void ModelSceneAppState::finishTransformCommand() {
    if (!m_transformCommandActive) {
        return;
    }
    const entt::entity entity = m_scene.findEntity(m_transformCommandEntityId);
    scene::SceneEntityDocument after;
    if (entity != entt::null && m_scene.captureEntityState(entity, after)) {
        m_history.recordEntityState(m_transformCommandBefore, after);
    }
    m_transformCommandActive = false;
    m_transformCommandFromGizmo = false;
    m_transformCommandEntityId = scene::kInvalidSceneEntityId;
    refreshSceneDirty();
}

void ModelSceneAppState::undoSceneCommand() {
    if (m_transformCommandActive || !m_history.canUndo()) {
        return;
    }
    if (m_history.undo(m_scene)) {
        m_entityNameEditorId = scene::kInvalidSceneEntityId;
        refreshSceneDirty();
    }
}

void ModelSceneAppState::redoSceneCommand() {
    if (m_transformCommandActive || !m_history.canRedo()) {
        return;
    }
    if (m_history.redo(m_scene)) {
        m_entityNameEditorId = scene::kInvalidSceneEntityId;
        refreshSceneDirty();
    }
}

void ModelSceneAppState::handleEditorShortcuts(const InputSnapshot& input) {
    if (m_cameraControlActive || ImGuizmo::IsUsing() || ImGui::GetIO().WantTextInput ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        return;
    }
    const bool control = input.isKeyHeld(GLFW_KEY_LEFT_CONTROL) || input.isKeyHeld(GLFW_KEY_RIGHT_CONTROL);
    const bool shift = input.isKeyHeld(GLFW_KEY_LEFT_SHIFT) || input.isKeyHeld(GLFW_KEY_RIGHT_SHIFT);
    if (control && input.isKeyJustPressed(GLFW_KEY_S)) {
        static_cast<void>(shift ? saveSceneAs() : saveScene());
        return;
    }
    if (control && input.isKeyJustPressed(GLFW_KEY_Z)) {
        if (shift) {
            redoSceneCommand();
        } else {
            undoSceneCommand();
        }
        return;
    }
    if (control && input.isKeyJustPressed(GLFW_KEY_Y)) {
        redoSceneCommand();
        return;
    }
    if (control && input.isKeyJustPressed(GLFW_KEY_O)) {
        requestSceneAction(PendingSceneAction::OpenScene);
        return;
    }
    if (control && input.isKeyJustPressed(GLFW_KEY_N)) {
        requestSceneAction(PendingSceneAction::NewScene);
        return;
    }
    if (control && input.isKeyJustPressed(GLFW_KEY_D)) {
        duplicateSelectedEntity();
        return;
    }
    if (input.isKeyJustPressed(GLFW_KEY_DELETE)) {
        deleteSelectedEntity();
        return;
    }
    if (m_viewportHovered && input.isKeyJustPressed(GLFW_KEY_F)) {
        focusSelectedEntity();
    }
}

void ModelSceneAppState::queueEntityAction(const PendingEntityAction action, const scene::SceneEntityId entityId) {
    m_pendingEntityAction = action;
    m_pendingEntityId = entityId;
}

void ModelSceneAppState::executePendingEntityAction() {
    if (m_pendingEntityAction == PendingEntityAction::None) {
        return;
    }
    const PendingEntityAction action = m_pendingEntityAction;
    const entt::entity entity = m_scene.findEntity(m_pendingEntityId);
    m_pendingEntityAction = PendingEntityAction::None;
    m_pendingEntityId = scene::kInvalidSceneEntityId;
    if (entity == entt::null) {
        return;
    }
    m_scene.setSelectedEntity(entity);
    switch (action) {
    case PendingEntityAction::None: return;
    case PendingEntityAction::Duplicate: duplicateSelectedEntity(); return;
    case PendingEntityAction::Delete: deleteSelectedEntity(); return;
    case PendingEntityAction::Focus: focusSelectedEntity(); return;
    default: std::abort();
    }
}

void ModelSceneAppState::duplicateSelectedEntity() {
    const entt::entity selected = m_scene.selectedEntity();
    if (selected == entt::null || !m_scene.registry().valid(selected)) {
        return;
    }
    if (m_scene.duplicateEntity(selected) != entt::null) {
        recordCreatedEntity(m_scene.selectedEntity());
        m_entityNameEditorId = scene::kInvalidSceneEntityId;
    }
}

void ModelSceneAppState::deleteSelectedEntity() {
    const entt::entity selected = m_scene.selectedEntity();
    if (selected == entt::null || !m_scene.registry().valid(selected)) {
        return;
    }
    std::vector<scene::SceneEntityDocument> states;
    if (!m_scene.captureEntitySubtree(selected, states)) {
        std::abort();
    }
    m_scene.destroyEntity(selected);
    m_history.recordDeletedSubtree(std::move(states));
    m_entityNameEditorId = scene::kInvalidSceneEntityId;
    refreshSceneDirty();
}

void ModelSceneAppState::focusSelectedEntity() {
    const entt::entity selected = m_scene.selectedEntity();
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    if (!m_scene.entityWorldBounds(selected, boundsMin, boundsMax)) {
        return;
    }
    m_cameraTarget = (boundsMin + boundsMax) * 0.5f;
    const float radius = glm::length(boundsMax - boundsMin) * 0.5f;
    const float framingDistance = radius / std::tan(glm::radians(27.5f)) * 1.2f;
    m_cameraDistance = std::clamp(std::max(2.0f, framingDistance), 0.6f, 80.0f);
    markSceneDirty();
}

scene::SceneEditorCameraDocument ModelSceneAppState::captureEditorCamera() const {
    scene::SceneEditorCameraDocument camera;
    camera.target = m_cameraTarget;
    camera.distance = m_cameraDistance;
    camera.yaw = m_cameraYaw;
    camera.pitch = m_cameraPitch;
    camera.nearPlane = m_cameraNearPlane;
    camera.farPlane = m_cameraFarPlane;
    return camera;
}

void ModelSceneAppState::applyEditorCamera(const scene::SceneEditorCameraDocument& camera) {
    m_cameraTarget = camera.target;
    m_cameraDistance = camera.distance;
    m_cameraYaw = camera.yaw;
    m_cameraPitch = camera.pitch;
    m_cameraNearPlane = camera.nearPlane;
    m_cameraFarPlane = camera.farPlane;
}

void ModelSceneAppState::requestSceneAction(const PendingSceneAction action) {
    if (action == PendingSceneAction::None) {
        return;
    }
    if (!m_sceneDirty) {
        executeSceneAction(action);
        return;
    }
    m_pendingSceneAction = action;
    m_openUnsavedPopup = true;
}

void ModelSceneAppState::executeSceneAction(const PendingSceneAction action) {
    switch (action) {
    case PendingSceneAction::None: return;
    case PendingSceneAction::NewScene: newScene(); return;
    case PendingSceneAction::OpenScene: openSceneDialog(); return;
    case PendingSceneAction::ReturnToMenu: requestReturnToMenu(); return;
    default: std::abort();
    }
}

void ModelSceneAppState::newScene() {
    m_scene.clearScene();
    m_scene.resetEnvironment();
    applyEditorCamera(scene::SceneEditorCameraDocument{});
    m_scenePath.clear();
    m_sceneIoError.clear();
    m_history.clear();
    m_nonHistoryDirty = false;
    refreshSceneDirty();
    m_entityNameEditorId = scene::kInvalidSceneEntityId;
    m_transformCommandActive = false;
    m_transformCommandFromGizmo = false;
    m_transformCommandEntityId = scene::kInvalidSceneEntityId;
    m_reflectionProbeEditorId = scene::kInvalidSceneReflectionProbeId;
}

bool ModelSceneAppState::loadScenePath(const std::string& path) {
    m_sceneIoError.clear();
    const std::string normalized = normalizedScenePath(path, m_sceneIoError);
    if (normalized.empty()) {
        return false;
    }
    scene::ModelSceneDocument document;
    if (!scene::ModelSceneSerializer::loadFromFile(normalized, document, m_sceneIoError)) {
        return false;
    }
    if (!m_scene.loadDocument(document)) {
        m_sceneIoError = m_scene.lastError();
        return false;
    }
    applyEditorCamera(document.editorCamera);
    m_scenePath = normalized;
    m_history.clear();
    m_nonHistoryDirty = false;
    refreshSceneDirty();
    m_entityNameEditorId = scene::kInvalidSceneEntityId;
    m_transformCommandActive = false;
    m_transformCommandFromGizmo = false;
    m_transformCommandEntityId = scene::kInvalidSceneEntityId;
    m_reflectionProbeEditorId = scene::kInvalidSceneReflectionProbeId;
    return true;
}

void ModelSceneAppState::openSceneDialog() {
    m_sceneIoError.clear();
    if (NFD_Init() != NFD_OKAY) {
        m_sceneIoError = NFD_GetError();
        return;
    }
    const nfdfilteritem_t filters[] = {{"Mecraft Scene", "scene"}};
    nfdchar_t* selectedPath = nullptr;
    std::string defaultPath;
    if (!m_scenePath.empty()) {
        defaultPath = std::filesystem::u8path(m_scenePath).parent_path().generic_u8string();
    }
    const nfdresult_t result =
        NFD_OpenDialog(&selectedPath, filters, std::size(filters), defaultPath.empty() ? nullptr : defaultPath.c_str());
    if (result == NFD_OKAY) {
        const std::string path(selectedPath);
        NFD_FreePath(selectedPath);
        NFD_Quit();
        static_cast<void>(loadScenePath(path));
        return;
    }
    if (result == NFD_ERROR) {
        m_sceneIoError = NFD_GetError();
    }
    NFD_Quit();
}

bool ModelSceneAppState::saveSceneToPath(const std::string& path) {
    m_sceneIoError.clear();
    const std::string withExtension = scenePathWithExtension(path);
    const std::string normalized = normalizedScenePath(withExtension, m_sceneIoError);
    if (normalized.empty()) {
        return false;
    }
    const scene::ModelSceneDocument document = m_scene.captureDocument(captureEditorCamera());
    if (!scene::ModelSceneSerializer::saveToFile(normalized, document, m_sceneIoError)) {
        return false;
    }
    m_scenePath = normalized;
    m_history.markSaved();
    m_nonHistoryDirty = false;
    refreshSceneDirty();
    return true;
}

bool ModelSceneAppState::saveSceneAs() {
    m_sceneIoError.clear();
    if (NFD_Init() != NFD_OKAY) {
        m_sceneIoError = NFD_GetError();
        return false;
    }
    const nfdfilteritem_t filters[] = {{"Mecraft Scene", "scene"}};
    nfdchar_t* selectedPath = nullptr;
    std::string defaultPath;
    std::string defaultName = "Untitled.scene";
    if (!m_scenePath.empty()) {
        const std::filesystem::path current = std::filesystem::u8path(m_scenePath);
        defaultPath = current.parent_path().generic_u8string();
        defaultName = current.filename().generic_u8string();
    }
    const nfdresult_t result = NFD_SaveDialog(&selectedPath, filters, std::size(filters),
                                              defaultPath.empty() ? nullptr : defaultPath.c_str(), defaultName.c_str());
    if (result == NFD_OKAY) {
        const std::string path(selectedPath);
        NFD_FreePath(selectedPath);
        NFD_Quit();
        return saveSceneToPath(path);
    }
    if (result == NFD_ERROR) {
        m_sceneIoError = NFD_GetError();
    }
    NFD_Quit();
    return false;
}

bool ModelSceneAppState::saveScene() {
    return m_scenePath.empty() ? saveSceneAs() : saveSceneToPath(m_scenePath);
}

void ModelSceneAppState::update(const double frameTime, double& accumulator) {
    accumulator = 0.0;
    if (!m_initialized) {
        requestReturnToMenu();
        return;
    }
    if (m_validationActive) {
        accumulator = 0.0;
        m_scene.syncTransforms();
        return;
    }
    m_deps.input.update();
    const InputSnapshot& input = m_deps.input.snapshot();
    if (input.isKeyJustPressed(GLFW_KEY_ESCAPE)) {
        if (m_cameraControlActive) {
            setCameraControlActive(false);
        } else {
            requestSceneAction(PendingSceneAction::ReturnToMenu);
        }
        return;
    }
    handleEditorShortcuts(input);
    updateCamera(input, frameTime);
    if (m_viewportHovered && !m_cameraControlActive && !ImGuizmo::IsUsing() && !ImGui::GetIO().WantTextInput) {
        if (input.isKeyJustPressed(GLFW_KEY_W))
            m_gizmoOperation = 0;
        if (input.isKeyJustPressed(GLFW_KEY_E))
            m_gizmoOperation = 1;
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
    const glm::vec3 direction{std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
    return m_cameraTarget + direction * m_cameraDistance;
}

void ModelSceneAppState::setCameraControlActive(const bool active) {
    if (m_cameraControlActive == active) {
        return;
    }
    m_cameraControlActive = active;
    m_deps.input.captureMouse(active);
}

void ModelSceneAppState::updateCamera(const InputSnapshot& input, const double frameTime) {
    const glm::vec3 previousTarget = m_cameraTarget;
    const float previousDistance = m_cameraDistance;
    const float previousYaw = m_cameraYaw;
    const float previousPitch = m_cameraPitch;
    const bool canStartControl = m_viewportHovered && !ImGuizmo::IsUsing() && !ImGui::GetIO().WantTextInput;
    const bool startingControl =
        !m_cameraControlActive && canStartControl && input.isMouseButtonJustPressed(GLFW_MOUSE_BUTTON_RIGHT);
    if (startingControl) {
        setCameraControlActive(true);
    }
    if (m_cameraControlActive && input.isMouseButtonJustReleased(GLFW_MOUSE_BUTTON_RIGHT)) {
        setCameraControlActive(false);
        return;
    }

    if (m_cameraControlActive) {
        if (!startingControl) {
            m_cameraYaw = std::remainder(m_cameraYaw + input.mouseDelta.x * kCameraLookSensitivity, 360.0f);
            m_cameraPitch = std::clamp(m_cameraPitch + input.mouseDelta.y * kCameraLookSensitivity, -85.0f, 85.0f);
        }

        const glm::vec3 position = cameraPosition();
        const glm::vec3 forward = glm::normalize(m_cameraTarget - position);
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 movement{0.0f};
        if (input.isKeyHeld(GLFW_KEY_W))
            movement += forward;
        if (input.isKeyHeld(GLFW_KEY_S))
            movement -= forward;
        if (input.isKeyHeld(GLFW_KEY_D))
            movement += right;
        if (input.isKeyHeld(GLFW_KEY_A))
            movement -= right;
        if (glm::dot(movement, movement) > 0.0f) {
            const bool fastMove = input.isKeyHeld(GLFW_KEY_LEFT_SHIFT) || input.isKeyHeld(GLFW_KEY_RIGHT_SHIFT);
            const float speed = kCameraMoveSpeed * (fastMove ? kCameraFastMoveMultiplier : 1.0f);
            m_cameraTarget += glm::normalize(movement) * speed * static_cast<float>(frameTime);
        }
    }

    if ((m_viewportHovered || m_cameraControlActive) && input.scrollDelta != 0.0) {
        const float zoomFactor = std::exp(static_cast<float>(-input.scrollDelta) * 0.12f);
        m_cameraDistance = std::clamp(m_cameraDistance * zoomFactor, 0.6f, 80.0f);
    }
    if (previousTarget != m_cameraTarget || previousDistance != m_cameraDistance || previousYaw != m_cameraYaw ||
        previousPitch != m_cameraPitch) {
        markSceneDirty();
    }
}

void ModelSceneAppState::buildInitialDockLayout(const ImGuiID dockspaceId) {
    ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspaceId);
    if (root != nullptr && !root->IsEmpty()) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);
    ImGuiID center = dockspaceId;
    ImGuiID left = 0u;
    ImGuiID right = 0u;
    ImGuiID bottom = 0u;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, &right, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24f, &bottom, &center);
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
            if (ImGui::MenuItem("New Scene")) {
                requestSceneAction(PendingSceneAction::NewScene);
            }
            if (ImGui::MenuItem("Open Scene...")) {
                requestSceneAction(PendingSceneAction::OpenScene);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Scene")) {
                static_cast<void>(saveScene());
            }
            if (ImGui::MenuItem("Save Scene As...")) {
                static_cast<void>(saveSceneAs());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Return to Main Menu")) {
                requestSceneAction(PendingSceneAction::ReturnToMenu);
            }
            ImGui::EndMenu();
        }
        const entt::entity selected = m_scene.selectedEntity();
        const bool hasSelection = selected != entt::null && m_scene.registry().valid(selected);
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", nullptr, false, m_history.canUndo())) {
                undoSceneCommand();
            }
            if (ImGui::MenuItem("Redo", nullptr, false, m_history.canRedo())) {
                redoSceneCommand();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate Entity", nullptr, false, hasSelection)) {
                duplicateSelectedEntity();
            }
            if (ImGui::MenuItem("Delete Entity", nullptr, false, hasSelection)) {
                deleteSelectedEntity();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Focus Selection", nullptr, false, hasSelection)) {
                focusSelectedEntity();
            }
            ImGui::EndMenu();
        }
        const std::string sceneName =
            m_scenePath.empty() ? "Untitled" : std::filesystem::u8path(m_scenePath).filename().generic_u8string();
        ImGui::TextDisabled("%s%s", sceneName.c_str(), m_sceneDirty ? " *" : "");
        ImGui::EndMainMenuBar();
    }
    const ImGuiID dockspaceId = ImGui::GetID("ModelSceneDockspaceV2");
    ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_None);
    buildInitialDockLayout(dockspaceId);
    showHierarchyPanel();
    showInspectorPanel();
    showRenderSettingsPanel();
    showAssetsPanel();
    showViewportPanel();
    showUnsavedChangesModal();
}

void ModelSceneAppState::showUnsavedChangesModal() {
    if (m_openUnsavedPopup) {
        ImGui::OpenPopup("Unsaved Scene");
        m_openUnsavedPopup = false;
    }
    if (!ImGui::BeginPopupModal("Unsaved Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted("Save changes to the current scene?");
    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
        if (saveScene()) {
            const PendingSceneAction action = m_pendingSceneAction;
            m_pendingSceneAction = PendingSceneAction::None;
            ImGui::CloseCurrentPopup();
            executeSceneAction(action);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(100.0f, 0.0f))) {
        const PendingSceneAction action = m_pendingSceneAction;
        m_pendingSceneAction = PendingSceneAction::None;
        ImGui::CloseCurrentPopup();
        executeSceneAction(action);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
        m_pendingSceneAction = PendingSceneAction::None;
        ImGui::CloseCurrentPopup();
    }
    if (!m_sceneIoError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", m_sceneIoError.c_str());
    }
    ImGui::EndPopup();
}

void ModelSceneAppState::showRenderSettingsPanel() {
    ImGui::Begin("Render Settings");
    RenderSettings settings = m_scene.renderSettings();
    bool changed = false;

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        float nearPlane = m_cameraNearPlane;
        float farPlane = m_cameraFarPlane;
        ImGui::SetNextItemWidth(-1.0f);
        bool cameraChanged =
            ImGui::DragFloat("Near Plane", &nearPlane, 0.005f, 0.001f, std::max(0.001f, farPlane - 0.001f), "%.3f");
        ImGui::SetNextItemWidth(-1.0f);
        cameraChanged |= ImGui::DragFloat("Far Plane", &farPlane, 1.0f, nearPlane + 0.001f, 100000.0f, "%.1f");
        if (cameraChanged) {
            m_cameraNearPlane = std::clamp(nearPlane, 0.001f, farPlane - 0.001f);
            m_cameraFarPlane = std::clamp(farPlane, m_cameraNearPlane + 0.001f, 100000.0f);
            markSceneDirty();
        }
    }

    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        float timeOfDay = m_scene.timeOfDay();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("Time of Day", &timeOfDay, 0.0f, 1199.0f, "%.0f s")) {
            m_scene.setTimeOfDay(timeOfDay);
            markSceneDirty();
        }
        bool advanceTime = !m_scene.timePaused();
        if (ImGui::Checkbox("Advance Time", &advanceTime)) {
            m_scene.setTimePaused(!advanceTime);
            markSceneDirty();
        }
        float timeScale = m_scene.timeScale();
        ImGui::BeginDisabled(!advanceTime);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::DragFloat("Time Scale", &timeScale, 0.05f, 0.05f, 100.0f, "%.2fx")) {
            m_scene.setTimeScale(std::clamp(timeScale, 0.05f, 100.0f));
            markSceneDirty();
        }
        ImGui::EndDisabled();

        constexpr const char* weatherNames[] = {"Clear", "Rain", "Storm", "Snow"};
        int weather = static_cast<int>(m_scene.weather());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Weather", &weather, weatherNames, static_cast<int>(std::size(weatherNames)))) {
            m_scene.setWeather(static_cast<WeatherType>(weather), m_scene.weatherTransitionInstant());
            markSceneDirty();
        }
        constexpr const char* transitionNames[] = {"Immediate", "Gradual"};
        int transition = m_scene.weatherTransitionInstant() ? 0 : 1;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Weather Transition", &transition, transitionNames,
                         static_cast<int>(std::size(transitionNames)))) {
            m_scene.setWeather(m_scene.weather(), transition == 0);
            markSceneDirty();
        }
    }

    if (ImGui::CollapsingHeader("Graphics Backend")) {
        const RhiBackend currentBackend = m_deps.rhiDevice.backend();
        const app::RhiBackendSettingResult configured = app::loadRhiBackend();
        const RhiBackend selectedBackend = configured.backend.value_or(currentBackend);
        constexpr std::array<RhiBackend, 2> knownBackends = {RhiBackend::OpenGL, RhiBackend::Vulkan};
        std::vector<RhiBackend> availableBackends;
        std::vector<const char*> backendNames;
        int selectedIndex = 0;
        for (const RhiBackend backend : knownBackends) {
            if (!renderer::rhi::isRhiBackendAvailable(backend)) {
                continue;
            }
            if (backend == selectedBackend) {
                selectedIndex = static_cast<int>(availableBackends.size());
            }
            availableBackends.push_back(backend);
            backendNames.push_back(renderer::rhi::rhiBackendDisplayName(backend));
        }
        ImGui::Text("Current: %s", renderer::rhi::rhiBackendDisplayName(currentBackend));
        ImGui::SetNextItemWidth(-1.0f);
        if (!availableBackends.empty() &&
            ImGui::Combo("Backend", &selectedIndex, backendNames.data(), static_cast<int>(backendNames.size()))) {
            if (!app::saveRhiBackend(availableBackends[static_cast<std::size_t>(selectedIndex)])) {
                m_sceneIoError = "Failed to save graphics backend setting";
            }
        }
        ImGui::TextDisabled("Backend changes apply after restart.");
    }

    if (ImGui::CollapsingHeader("Upscaling", ImGuiTreeNodeFlags_DefaultOpen)) {
        const bool fsr31Supported = m_scene.isFsr31Supported();
        bool fsr31Enabled = settings.upscale.type == TemporalUpscalerType::Fsr31;
        ImGui::BeginDisabled(!fsr31Supported);
        if (ImGui::Checkbox("FSR 3.1", &fsr31Enabled)) {
            settings.upscale.type = fsr31Enabled ? TemporalUpscalerType::Fsr31 : TemporalUpscalerType::Native;
            if (fsr31Enabled) {
                settings.upscale.fsr1Enabled = false;
                if (settings.upscale.quality == TemporalUpscaleQuality::Native) {
                    settings.upscale.quality = TemporalUpscaleQuality::Quality;
                }
            }
            changed = true;
        }
        constexpr const char* temporalQualityNames[] = {"Quality", "Balanced", "Performance", "Ultra Performance"};
        int temporalQuality = std::clamp(
            static_cast<int>(settings.upscale.quality) - static_cast<int>(TemporalUpscaleQuality::Quality), 0, 3);
        ImGui::BeginDisabled(!fsr31Enabled);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("FSR 3.1 Quality", &temporalQuality, temporalQualityNames,
                         static_cast<int>(std::size(temporalQualityNames)))) {
            settings.upscale.quality = static_cast<TemporalUpscaleQuality>(
                static_cast<int>(TemporalUpscaleQuality::Quality) + temporalQuality);
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        const bool fsr1Supported = m_scene.isFsr1Supported();
        bool fsr1Enabled = settings.upscale.fsr1Enabled;
        ImGui::BeginDisabled(!fsr1Supported);
        if (ImGui::Checkbox("FSR 1", &fsr1Enabled)) {
            settings.upscale.fsr1Enabled = fsr1Enabled;
            if (fsr1Enabled) {
                settings.upscale.type = TemporalUpscalerType::Native;
            }
            changed = true;
        }
        constexpr const char* fsr1QualityNames[] = {"Ultra Quality", "Quality", "Balanced", "Performance"};
        constexpr std::array<float, 4> fsr1Scales = {0.77f, 0.67f, 0.59f, 0.50f};
        int fsr1Quality = 0;
        float closestScaleDistance = std::abs(settings.upscale.fsr1RenderScale - fsr1Scales[0]);
        for (std::size_t index = 1u; index < fsr1Scales.size(); ++index) {
            const float distance = std::abs(settings.upscale.fsr1RenderScale - fsr1Scales[index]);
            if (distance < closestScaleDistance) {
                closestScaleDistance = distance;
                fsr1Quality = static_cast<int>(index);
            }
        }
        ImGui::BeginDisabled(!fsr1Enabled);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("FSR 1 Quality", &fsr1Quality, fsr1QualityNames,
                         static_cast<int>(std::size(fsr1QualityNames)))) {
            settings.upscale.fsr1RenderScale = fsr1Scales[static_cast<std::size_t>(fsr1Quality)];
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        if (!fsr31Supported) {
            ImGui::TextDisabled("FSR 3.1 requires the Vulkan backend.");
        }
        if (!fsr1Supported) {
            ImGui::TextDisabled("FSR 1 requires the OpenGL backend.");
        }
    }

    if (ImGui::CollapsingHeader("Debug View", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("DebugViewSettings");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= render_settings_imgui::showDeferredDebugView(settings);
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Reflections", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("ReflectionSettings");
        changed |= render_settings_imgui::showReflectionSettings(settings);
        const ReflectionProbeCaptureFrameStats capture = m_scene.reflectionProbeCaptureStats();
        ImGui::SeparatorText("Probe Capture Queue");
        ImGui::Text("Sources %u  Active %u  Building %u  Pending %u", capture.sourceCount, capture.activeProbeCount,
                    capture.buildingProbeCount, capture.pendingWorkItemCount);
        if (capture.currentProbeId.isValid()) {
            ImGui::Text("Probe %u  Work %u/%u  Revision %u -> %u", capture.currentProbeId.value,
                        capture.currentWorkItem, renderer::contracts::kReflectionProbeCaptureWorkItemCount,
                        capture.activeRevision, capture.buildRevision);
        }
        ImGui::PopID();
    }
    showReflectionProbePanel();
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
    if (ImGui::CollapsingHeader("Post Process / Picture", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("PostProcessSettings");
        changed |= render_settings_imgui::showPostProcessSettings(settings);
        ImGui::PopID();
    }

    if (changed) {
        if (m_scene.setRenderSettings(settings)) {
            markSceneDirty();
        }
    }
    if (!m_scene.lastError().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", m_scene.lastError().c_str());
    }
    ImGui::End();
}

void ModelSceneAppState::showReflectionProbePanel() {
    if (!ImGui::CollapsingHeader("Reflection Probes", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::Text("Persistent probes: %zu / %u", m_scene.reflectionProbeCount(),
                renderer::contracts::kReflectionProbeCaptureMaxProbeCount);
    if (ImGui::Button("Add Probe at Camera Target")) {
        const scene::SceneReflectionProbeId id = m_scene.addReflectionProbe(m_cameraTarget);
        if (id != scene::kInvalidSceneReflectionProbeId) {
            m_reflectionProbeEditorId = id;
            markSceneDirty();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Generate Regular Grid")) {
        if (m_scene.generateReflectionProbeGrid(m_reflectionProbeGridSpacing, m_reflectionProbeGridPadding)) {
            m_reflectionProbeEditorId = m_scene.reflectionProbe(0u).id;
            markSceneDirty();
        }
    }
    ImGui::SetNextItemWidth(180.0f);
    ImGui::DragFloat("Grid Spacing", &m_reflectionProbeGridSpacing, 0.1f, 0.1f, 1000.0f, "%.2f m");
    ImGui::SetNextItemWidth(180.0f);
    ImGui::DragFloat("Grid Padding", &m_reflectionProbeGridPadding, 0.1f, 0.0f, 1000.0f, "%.2f m");
    if (m_scene.reflectionProbeCount() == 0u) {
        ImGui::TextDisabled("No capture source is active until a probe is added.");
        return;
    }

    if (m_reflectionProbeEditorId == scene::kInvalidSceneReflectionProbeId) {
        m_reflectionProbeEditorId = m_scene.reflectionProbe(0u).id;
    }
    ImGui::Separator();
    for (std::size_t index = 0u; index < m_scene.reflectionProbeCount(); ++index) {
        const scene::SceneReflectionProbeDocument& probe = m_scene.reflectionProbe(index);
        const bool selected = probe.id == m_reflectionProbeEditorId;
        const std::string label = "Probe " + std::to_string(probe.id);
        if (ImGui::Selectable(label.c_str(), selected)) {
            m_reflectionProbeEditorId = probe.id;
        }
    }
    const auto selectedIndex = [&]() -> std::optional<std::size_t> {
        for (std::size_t index = 0u; index < m_scene.reflectionProbeCount(); ++index) {
            if (m_scene.reflectionProbe(index).id == m_reflectionProbeEditorId) {
                return index;
            }
        }
        return std::nullopt;
    }();
    if (!selectedIndex.has_value()) {
        m_reflectionProbeEditorId = scene::kInvalidSceneReflectionProbeId;
        return;
    }
    scene::SceneReflectionProbeDocument edited = m_scene.reflectionProbe(*selectedIndex);
    ImGui::PushID(static_cast<int>(edited.id));
    bool changed = false;
    changed |= ImGui::InputFloat3("Position", glm::value_ptr(edited.position));
    changed |= ImGui::InputFloat3("Influence Min", glm::value_ptr(edited.influenceMin));
    changed |= ImGui::InputFloat3("Influence Max", glm::value_ptr(edited.influenceMax));
    changed |= ImGui::InputFloat3("Projection Min", glm::value_ptr(edited.boxProjectionMin));
    changed |= ImGui::InputFloat3("Projection Max", glm::value_ptr(edited.boxProjectionMax));
    changed |= ImGui::DragFloat("Blend Distance", &edited.blendDistance, 0.05f, 0.001f, 10000.0f, "%.3f m");
    changed |= ImGui::DragFloat("Exposure Scale", &edited.exposureScale, 0.01f, 0.001f, 100.0f, "%.3f");
    if (changed && m_scene.updateReflectionProbe(edited)) {
        markSceneDirty();
    }
    if (ImGui::Button("Delete Selected Probe")) {
        if (m_scene.removeReflectionProbe(edited.id)) {
            m_reflectionProbeEditorId = scene::kInvalidSceneReflectionProbeId;
            markSceneDirty();
        }
    }
    ImGui::PopID();
}

void ModelSceneAppState::showHierarchyPanel() {
    ImGui::Begin("Scene Hierarchy");
    if (ImGui::Button("Create Empty")) {
        const entt::entity created = m_scene.createEmptyEntity("Empty Entity");
        if (created != entt::null) {
            recordCreatedEntity(created);
        }
    }
    ImGui::Separator();

    m_hierarchyDropPending = false;
    const ImGuiTreeNodeFlags rootFlags =
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    const bool rootOpen = ImGui::TreeNodeEx("Scene##SceneRoot", rootFlags);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_SCENE_ENTITY_ID")) {
            if (payload->DataSize == sizeof(scene::SceneEntityId)) {
                m_hierarchyDropChild = *static_cast<const scene::SceneEntityId*>(payload->Data);
                m_hierarchyDropParent = scene::kInvalidSceneEntityId;
                m_hierarchyDropPending = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (rootOpen) {
        std::vector<entt::entity> roots;
        const auto rootView =
            m_scene.registry().view<scene::SceneEntityIdComponent, scene::NameComponent, ecs::ChildrenComponent>(
                entt::exclude<ecs::ParentComponent>);
        roots.assign(rootView.begin(), rootView.end());
        std::sort(roots.begin(), roots.end(), [this](const entt::entity lhs, const entt::entity rhs) {
            return m_scene.entityId(lhs) < m_scene.entityId(rhs);
        });
        for (const entt::entity entity : roots) {
            showHierarchyEntity(entity);
        }
        ImGui::TreePop();
    }
    if (m_hierarchyDropPending) {
        const entt::entity child = m_scene.findEntity(m_hierarchyDropChild);
        const entt::entity parent = m_scene.findEntity(m_hierarchyDropParent);
        scene::SceneEntityDocument before;
        if (child != entt::null && !m_scene.captureEntityState(child, before)) {
            std::abort();
        }
        if (m_scene.setParent(child, parent)) {
            scene::SceneEntityDocument after;
            if (!m_scene.captureEntityState(child, after)) {
                std::abort();
            }
            m_history.recordEntityState(before, after);
            refreshSceneDirty();
        }
    }
    executePendingEntityAction();
    if (!m_scene.lastError().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", m_scene.lastError().c_str());
    }
    ImGui::End();
}

void ModelSceneAppState::showHierarchyEntity(const entt::entity entity) {
    const scene::SceneEntityId id = m_scene.entityId(entity);
    const auto& name = m_scene.registry().get<scene::NameComponent>(entity).value;
    const auto& children = m_scene.registry().get<ecs::ChildrenComponent>(entity).children;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (entity == m_scene.selectedEntity()) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const std::string label = name + "###SceneEntity" + std::to_string(id);
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked()) {
        m_scene.setSelectedEntity(entity);
    }
    if (ImGui::BeginPopupContextItem()) {
        m_scene.setSelectedEntity(entity);
        if (ImGui::MenuItem("Duplicate")) {
            queueEntityAction(PendingEntityAction::Duplicate, id);
        }
        if (ImGui::MenuItem("Delete")) {
            queueEntityAction(PendingEntityAction::Delete, id);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Focus")) {
            queueEntityAction(PendingEntityAction::Focus, id);
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("MODEL_SCENE_ENTITY_ID", &id, sizeof(id));
        ImGui::TextUnformatted(name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_SCENE_ENTITY_ID")) {
            if (payload->DataSize == sizeof(scene::SceneEntityId)) {
                m_hierarchyDropChild = *static_cast<const scene::SceneEntityId*>(payload->Data);
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
    const entt::entity selected = m_scene.selectedEntity();
    if (selected == entt::null || !m_scene.registry().valid(selected)) {
        ImGui::TextUnformatted("No entity selected");
        ImGui::End();
        return;
    }
    auto* name = m_scene.registry().try_get<scene::NameComponent>(selected);
    auto* transform = m_scene.registry().try_get<ecs::LocalTransformComponent>(selected);
    if (name != nullptr) {
        const scene::SceneEntityId selectedId = m_scene.entityId(selected);
        if (m_entityNameEditorId != selectedId) {
            std::fill(m_entityNameBuffer.begin(), m_entityNameBuffer.end(), '\0');
            const std::size_t copyLength = std::min(name->value.size(), m_entityNameBuffer.size() - 1u);
            std::copy_n(name->value.begin(), copyLength, m_entityNameBuffer.begin());
            m_entityNameEditorId = selectedId;
        }
        const float nameLabelWidth = ImGui::CalcTextSize("Name").x + ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SetNextItemWidth(-nameLabelWidth);
        const bool submitted =
            ImGui::InputText("Name", m_entityNameBuffer.data(), m_entityNameBuffer.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool finishedEditing = ImGui::IsItemDeactivatedAfterEdit();
        if (submitted || finishedEditing) {
            scene::SceneEntityDocument before;
            if (!m_scene.captureEntityState(selected, before)) {
                std::abort();
            }
            if (m_scene.renameEntity(selected, m_entityNameBuffer.data())) {
                scene::SceneEntityDocument after;
                if (!m_scene.captureEntityState(selected, after)) {
                    std::abort();
                }
                m_history.recordEntityState(before, after);
                refreshSceneDirty();
                std::fill(m_entityNameBuffer.begin(), m_entityNameBuffer.end(), '\0');
                const std::size_t copyLength = std::min(name->value.size(), m_entityNameBuffer.size() - 1u);
                std::copy_n(name->value.begin(), copyLength, m_entityNameBuffer.begin());
            }
        }
        ImGui::TextDisabled("Entity ID: %llu", static_cast<unsigned long long>(m_scene.entityId(selected)));
        ImGui::Separator();
    }
    if (transform != nullptr) {
        ecs::LocalTransformComponent editedTransform = *transform;
        scene::SceneEntityDocument beforeFrame;
        if (!m_scene.captureEntityState(selected, beforeFrame)) {
            std::abort();
        }
        bool changed = false;
        bool activated = false;
        bool deactivated = false;
        changed |= ImGui::DragFloat3("Position", glm::value_ptr(editedTransform.localPosition), 0.02f);
        activated |= ImGui::IsItemActivated();
        deactivated |= ImGui::IsItemDeactivated();
        changed |= ImGui::DragFloat3("Rotation", glm::value_ptr(editedTransform.localRotation), 0.25f);
        activated |= ImGui::IsItemActivated();
        deactivated |= ImGui::IsItemDeactivated();
        changed |= ImGui::DragFloat3("Scale", glm::value_ptr(editedTransform.localScale), 0.01f, 0.001f, 1000.0f);
        activated |= ImGui::IsItemActivated();
        deactivated |= ImGui::IsItemDeactivated();
        if (activated) {
            beginTransformCommand(selected, beforeFrame, false);
        }
        if (changed) {
            scene::SceneEntityDocument editedState = beforeFrame;
            editedState.transform.position = editedTransform.localPosition;
            editedState.transform.rotation = editedTransform.localRotation;
            editedState.transform.scale = editedTransform.localScale;
            if (m_scene.applyEntityState(editedState)) {
                m_sceneDirty = true;
            }
        }
        if (deactivated && m_transformCommandActive && !m_transformCommandFromGizmo) {
            finishTransformCommand();
        }
    }
    const auto* pickable = m_scene.registry().try_get<scene::PickableComponent>(selected);
    if (pickable != nullptr && ImGui::CollapsingHeader("Local Bounds")) {
        ImGui::Text("Min: %.3f, %.3f, %.3f", pickable->localBoundsMin.x, pickable->localBoundsMin.y,
                    pickable->localBoundsMin.z);
        ImGui::Text("Max: %.3f, %.3f, %.3f", pickable->localBoundsMax.x, pickable->localBoundsMax.y,
                    pickable->localBoundsMax.z);
    }
    if (!m_scene.lastError().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", m_scene.lastError().c_str());
    }
    ImGui::Separator();
    if (ImGui::Button("Duplicate Entity")) {
        duplicateSelectedEntity();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Entity")) {
        deleteSelectedEntity();
    }
    ImGui::End();
}

void ModelSceneAppState::showAssetsPanel() {
    ImGui::Begin("Assets");
    const ImGuiStyle& style = ImGui::GetStyle();
    const float browseButtonWidth = ImGui::CalcTextSize("Browse...").x + style.FramePadding.x * 2.0f;
    const float importButtonWidth = ImGui::CalcTextSize("Import Model").x + style.FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(browseButtonWidth + importButtonWidth + style.ItemSpacing.x * 2.0f));
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
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", m_importDialogError.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!m_sceneIoError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", m_sceneIoError.c_str());
    }
    ImGui::Separator();
    for (size_t index = 0u; index < m_scene.assetCount(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::SmallButton("+")) {
            const entt::entity created = m_scene.createAssetInstance(m_scene.assetId(index));
            if (created != entt::null) {
                recordCreatedEntity(created);
                m_entityNameEditorId = scene::kInvalidSceneEntityId;
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Create model instance");
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(m_scene.assetName(index).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("glTF / GLB");
        ImGui::TextDisabled("%s", m_scene.assetPath(index).c_str());
        ImGui::PopID();
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
    const nfdresult_t result = NFD_OpenDialog(&selectedPath, filters, std::size(filters), nullptr);
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
    const std::size_t assetCountBeforeImport = m_scene.assetCount();
    const entt::entity imported = m_scene.importModel(path);
    if (imported == entt::null) {
        m_importDialogError = m_scene.lastError();
        return;
    }
    m_scene.setSelectedEntity(imported);
    recordCreatedEntity(imported);
    if (m_scene.assetCount() != assetCountBeforeImport) {
        markSceneDirty();
    }
}

void ModelSceneAppState::showViewportPanel() {
    if (m_transformCommandActive && m_transformCommandFromGizmo && !ImGuizmo::IsUsing()) {
        finishTransformCommand();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Scene Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
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
    const bool flipTextureVertically = m_deps.rhiDevice.backend() == RhiBackend::OpenGL;
    const ImVec2 uv0 = flipTextureVertically ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
    const ImVec2 uv1 = flipTextureVertically ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
    ImGui::Image(static_cast<ImTextureID>(m_scene.viewportTextureId()), available, uv0, uv1);
    const bool viewportImageHovered = ImGui::IsItemHovered();
    const bool gizmoToolbarHovered = showGizmoToolbar();
    m_viewportHovered = viewportImageHovered && !gizmoToolbarHovered;

    const float aspect = available.x / available.y;
    m_view = glm::lookAt(cameraPosition(), m_cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    m_projection = glm::perspective(glm::radians(55.0f), aspect, m_cameraNearPlane, m_cameraFarPlane);

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(m_viewportPosition.x, m_viewportPosition.y, available.x, available.y);
    const entt::entity selected = m_scene.selectedEntity();
    if (selected != entt::null && m_scene.registry().valid(selected)) {
        auto* world = m_scene.registry().try_get<ecs::WorldTransformComponent>(selected);
        if (world != nullptr) {
            glm::mat4 manipulated = world->worldMatrix;
            if (ImGuizmo::Manipulate(
                    glm::value_ptr(m_view), glm::value_ptr(m_projection), gizmoOperation(m_gizmoOperation),
                    m_gizmoMode == 0 ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(manipulated))) {
                if (!m_transformCommandActive) {
                    scene::SceneEntityDocument before;
                    if (!m_scene.captureEntityState(selected, before)) {
                        std::abort();
                    }
                    beginTransformCommand(selected, before, true);
                }
                if (m_scene.setWorldTransform(selected, manipulated)) {
                    m_sceneDirty = true;
                }
            }
        }
    }
    if (m_transformCommandActive && m_transformCommandFromGizmo && !ImGuizmo::IsUsing()) {
        finishTransformCommand();
    }
    if (m_viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoToolbarHovered &&
        !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
        selectFromViewport(ImGui::GetMousePos());
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

bool ModelSceneAppState::showGizmoToolbar() {
    ImGui::SetCursorScreenPos(ImVec2(m_viewportPosition.x + 8.0f, m_viewportPosition.y + 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.060f, 0.070f, 0.94f));
    ImGui::BeginChild("##GizmoToolbar", ImVec2(196.0f, 38.0f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    bool hovered = ImGui::IsWindowHovered();
    const auto operationButton = [&](const char* label, const int operation, const char* tooltip) {
        const bool active = m_gizmoOperation == operation;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.68f, 1.0f));
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

    const auto modeButton = [&](const char* label, const int mode, const char* tooltip) {
        const bool disabled = m_gizmoOperation == 2 && mode == 1;
        const bool active = m_gizmoMode == mode;
        ImGui::BeginDisabled(disabled);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.68f, 1.0f));
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
    const float normalizedX = (mousePosition.x - m_viewportPosition.x) / m_viewportSize.x;
    const float normalizedY = (mousePosition.y - m_viewportPosition.y) / m_viewportSize.y;
    const float clipX = normalizedX * 2.0f - 1.0f;
    const float clipY = 1.0f - normalizedY * 2.0f;
    const glm::mat4 inverseViewProjection = glm::inverse(m_projection * m_view);
    glm::vec4 nearPoint = inverseViewProjection * glm::vec4(clipX, clipY, -1.0f, 1.0f);
    glm::vec4 farPoint = inverseViewProjection * glm::vec4(clipX, clipY, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    const glm::vec3 origin = glm::vec3(nearPoint);
    const glm::vec3 direction = glm::normalize(glm::vec3(farPoint - nearPoint));
    m_scene.setSelectedEntity(m_scene.pick(origin, direction));
}

void ModelSceneAppState::render(const double frameTime) {
    if (!m_initialized || m_returnRequested) {
        return;
    }
    if (m_validationActive) {
        const app::validation::ValidationFrame* validationFrame = m_deps.validationRun.currentFrame();
        if (validationFrame == nullptr) {
            m_deps.validationRun.fail(app::validation::ValidationRunError::InvalidState,
                                      "model validation has no active frame");
            return;
        }
        const AppLaunchOptions& options = m_deps.validationRun.options();
        if (!m_scene.ensureViewport(options.validationWidth, options.validationHeight)) {
            m_deps.validationRun.fail(app::validation::ValidationRunError::RenderFailed, m_scene.lastError());
            return;
        }

        Camera camera;
        if (!camera.setViewPose(glm::vec3(validationFrame->cameraPose.position),
                                glm::vec3(validationFrame->cameraPose.forward),
                                glm::vec3(validationFrame->cameraPose.up),
                                static_cast<float>(validationFrame->cameraPose.verticalFovDegrees))) {
            m_deps.validationRun.fail(app::validation::ValidationRunError::CameraPoseConversionFailed,
                                      "model Camera Path pose cannot be represented by the float render camera");
            return;
        }
        const float aspect = static_cast<float>(options.validationWidth) / static_cast<float>(options.validationHeight);
        const glm::mat4 view = camera.getViewMatrix();
        const glm::mat4 projection =
            glm::perspective(glm::radians(camera.getFOV()), aspect, m_cameraNearPlane, m_cameraFarPlane);
        const RenderFrameClock clock{validationFrame->sequenceFrameIndex, validationFrame->deltaTimeSeconds,
                                     validationFrame->renderTimeSeconds, validationFrame->renderTimeSeconds};
        if (!m_scene.renderViewport(view, projection, camera.getPosition(), m_cameraNearPlane, m_cameraFarPlane,
                                    camera.getFOV(), clock)) {
            m_deps.validationRun.fail(app::validation::ValidationRunError::RenderFailed, m_scene.lastError());
            return;
        }

        bool captureSucceeded = true;
        std::string captureDetail;
        if (validationFrame->captureAfterRender) {
            renderer::capture::TextureCaptureRequest request;
            request.sourceTexture = m_scene.captureTextureHandle();
            request.sourceState = RhiResourceState::ShaderRead;
            request.sourceFormat = m_scene.captureTextureFormat();
            request.width = options.validationWidth;
            request.height = options.validationHeight;
            request.origin = m_deps.rhiDevice.backend() == RhiBackend::OpenGL
                                 ? renderer::capture::TextureCaptureOrigin::BottomLeft
                                 : renderer::capture::TextureCaptureOrigin::TopLeft;
            request.outputPath = options.validationCapturePath;
            const renderer::capture::TextureCaptureResult result =
                renderer::capture::captureTextureToPng(m_deps.rhiDevice, m_deps.commandListPool, request);
            captureSucceeded = result.succeeded();
            if (!captureSucceeded) {
                captureDetail =
                    std::string(renderer::capture::textureCaptureErrorStableId(result.error)) + ":" + result.detail;
            }
        }
        static_cast<void>(m_deps.validationRun.completeFrame(captureSucceeded, std::move(captureDetail)));
        return;
    }
    const Window::FramebufferSize framebufferSize = m_deps.window.getFramebufferSize();
    if (framebufferSize.width <= 0 || framebufferSize.height <= 0) {
        return;
    }
    if (!m_deps.rhiDevice.resizeSwapchain(static_cast<uint32_t>(framebufferSize.width),
                                          static_cast<uint32_t>(framebufferSize.height))) {
        MECRAFT_LOG_STREAM(std::cerr << "[ModelSceneAppState] Failed to resize swapchain\n");
        return;
    }
    const RhiFrameAcquireResult frame = m_deps.rhiDevice.acquireFrame();
    if (frame.status == RhiFrameStatus::Minimized || frame.status == RhiFrameStatus::OutOfDate) {
        return;
    }
    if (frame.status != RhiFrameStatus::Success && frame.status != RhiFrameStatus::Suboptimal) {
        MECRAFT_LOG_STREAM(std::cerr << "[ModelSceneAppState] Failed to acquire frame\n");
        return;
    }
    if (!m_imguiRenderer.beginFrame(static_cast<int>(frame.width), static_cast<int>(frame.height))) {
        std::abort();
    }
    ImGuizmo::BeginFrame();
    buildEditorUi();
    if (m_scene.viewportWidth() != 0u &&
        !m_scene.renderViewport(m_view, m_projection, cameraPosition(), m_cameraNearPlane, m_cameraFarPlane, 55.0f,
                                RenderFrameClock{Time::getFrameIndex(), static_cast<float>(frameTime),
                                                 Time::getGameTime(), Time::getRawTime()})) {
        std::abort();
    }
    RhiCommandList* commandList = m_deps.commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr || !commandList->begin({"ModelScene.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    if (!m_imguiRenderer.prepareDrawData(*commandList)) {
        std::abort();
    }
    commandList->textureBarrier(
        {m_deps.rhiDevice.currentSwapchainColorTexture(), RhiResourceState::Present, RhiResourceState::RenderTarget});
    RhiColorAttachment colorAttachment;
    colorAttachment.view = m_deps.rhiDevice.currentSwapchainColorView();
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.035f;
    colorAttachment.clearColor[1] = 0.040f;
    colorAttachment.clearColor[2] = 0.045f;
    colorAttachment.clearColor[3] = 1.0f;
    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = m_deps.rhiDevice.currentSwapchainDepthStencilView();
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
    commandList->textureBarrier(
        {m_deps.rhiDevice.currentSwapchainColorTexture(), RhiResourceState::RenderTarget, RhiResourceState::Present});
    if (!commandList->end()) {
        std::abort();
    }
    RhiCommandList* submitted[] = {commandList};
    if (!m_deps.rhiDevice.submit({"ModelScene.Submit", submitted, 1u, RhiQueueType::Graphics})) {
        std::abort();
    }
    const RhiFrameStatus presentStatus =
        m_deps.rhiDevice.presentFrame({frame.frameIndex, frame.imageIndex, Time::getFrameIndex()});
    if (presentStatus != RhiFrameStatus::Success && presentStatus != RhiFrameStatus::Suboptimal &&
        presentStatus != RhiFrameStatus::OutOfDate && presentStatus != RhiFrameStatus::Minimized) {
        MECRAFT_LOG_STREAM(std::cerr << "[ModelSceneAppState] Failed to present frame\n");
    }
}
