#include "ModelSceneRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <utility>

#include <glm/gtc/matrix_inverse.hpp>

#include "ModelSceneComponents.h"
#include "ModelSceneDeferredRenderer.h"
#include "ecs/components/TransformComponents.h"
#include "renderer/core/FrameContext.h"
#include "renderer/renderers/StaticMeshRenderer.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiCommandListPool.h"
#include "renderer/rhi/RhiDevice.h"
#include "resource/ResourceMgr.h"
#include "ui/imgui/ImGuiRhiRenderer.h"

namespace {

[[nodiscard]] bool intersectLocalBounds(const glm::vec3& origin,
                                        const glm::vec3& direction,
                                        const glm::vec3& boundsMin,
                                        const glm::vec3& boundsMax,
                                        float& distance) {
    float nearDistance = 0.0f;
    float farDistance = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-8f) {
            if (origin[axis] < boundsMin[axis] || origin[axis] > boundsMax[axis]) {
                return false;
            }
            continue;
        }
        const float inverseDirection = 1.0f / direction[axis];
        float first = (boundsMin[axis] - origin[axis]) * inverseDirection;
        float second = (boundsMax[axis] - origin[axis]) * inverseDirection;
        if (first > second) {
            std::swap(first, second);
        }
        nearDistance = std::max(nearDistance, first);
        farDistance = std::min(farDistance, second);
        if (nearDistance > farDistance) {
            return false;
        }
    }
    distance = nearDistance;
    return farDistance >= 0.0f;
}
} // namespace

ModelSceneRuntime::~ModelSceneRuntime() {
    shutdown();
}

ModelSceneRuntime::ModelSceneRuntime() = default;

bool ModelSceneRuntime::init(ResourceMgr& resourceMgr,
                             RhiDevice& rhiDevice,
                             RhiCommandListPool& commandListPool,
                             ImGuiRhiRenderer& imguiRenderer) {
    shutdown();
    m_resourceMgr = &resourceMgr;

    if (importModel("assets/models/showcase/DamagedHelmet.glb") == entt::null) {
        const std::string error = m_lastError;
        shutdown();
        m_lastError = error;
        return false;
    }
    m_deferredRenderer = std::make_unique<ModelSceneDeferredRenderer>();
    if (!m_deferredRenderer->init(
            resourceMgr, rhiDevice, commandListPool, imguiRenderer, *this)) {
        const std::string error = m_deferredRenderer->lastError();
        shutdown();
        m_lastError = error;
        return false;
    }
    m_lastError.clear();
    return true;
}

entt::entity ModelSceneRuntime::instantiateAsset(
    const uint32_t assetIndex,
    const std::string& instanceName) {
    if (assetIndex >= m_assets.size()) {
        setError("cannot instantiate an invalid model scene asset index");
        return entt::null;
    }
    const MeshAsset& asset = m_assets[assetIndex];
    const glm::vec3 center = (asset.boundsMin + asset.boundsMax) * 0.5f;
    const glm::vec3 extent = asset.boundsMax - asset.boundsMin;
    const float scale = 2.4f / std::max({extent.x, extent.y, extent.z});
    const entt::entity entity = m_registry.create();
    m_registry.emplace<scene::NameComponent>(
        entity, scene::NameComponent{instanceName});
    m_registry.emplace<scene::StaticMeshComponent>(
        entity, scene::StaticMeshComponent{assetIndex});
    ecs::LocalTransformComponent transform;
    transform.localPosition = -center * scale;
    transform.localScale = glm::vec3(scale);
    m_registry.emplace<ecs::LocalTransformComponent>(entity, transform);
    const glm::mat4 world = transform.toMatrix();
    m_registry.emplace<ecs::WorldTransformComponent>(
        entity, ecs::WorldTransformComponent{world});
    m_registry.emplace<scene::PreviousWorldTransformComponent>(
        entity, scene::PreviousWorldTransformComponent{world});
    m_registry.emplace<scene::PickableComponent>(
        entity, scene::PickableComponent{asset.boundsMin, asset.boundsMax});
    m_selectedEntity = entity;
    return entity;
}

