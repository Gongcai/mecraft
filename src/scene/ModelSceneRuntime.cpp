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
#include "ecs/components/TransformComponents.h"
#include "renderer/renderers/StaticMeshRenderer.h"
#include "renderer/rhi/RhiCommandList.h"
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
                             ImGuiRhiRenderer& imguiRenderer) {
    shutdown();
    m_rhiDevice = &rhiDevice;
    m_imguiRenderer = &imguiRenderer;
    m_resourceMgr = &resourceMgr;
    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Linear;
    samplerDesc.magFilter = RhiFilter::Linear;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    m_viewportSampler = m_rhiDevice->createSampler(samplerDesc);
    if (!m_viewportSampler.isValid()) {
        setError("failed to create model scene viewport sampler");
        shutdown();
        return false;
    }

    if (importModel("assets/models/showcase/DamagedHelmet.glb") == entt::null) {
        const std::string error = m_lastError;
        shutdown();
        m_lastError = error;
        return false;
    }
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
    destroyViewport();
    for (MeshAsset& asset : m_assets) {
        if (asset.renderer) {
            asset.renderer->shutdown();
        }
    }
    m_assets.clear();
    m_registry.clear();
    if (m_rhiDevice != nullptr && m_viewportSampler.isValid()) {
        m_rhiDevice->destroySampler(m_viewportSampler);
    }
    m_viewportSampler = {};
    m_selectedEntity = entt::null;
    m_imguiRenderer = nullptr;
    m_resourceMgr = nullptr;
    m_rhiDevice = nullptr;
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
    if (m_rhiDevice == nullptr || m_imguiRenderer == nullptr ||
        width == 0u || height == 0u) {
        return false;
    }
    if (width == m_viewportWidth && height == m_viewportHeight &&
        m_colorTexture.isValid() && m_depthTexture.isValid()) {
        return true;
    }
    destroyViewport();
    RhiTextureDesc colorDesc;
    colorDesc.debugName = "ModelScene.ViewportColor";
    colorDesc.format = RhiTextureFormat::Rgba8Unorm;
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) |
                      rhiFlag(RhiTextureUsage::Sampled);
    m_colorTexture = m_rhiDevice->createTexture(colorDesc, nullptr);
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = m_colorTexture;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.format = colorDesc.format;
    m_colorView = m_rhiDevice->createTextureView(viewDesc);
    RhiTextureDesc depthDesc;
    depthDesc.debugName = "ModelScene.ViewportDepth";
    depthDesc.format = RhiTextureFormat::Depth32Float;
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.usage = rhiFlag(RhiTextureUsage::DepthStencilAttachment);
    m_depthTexture = m_rhiDevice->createTexture(depthDesc, nullptr);
    viewDesc.texture = m_depthTexture;
    viewDesc.format = depthDesc.format;
    m_depthView = m_rhiDevice->createTextureView(viewDesc);
    if (!m_colorTexture.isValid() || !m_colorView.isValid() ||
        !m_depthTexture.isValid() || !m_depthView.isValid()) {
        setError("failed to create model scene viewport textures");
        destroyViewport();
        return false;
    }
    m_viewportTextureId = static_cast<uint64_t>(
        m_imguiRenderer->registerTexture(m_colorView, m_viewportSampler));
    if (m_viewportTextureId == 0u) {
        setError("failed to register model scene viewport texture with ImGui");
        destroyViewport();
        return false;
    }
    m_viewportWidth = width;
    m_viewportHeight = height;
    m_colorState = RhiResourceState::Undefined;
    m_depthState = RhiResourceState::Undefined;
    return true;
}

void ModelSceneRuntime::destroyViewport() {
    if (m_imguiRenderer != nullptr && m_viewportTextureId != 0u) {
        m_imguiRenderer->unregisterTexture(
            static_cast<ImTextureID>(m_viewportTextureId));
    }
    m_viewportTextureId = 0u;
    if (m_rhiDevice != nullptr) {
        if (m_depthView.isValid()) m_rhiDevice->destroyTextureView(m_depthView);
        if (m_colorView.isValid()) m_rhiDevice->destroyTextureView(m_colorView);
        if (m_depthTexture.isValid()) m_rhiDevice->destroyTexture(m_depthTexture);
        if (m_colorTexture.isValid()) m_rhiDevice->destroyTexture(m_colorTexture);
    }
    m_depthView = {};
    m_colorView = {};
    m_depthTexture = {};
    m_colorTexture = {};
    m_viewportWidth = 0u;
    m_viewportHeight = 0u;
    m_colorState = RhiResourceState::Undefined;
    m_depthState = RhiResourceState::Undefined;
}

bool ModelSceneRuntime::recordViewport(RhiCommandList& commandList,
                                       const glm::mat4& viewProjection) {
    if (!m_colorTexture.isValid() || !m_depthTexture.isValid()) {
        return false;
    }
    for (MeshAsset& asset : m_assets) {
        asset.renderer->prepareStandaloneFrame();
        if (!asset.renderer->prepareGBuffer(commandList)) {
            return false;
        }
    }
    commandList.textureBarrier(
        {m_colorTexture, m_colorState, RhiResourceState::RenderTarget});
    commandList.textureBarrier(
        {m_depthTexture, m_depthState, RhiResourceState::DepthWrite});
    m_colorState = RhiResourceState::RenderTarget;
    m_depthState = RhiResourceState::DepthWrite;
    RhiColorAttachment colorAttachment;
    colorAttachment.view = m_colorView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.055f;
    colorAttachment.clearColor[1] = 0.065f;
    colorAttachment.clearColor[2] = 0.075f;
    colorAttachment.clearColor[3] = 1.0f;
    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = m_depthView;
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "ModelScene.Viewport";
    renderingInfo.renderArea = {0, 0, m_viewportWidth, m_viewportHeight};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;
    commandList.beginRendering(renderingInfo);
    commandList.setViewport({
        0.0f, 0.0f, static_cast<float>(m_viewportWidth),
        static_cast<float>(m_viewportHeight), 0.0f, 1.0f});
    commandList.setScissor({0, 0, m_viewportWidth, m_viewportHeight});
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
            commandList.endRendering();
            setError("model scene entity references an invalid mesh asset");
            return false;
        }
        StaticMeshRenderer& renderer = *m_assets[mesh.assetIndex].renderer;
        renderer.setInstanceTransform(world.worldMatrix, previous.worldMatrix);
        renderer.renderPreview(commandList, viewProjection);
    }
    commandList.endRendering();
    commandList.textureBarrier(
        {m_colorTexture, RhiResourceState::RenderTarget,
         RhiResourceState::ShaderRead});
    m_colorState = RhiResourceState::ShaderRead;
    return true;
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