entt::entity ModelSceneRuntime::importModel(const std::string& path) {
    if (m_resourceMgr == nullptr || path.empty()) {
        setError("model import requires a non-empty asset path");
        return entt::null;
    }
    const std::filesystem::path filesystemPath(path);
    const std::string name = filesystemPath.stem().string();
    if (name.empty()) {
        setError("model import path must contain a file name");
        return entt::null;
    }
    const auto existing = std::find_if(
        m_assets.begin(), m_assets.end(),
        [&path](const MeshAsset& asset) { return asset.path == path; });
    uint32_t assetIndex = 0u;
    if (existing == m_assets.end()) {
        if (!loadMeshAsset(*m_resourceMgr, name, path, assetIndex)) {
            return entt::null;
        }
    } else {
        assetIndex = static_cast<uint32_t>(
            std::distance(m_assets.begin(), existing));
    }
    m_lastError.clear();
    return instantiateAsset(assetIndex, name);
}

void ModelSceneRuntime::destroyEntity(const entt::entity entity) {
    if (!m_registry.valid(entity)) {
        return;
    }
    m_registry.destroy(entity);
    if (m_selectedEntity == entity) {
        m_selectedEntity = entt::null;
    }
}

const std::string& ModelSceneRuntime::assetName(const size_t index) const {
    if (index >= m_assets.size()) {
        std::abort();
    }
    return m_assets[index].name;
}

const std::string& ModelSceneRuntime::assetPath(const size_t index) const {
    if (index >= m_assets.size()) {
        std::abort();
    }
    return m_assets[index].path;
}

void ModelSceneRuntime::shutdown() {
    if (m_deferredRenderer) {
        m_deferredRenderer->shutdown();
        m_deferredRenderer.reset();
    }
    for (MeshAsset& asset : m_assets) {
        if (asset.renderer) {
            asset.renderer->shutdown();
        }
    }
    m_assets.clear();
    m_registry.clear();
    m_selectedEntity = entt::null;
    m_resourceMgr = nullptr;
}

bool ModelSceneRuntime::loadMeshAsset(ResourceMgr& resourceMgr,
                                      const std::string& name,
                                      const std::string& path,
                                      uint32_t& assetIndex) {
    auto renderer = std::make_unique<StaticMeshRenderer>();
    if (!renderer->init(resourceMgr, path)) {
        setError(renderer->lastError());
        return false;
    }
    MeshAsset asset;
    asset.name = name;
    asset.path = path;
    renderer->assetBounds(asset.boundsMin, asset.boundsMax);
    asset.renderer = std::move(renderer);
    assetIndex = static_cast<uint32_t>(m_assets.size());
    m_assets.push_back(std::move(asset));
    return true;
}

bool ModelSceneRuntime::ensureViewport(const uint32_t width,
                                       const uint32_t height) {
    if (!m_deferredRenderer || width == 0u || height == 0u) {
        return false;
    }
    if (!m_deferredRenderer->ensureViewport(width, height)) {
        setError(m_deferredRenderer->lastError());
        return false;
    }
    m_lastError.clear();
    return true;
}

bool ModelSceneRuntime::renderViewport(const glm::mat4& view,
                                       const glm::mat4& projection,
                                       const glm::vec3& cameraPosition,
                                       const float deltaTime) {
    if (!m_deferredRenderer ||
        !m_deferredRenderer->render(
            view, projection, cameraPosition, deltaTime)) {
        if (m_deferredRenderer) {
            setError(m_deferredRenderer->lastError());
        }
        return false;
    }
    m_lastError.clear();
    return true;
}

bool ModelSceneRuntime::prepareGBuffer(
    RhiCommandList& commandList,
    const FrameContext& context) {
    for (MeshAsset& asset : m_assets) {
        asset.renderer->prepareStandaloneFrame();
        if (!asset.renderer->prepareGBuffer(
                commandList, context.camera.viewProj,
                context.previousViewProjWithCurrentJitter)) {
            return false;
        }
    }
    return true;
}

void ModelSceneRuntime::renderToGBuffer(
    RhiCommandList& commandList,
    const glm::mat4& viewProjection,
    const glm::mat4& previousViewProjection) {
    (void)viewProjection;
    (void)previousViewProjection;
    const auto view = m_registry.view<
        scene::StaticMeshComponent,
        ecs::WorldTransformComponent,
        scene::PreviousWorldTransformComponent>();
    for (const entt::entity entity : view) {
        const auto& mesh = view.get<scene::StaticMeshComponent>(entity);
        const auto& world = view.get<ecs::WorldTransformComponent>(entity);
        const auto& previous =
            view.get<scene::PreviousWorldTransformComponent>(entity);
        if (mesh.assetIndex >= m_assets.size()) {
            std::abort();
        }
        StaticMeshRenderer& renderer = *m_assets[mesh.assetIndex].renderer;
        renderer.setInstanceTransform(world.worldMatrix, previous.worldMatrix);
        renderer.renderToGBuffer(commandList);
    }
}

void ModelSceneRuntime::renderToShadowMap(
    RhiCommandList& commandList,
    const glm::mat4& shadowViewProjection) {
    const auto view = m_registry.view<
        scene::StaticMeshComponent,
        ecs::WorldTransformComponent,
        scene::PreviousWorldTransformComponent>();
    for (const entt::entity entity : view) {
        const auto& mesh = view.get<scene::StaticMeshComponent>(entity);
        if (mesh.assetIndex >= m_assets.size()) {
            std::abort();
        }
        StaticMeshRenderer& renderer = *m_assets[mesh.assetIndex].renderer;
        renderer.setInstanceTransform(
            view.get<ecs::WorldTransformComponent>(entity).worldMatrix,
            view.get<scene::PreviousWorldTransformComponent>(entity).worldMatrix);
        renderer.renderToShadowMap(commandList, shadowViewProjection);
    }
}

uint64_t ModelSceneRuntime::viewportTextureId() const {
    if (!m_deferredRenderer) {
        std::abort();
    }
    return m_deferredRenderer->viewportTextureId();
}

uint32_t ModelSceneRuntime::viewportWidth() const {
    if (!m_deferredRenderer) {
        std::abort();
    }
    return m_deferredRenderer->viewportWidth();
}

uint32_t ModelSceneRuntime::viewportHeight() const {
    if (!m_deferredRenderer) {
        std::abort();
    }
    return m_deferredRenderer->viewportHeight();
}

void ModelSceneRuntime::setTimeOfDay(const float timeOfDaySeconds) {
    if (!m_deferredRenderer) {
        std::abort();
    }
    m_deferredRenderer->setTimeOfDay(timeOfDaySeconds);
}

float ModelSceneRuntime::timeOfDay() const {
    if (!m_deferredRenderer) {
        std::abort();
    }
    return m_deferredRenderer->timeOfDay();
}

entt::entity ModelSceneRuntime::pick(const glm::vec3& rayOrigin,
                                     const glm::vec3& rayDirection) const {
    entt::entity nearestEntity = entt::null;
    float nearestDistance = std::numeric_limits<float>::max();
    const auto view = m_registry.view<
        scene::PickableComponent, ecs::WorldTransformComponent>();
    for (const entt::entity entity : view) {
        const auto& bounds = view.get<scene::PickableComponent>(entity);
        const glm::mat4& world =
            view.get<ecs::WorldTransformComponent>(entity).worldMatrix;
        const float determinant = glm::determinant(glm::mat3(world));
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f) {
            continue;
        }
        const glm::mat4 inverseWorld = glm::inverse(world);
        const glm::vec3 localOrigin = glm::vec3(
            inverseWorld * glm::vec4(rayOrigin, 1.0f));
        const glm::vec3 localDirection = glm::vec3(
            inverseWorld * glm::vec4(rayDirection, 0.0f));
        float distance = 0.0f;
        if (intersectLocalBounds(localOrigin, localDirection,
                                 bounds.localBoundsMin, bounds.localBoundsMax,
                                 distance) && distance < nearestDistance) {
            nearestDistance = distance;
            nearestEntity = entity;
        }
    }
    return nearestEntity;
}

void ModelSceneRuntime::syncTransforms() {
    const auto view = m_registry.view<
        ecs::LocalTransformComponent,
        ecs::WorldTransformComponent,
        scene::PreviousWorldTransformComponent>();
    for (const entt::entity entity : view) {
        auto& world = view.get<ecs::WorldTransformComponent>(entity);
        auto& previous =
            view.get<scene::PreviousWorldTransformComponent>(entity);
        previous.worldMatrix = world.worldMatrix;
        world.worldMatrix =
            view.get<ecs::LocalTransformComponent>(entity).toMatrix();
    }
}

void ModelSceneRuntime::setError(std::string message) {
    m_lastError = std::move(message);
}
