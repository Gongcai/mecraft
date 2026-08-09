#include "ModelSceneRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <unordered_set>
#include <utility>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ModelSceneComponents.h"
#include "ModelSceneDeferredRenderer.h"
#include "ModelSceneSerializer.h"
#include "ecs/components/TransformComponents.h"
#include "renderer/core/FrameContext.h"
#include "renderer/debug/RenderDebugService.h"
#include "renderer/renderers/StaticMeshRenderer.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiCommandListPool.h"
#include "renderer/rhi/RhiDevice.h"
#include "resource/ResourceMgr.h"
#include "ui/imgui/ImGuiRhiRenderer.h"

namespace {

[[nodiscard]] bool intersectLocalBounds(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& boundsMin,
                                        const glm::vec3& boundsMax, float& distance) {
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

[[nodiscard]] bool syncRegistryTransforms(entt::registry& registry) {
    const auto view =
        registry
            .view<ecs::LocalTransformComponent, ecs::WorldTransformComponent, scene::PreviousWorldTransformComponent>();
    for (const entt::entity entity : view) {
        auto& world = view.get<ecs::WorldTransformComponent>(entity);
        auto& previous = view.get<scene::PreviousWorldTransformComponent>(entity);
        previous.worldMatrix = world.worldMatrix;
    }

    std::vector<entt::entity> queue;
    const auto roots =
        registry.view<ecs::LocalTransformComponent, ecs::WorldTransformComponent, ecs::ChildrenComponent>(
            entt::exclude<ecs::ParentComponent>);
    queue.reserve(view.size_hint());
    for (const entt::entity root : roots) {
        roots.get<ecs::WorldTransformComponent>(root).worldMatrix =
            roots.get<ecs::LocalTransformComponent>(root).toMatrix();
        queue.push_back(root);
    }

    std::unordered_set<entt::entity> visited;
    visited.reserve(view.size_hint());
    for (std::size_t front = 0u; front < queue.size(); ++front) {
        const entt::entity entity = queue[front];
        if (!visited.insert(entity).second) {
            return false;
        }
        const glm::mat4& parentWorld = registry.get<ecs::WorldTransformComponent>(entity).worldMatrix;
        const auto& children = registry.get<ecs::ChildrenComponent>(entity).children;
        for (const entt::entity child : children) {
            if (!registry.valid(child) ||
                !registry.all_of<ecs::LocalTransformComponent, ecs::WorldTransformComponent, ecs::ChildrenComponent,
                                 ecs::ParentComponent>(child) ||
                registry.get<ecs::ParentComponent>(child).parent != entity) {
                return false;
            }
            registry.get<ecs::WorldTransformComponent>(child).worldMatrix =
                parentWorld * registry.get<ecs::LocalTransformComponent>(child).toMatrix();
            queue.push_back(child);
        }
    }
    return visited.size() == view.size_hint();
}

[[nodiscard]] bool validDocumentTransform(const scene::SceneTransformDocument& transform) {
    const auto finite = [](const glm::vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    return finite(transform.position) && finite(transform.rotation) && finite(transform.scale) &&
           std::abs(transform.scale.x) > 1e-8f && std::abs(transform.scale.y) > 1e-8f &&
           std::abs(transform.scale.z) > 1e-8f;
}
} // namespace

ModelSceneRuntime::~ModelSceneRuntime() {
    shutdown();
}

ModelSceneRuntime::ModelSceneRuntime() = default;

bool ModelSceneRuntime::init(ResourceMgr& resourceMgr, RhiDevice& rhiDevice, RhiCommandListPool& commandListPool,
                             ImGuiRhiRenderer& imguiRenderer) {
    shutdown();
    m_resourceMgr = &resourceMgr;
    m_deferredRenderer = std::make_unique<ModelSceneDeferredRenderer>();
    if (!m_deferredRenderer->init(resourceMgr, rhiDevice, commandListPool, imguiRenderer, *this)) {
        const std::string error = m_deferredRenderer->lastError();
        shutdown();
        m_lastError = error;
        return false;
    }
    m_lastError.clear();
    return true;
}

entt::entity ModelSceneRuntime::instantiateAsset(const scene::SceneAssetId assetId, const std::string& instanceName) {
    const auto assetIt = m_assetIndices.find(assetId);
    if (assetIt == m_assetIndices.end()) {
        setError("cannot instantiate an unknown model scene asset");
        return entt::null;
    }
    const MeshAsset& asset = m_assets[assetIt->second];
    const glm::vec3 center = (asset.boundsMin + asset.boundsMax) * 0.5f;
    const glm::vec3 extent = asset.boundsMax - asset.boundsMin;
    const float scale = 2.4f / std::max({extent.x, extent.y, extent.z});
    const entt::entity entity = createEntity(instanceName);
    if (entity == entt::null) {
        return entt::null;
    }
    m_registry.emplace<scene::StaticMeshComponent>(entity, scene::StaticMeshComponent{assetId});
    auto& transform = m_registry.get<ecs::LocalTransformComponent>(entity);
    transform.localPosition = -center * scale;
    transform.localScale = glm::vec3(scale);
    const glm::mat4 world = transform.toMatrix();
    m_registry.get<ecs::WorldTransformComponent>(entity).worldMatrix = world;
    m_registry.get<scene::PreviousWorldTransformComponent>(entity).worldMatrix = world;
    m_registry.emplace<scene::PickableComponent>(entity, scene::PickableComponent{asset.boundsMin, asset.boundsMax});
    m_selectedEntity = entity;
    return entity;
}

entt::entity ModelSceneRuntime::createEntity(const std::string& baseName) {
    if (m_nextEntityId == std::numeric_limits<scene::SceneEntityId>::max()) {
        setError("model scene entity ID space is exhausted");
        return entt::null;
    }
    const std::optional<renderer::contracts::StableObjectId> objectId =
        renderer::contracts::allocateStableSceneId<renderer::contracts::StableObjectIdTag>();
    if (!objectId.has_value()) {
        setError("stable model scene object identity space is exhausted");
        return entt::null;
    }
    const entt::entity entity = m_registry.create();
    m_registry.emplace<scene::SceneEntityIdComponent>(entity, scene::SceneEntityIdComponent{m_nextEntityId++});
    m_registry.emplace<scene::NameComponent>(entity, scene::NameComponent{makeUniqueInstanceName(baseName)});
    m_registry.emplace<scene::StableObjectIdComponent>(entity, scene::StableObjectIdComponent{*objectId});
    m_registry.emplace<ecs::LocalTransformComponent>(entity);
    m_registry.emplace<ecs::WorldTransformComponent>(entity);
    m_registry.emplace<scene::PreviousWorldTransformComponent>(entity);
    m_registry.emplace<ecs::ChildrenComponent>(entity);
    return entity;
}

entt::entity ModelSceneRuntime::createEmptyEntity(const std::string& baseName) {
    if (baseName.empty()) {
        setError("empty scene entities require a non-empty name");
        return entt::null;
    }
    const entt::entity entity = createEntity(baseName);
    if (entity != entt::null) {
        m_selectedEntity = entity;
        m_lastError.clear();
    }
    return entity;
}

entt::entity ModelSceneRuntime::createAssetInstance(const scene::SceneAssetId assetId) {
    const auto assetIt = m_assetIndices.find(assetId);
    if (assetIt == m_assetIndices.end()) {
        setError("cannot instantiate an unknown model scene asset");
        return entt::null;
    }
    const entt::entity entity = instantiateAsset(assetId, m_assets[assetIt->second].name);
    if (entity != entt::null) {
        m_lastError.clear();
    }
    return entity;
}

bool ModelSceneRuntime::renameEntity(const entt::entity entity, const std::string& requestedName) {
    if (!m_registry.valid(entity) || !m_registry.all_of<scene::NameComponent>(entity)) {
        setError("entity renaming requires a valid scene entity");
        return false;
    }
    if (requestedName.empty() || requestedName.find('\0') != std::string::npos) {
        setError("scene entity names must be non-empty text");
        return false;
    }
    auto& name = m_registry.get<scene::NameComponent>(entity).value;
    if (name == requestedName) {
        m_lastError.clear();
        return true;
    }
    name = makeUniqueInstanceName(requestedName, entity);
    m_lastError.clear();
    return true;
}

entt::entity ModelSceneRuntime::duplicateEntity(const entt::entity source) {
    if (!m_registry.valid(source) ||
        !m_registry.all_of<scene::SceneEntityIdComponent, scene::NameComponent, scene::StableObjectIdComponent,
                           ecs::LocalTransformComponent, ecs::WorldTransformComponent, ecs::ChildrenComponent>(
            source)) {
        setError("entity duplication requires a valid scene entity");
        return entt::null;
    }

    std::vector<entt::entity> originals{source};
    std::unordered_set<entt::entity> visited;
    for (std::size_t index = 0u; index < originals.size(); ++index) {
        const entt::entity entity = originals[index];
        if (!m_registry.valid(entity) ||
            !m_registry.all_of<scene::SceneEntityIdComponent, scene::NameComponent, scene::StableObjectIdComponent,
                               ecs::LocalTransformComponent, ecs::WorldTransformComponent, ecs::ChildrenComponent>(
                entity) ||
            !visited.insert(entity).second) {
            setError("entity duplication found an invalid scene hierarchy");
            return entt::null;
        }
        const auto& children = m_registry.get<ecs::ChildrenComponent>(entity).children;
        for (const entt::entity child : children) {
            const auto* parent = m_registry.try_get<ecs::ParentComponent>(child);
            if (parent == nullptr || parent->parent != entity) {
                setError("entity duplication found an inconsistent parent link");
                return entt::null;
            }
            originals.push_back(child);
        }
    }

    const auto maximumId = std::numeric_limits<scene::SceneEntityId>::max();
    if (originals.size() > maximumId - m_nextEntityId) {
        setError("model scene entity ID space cannot fit the duplicated hierarchy");
        return entt::null;
    }

    std::unordered_map<entt::entity, entt::entity> duplicates;
    duplicates.reserve(originals.size());
    const scene::SceneEntityId originalNextEntityId = m_nextEntityId;
    for (const entt::entity original : originals) {
        const auto& originalName = m_registry.get<scene::NameComponent>(original).value;
        const entt::entity duplicate = createEntity(originalName);
        if (duplicate == entt::null) {
            for (const auto& pair : duplicates) {
                if (m_registry.valid(pair.second)) {
                    m_registry.destroy(pair.second);
                }
            }
            m_nextEntityId = originalNextEntityId;
            return entt::null;
        }
        duplicates.emplace(original, duplicate);
        m_registry.replace<ecs::LocalTransformComponent>(duplicate,
                                                         m_registry.get<ecs::LocalTransformComponent>(original));
        if (const auto* mesh = m_registry.try_get<scene::StaticMeshComponent>(original)) {
            m_registry.emplace<scene::StaticMeshComponent>(duplicate, *mesh);
        }
        if (const auto* pickable = m_registry.try_get<scene::PickableComponent>(original)) {
            m_registry.emplace<scene::PickableComponent>(duplicate, *pickable);
        }
    }

    for (const entt::entity original : originals) {
        const entt::entity duplicate = duplicates.at(original);
        entt::entity duplicateParent = entt::null;
        if (const auto* originalParent = m_registry.try_get<ecs::ParentComponent>(original)) {
            const auto duplicateParentIt = duplicates.find(originalParent->parent);
            duplicateParent =
                duplicateParentIt != duplicates.end() ? duplicateParentIt->second : originalParent->parent;
        }
        if (duplicateParent != entt::null) {
            if (!m_registry.valid(duplicateParent) || !m_registry.all_of<ecs::ChildrenComponent>(duplicateParent)) {
                std::abort();
            }
            m_registry.emplace<ecs::ParentComponent>(duplicate, duplicateParent);
            m_registry.get<ecs::ChildrenComponent>(duplicateParent).children.push_back(duplicate);
        }
    }

    syncTransforms();
    for (const auto& pair : duplicates) {
        const glm::mat4& world = m_registry.get<ecs::WorldTransformComponent>(pair.second).worldMatrix;
        m_registry.get<scene::PreviousWorldTransformComponent>(pair.second).worldMatrix = world;
    }
    const entt::entity duplicateRoot = duplicates.at(source);
    m_selectedEntity = duplicateRoot;
    m_lastError.clear();
    return duplicateRoot;
}

bool ModelSceneRuntime::captureEntityState(const entt::entity entity, scene::SceneEntityDocument& state) const {
    if (!m_registry.valid(entity) || !m_registry.all_of<scene::SceneEntityIdComponent, scene::NameComponent,
                                                        ecs::LocalTransformComponent, ecs::ChildrenComponent>(entity)) {
        return false;
    }
    scene::SceneEntityDocument captured;
    captured.id = m_registry.get<scene::SceneEntityIdComponent>(entity).value;
    captured.name = m_registry.get<scene::NameComponent>(entity).value;
    if (const auto* parent = m_registry.try_get<ecs::ParentComponent>(entity)) {
        captured.parentId = entityId(parent->parent);
        if (*captured.parentId == scene::kInvalidSceneEntityId) {
            return false;
        }
    }
    if (const auto* mesh = m_registry.try_get<scene::StaticMeshComponent>(entity)) {
        captured.assetId = mesh->assetId;
    }
    const auto& transform = m_registry.get<ecs::LocalTransformComponent>(entity);
    captured.transform.position = transform.localPosition;
    captured.transform.rotation = transform.localRotation;
    captured.transform.scale = transform.localScale;
    state = std::move(captured);
    return true;
}

bool ModelSceneRuntime::captureEntitySubtree(const entt::entity root,
                                             std::vector<scene::SceneEntityDocument>& states) const {
    states.clear();
    if (!m_registry.valid(root)) {
        return false;
    }
    std::vector<entt::entity> entities{root};
    std::unordered_set<entt::entity> visited;
    for (std::size_t index = 0u; index < entities.size(); ++index) {
        const entt::entity entity = entities[index];
        if (!visited.insert(entity).second) {
            states.clear();
            return false;
        }
        scene::SceneEntityDocument state;
        if (!captureEntityState(entity, state)) {
            states.clear();
            return false;
        }
        states.push_back(std::move(state));
        const auto& children = m_registry.get<ecs::ChildrenComponent>(entity).children;
        for (const entt::entity child : children) {
            const auto* parent = m_registry.try_get<ecs::ParentComponent>(child);
            if (parent == nullptr || parent->parent != entity) {
                states.clear();
                return false;
            }
            entities.push_back(child);
        }
    }
    return true;
}

bool ModelSceneRuntime::applyEntityState(const scene::SceneEntityDocument& state) {
    const entt::entity entity = findEntity(state.id);
    if (entity == entt::null || state.name.empty() || state.name.find('\0') != std::string::npos ||
        !validDocumentTransform(state.transform)) {
        setError("entity state command contains invalid entity data");
        return false;
    }
    const auto* mesh = m_registry.try_get<scene::StaticMeshComponent>(entity);
    const std::optional<scene::SceneAssetId> currentAsset =
        mesh != nullptr ? std::optional<scene::SceneAssetId>(mesh->assetId) : std::nullopt;
    if (currentAsset != state.assetId) {
        setError("entity state command cannot change mesh asset ownership");
        return false;
    }
    const auto names = m_registry.view<scene::NameComponent>();
    for (const entt::entity namedEntity : names) {
        if (namedEntity != entity && names.get<scene::NameComponent>(namedEntity).value == state.name) {
            setError("entity state command would create a duplicate name");
            return false;
        }
    }

    entt::entity parent = entt::null;
    if (state.parentId.has_value()) {
        parent = findEntity(*state.parentId);
        if (parent == entt::null || parent == entity) {
            setError("entity state command references an invalid parent");
            return false;
        }
        std::unordered_set<entt::entity> ancestors;
        for (entt::entity ancestor = parent; ancestor != entt::null;) {
            if (ancestor == entity || !ancestors.insert(ancestor).second) {
                setError("entity state command would create a hierarchy cycle");
                return false;
            }
            const auto* next = m_registry.try_get<ecs::ParentComponent>(ancestor);
            ancestor = next != nullptr ? next->parent : entt::null;
        }
    }

    detachFromParent(entity);
    if (parent != entt::null) {
        m_registry.emplace<ecs::ParentComponent>(entity, parent);
        m_registry.get<ecs::ChildrenComponent>(parent).children.push_back(entity);
    }
    auto& name = m_registry.get<scene::NameComponent>(entity).value;
    name = state.name;
    ecs::LocalTransformComponent transform;
    transform.localPosition = state.transform.position;
    transform.localRotation = state.transform.rotation;
    transform.localScale = state.transform.scale;
    m_registry.replace<ecs::LocalTransformComponent>(entity, transform);
    syncTransforms();
    m_lastError.clear();
    return true;
}

entt::entity ModelSceneRuntime::restoreEntitySubtree(const std::vector<scene::SceneEntityDocument>& states) {
    if (states.empty()) {
        setError("entity subtree restoration requires at least one entity");
        return entt::null;
    }

    std::unordered_set<scene::SceneEntityId> restoredIds;
    std::unordered_set<std::string> restoredNames;
    restoredIds.reserve(states.size());
    restoredNames.reserve(states.size());
    const auto existingNames = m_registry.view<scene::NameComponent>();
    for (std::size_t index = 0u; index < states.size(); ++index) {
        const scene::SceneEntityDocument& state = states[index];
        if (state.id == scene::kInvalidSceneEntityId || state.id == std::numeric_limits<scene::SceneEntityId>::max() ||
            findEntity(state.id) != entt::null || !restoredIds.insert(state.id).second || state.name.empty() ||
            state.name.find('\0') != std::string::npos || !restoredNames.insert(state.name).second ||
            !validDocumentTransform(state.transform)) {
            setError("entity subtree restoration contains invalid or conflicting data");
            return entt::null;
        }
        for (const entt::entity existing : existingNames) {
            if (existingNames.get<scene::NameComponent>(existing).value == state.name) {
                setError("entity subtree restoration would create a duplicate name");
                return entt::null;
            }
        }
        if (state.assetId.has_value() && m_assetIndices.find(*state.assetId) == m_assetIndices.end()) {
            setError("entity subtree restoration references an unknown asset");
            return entt::null;
        }
    }

    std::unordered_map<scene::SceneEntityId, scene::SceneEntityId> parents;
    parents.reserve(states.size());
    for (std::size_t index = 0u; index < states.size(); ++index) {
        const scene::SceneEntityDocument& state = states[index];
        if (!state.parentId.has_value()) {
            parents.emplace(state.id, scene::kInvalidSceneEntityId);
            if (index != 0u) {
                setError("restored subtree contains more than one root");
                return entt::null;
            }
            continue;
        }
        if (*state.parentId == state.id) {
            setError("restored subtree entity cannot parent itself");
            return entt::null;
        }
        const bool parentIsRestored = restoredIds.find(*state.parentId) != restoredIds.end();
        if (index == 0u) {
            if (parentIsRestored) {
                setError("restored subtree root cannot reference a descendant");
                return entt::null;
            }
            if (findEntity(*state.parentId) == entt::null) {
                setError("restored subtree root references an unknown parent");
                return entt::null;
            }
        } else if (!parentIsRestored) {
            setError("restored subtree descendant references an external parent");
            return entt::null;
        }
        parents.emplace(state.id, *state.parentId);
    }
    for (const scene::SceneEntityDocument& state : states) {
        std::unordered_set<scene::SceneEntityId> ancestors;
        scene::SceneEntityId current = state.id;
        while (restoredIds.find(current) != restoredIds.end()) {
            if (!ancestors.insert(current).second) {
                setError("restored subtree contains a hierarchy cycle");
                return entt::null;
            }
            current = parents.at(current);
        }
    }

    std::vector<renderer::contracts::StableObjectId> stableObjectIds;
    stableObjectIds.reserve(states.size());
    for (std::size_t index = 0u; index < states.size(); ++index) {
        const std::optional<renderer::contracts::StableObjectId> objectId =
            renderer::contracts::allocateStableSceneId<renderer::contracts::StableObjectIdTag>();
        if (!objectId.has_value()) {
            setError("stable model scene object identity space is exhausted");
            return entt::null;
        }
        stableObjectIds.push_back(*objectId);
    }

    std::unordered_map<scene::SceneEntityId, entt::entity> restored;
    restored.reserve(states.size());
    for (std::size_t index = 0u; index < states.size(); ++index) {
        const scene::SceneEntityDocument& state = states[index];
        const entt::entity entity = m_registry.create();
        restored.emplace(state.id, entity);
        m_registry.emplace<scene::SceneEntityIdComponent>(entity, scene::SceneEntityIdComponent{state.id});
        m_registry.emplace<scene::NameComponent>(entity, scene::NameComponent{state.name});
        m_registry.emplace<scene::StableObjectIdComponent>(entity,
                                                           scene::StableObjectIdComponent{stableObjectIds[index]});
        ecs::LocalTransformComponent transform;
        transform.localPosition = state.transform.position;
        transform.localRotation = state.transform.rotation;
        transform.localScale = state.transform.scale;
        m_registry.emplace<ecs::LocalTransformComponent>(entity, transform);
        m_registry.emplace<ecs::WorldTransformComponent>(entity);
        m_registry.emplace<scene::PreviousWorldTransformComponent>(entity);
        m_registry.emplace<ecs::ChildrenComponent>(entity);
        if (state.assetId.has_value()) {
            const MeshAsset& asset = m_assets[m_assetIndices.at(*state.assetId)];
            m_registry.emplace<scene::StaticMeshComponent>(entity, scene::StaticMeshComponent{*state.assetId});
            m_registry.emplace<scene::PickableComponent>(entity,
                                                         scene::PickableComponent{asset.boundsMin, asset.boundsMax});
        }
    }
    for (const scene::SceneEntityDocument& state : states) {
        if (!state.parentId.has_value()) {
            continue;
        }
        const entt::entity child = restored.at(state.id);
        const auto restoredParent = restored.find(*state.parentId);
        const entt::entity parent =
            restoredParent != restored.end() ? restoredParent->second : findEntity(*state.parentId);
        if (parent == entt::null || !m_registry.all_of<ecs::ChildrenComponent>(parent)) {
            std::abort();
        }
        m_registry.emplace<ecs::ParentComponent>(child, parent);
        m_registry.get<ecs::ChildrenComponent>(parent).children.push_back(child);
    }
    syncTransforms();
    for (const auto& pair : restored) {
        const glm::mat4& world = m_registry.get<ecs::WorldTransformComponent>(pair.second).worldMatrix;
        m_registry.get<scene::PreviousWorldTransformComponent>(pair.second).worldMatrix = world;
    }
    for (const scene::SceneEntityDocument& state : states) {
        m_nextEntityId = std::max(m_nextEntityId, state.id + 1u);
    }
    const entt::entity root = restored.at(states.front().id);
    m_selectedEntity = root;
    m_lastError.clear();
    return root;
}

std::string ModelSceneRuntime::makeUniqueInstanceName(const std::string& baseName,
                                                      const entt::entity ignoredEntity) const {
    const auto names = m_registry.view<scene::NameComponent>();
    const auto nameExists = [&names, ignoredEntity](const std::string& candidate) {
        return std::any_of(names.begin(), names.end(), [&names, &candidate, ignoredEntity](const entt::entity entity) {
            if (entity == ignoredEntity) {
                return false;
            }
            return names.get<scene::NameComponent>(entity).value == candidate;
        });
    };
    if (!nameExists(baseName)) {
        return baseName;
    }
    for (size_t suffix = 2u;; ++suffix) {
        std::string candidate = baseName + " (" + std::to_string(suffix) + ")";
        if (!nameExists(candidate)) {
            return candidate;
        }
    }
}

entt::entity ModelSceneRuntime::importModel(const std::string& path) {
    if (m_resourceMgr == nullptr || path.empty()) {
        setError("model import requires a non-empty asset path");
        return entt::null;
    }
    std::error_code pathError;
    const std::filesystem::path filesystemPath =
        std::filesystem::weakly_canonical(std::filesystem::u8path(path), pathError);
    if (pathError) {
        setError("failed to resolve model import path: " + pathError.message());
        return entt::null;
    }
    const std::string normalizedPath = filesystemPath.generic_u8string();
    const std::string name = filesystemPath.stem().string();
    if (name.empty()) {
        setError("model import path must contain a file name");
        return entt::null;
    }
    const auto existing = std::find_if(m_assets.begin(), m_assets.end(), [&normalizedPath](const MeshAsset& asset) {
        return asset.path == normalizedPath;
    });
    scene::SceneAssetId assetId = scene::kInvalidSceneAssetId;
    if (existing == m_assets.end()) {
        if (!loadMeshAsset(*m_resourceMgr, name, normalizedPath, assetId)) {
            return entt::null;
        }
    } else {
        assetId = existing->id;
    }
    m_lastError.clear();
    return instantiateAsset(assetId, name);
}

void ModelSceneRuntime::destroyEntity(const entt::entity entity) {
    if (!m_registry.valid(entity)) {
        return;
    }

    detachFromParent(entity);
    std::vector<entt::entity> entities{entity};
    for (size_t index = 0u; index < entities.size(); ++index) {
        const auto* children = m_registry.try_get<ecs::ChildrenComponent>(entities[index]);
        if (children != nullptr) {
            entities.insert(entities.end(), children->children.begin(), children->children.end());
        }
    }
    if (std::find(entities.begin(), entities.end(), m_selectedEntity) != entities.end()) {
        m_selectedEntity = entt::null;
    }
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        if (m_registry.valid(*it)) {
            m_registry.destroy(*it);
        }
    }
}

scene::SceneEntityId ModelSceneRuntime::entityId(const entt::entity entity) const {
    if (!m_registry.valid(entity)) {
        return scene::kInvalidSceneEntityId;
    }
    const auto* id = m_registry.try_get<scene::SceneEntityIdComponent>(entity);
    return id != nullptr ? id->value : scene::kInvalidSceneEntityId;
}

entt::entity ModelSceneRuntime::findEntity(const scene::SceneEntityId id) const {
    if (id == scene::kInvalidSceneEntityId) {
        return entt::null;
    }
    const auto view = m_registry.view<scene::SceneEntityIdComponent>();
    const auto found = std::find_if(view.begin(), view.end(), [&view, id](const entt::entity entity) {
        return view.get<scene::SceneEntityIdComponent>(entity).value == id;
    });
    return found != view.end() ? *found : entt::null;
}

void ModelSceneRuntime::detachFromParent(const entt::entity entity) {
    const auto* parent = m_registry.try_get<ecs::ParentComponent>(entity);
    if (parent == nullptr) {
        return;
    }
    if (m_registry.valid(parent->parent)) {
        auto* siblings = m_registry.try_get<ecs::ChildrenComponent>(parent->parent);
        if (siblings != nullptr) {
            siblings->children.erase(std::remove(siblings->children.begin(), siblings->children.end(), entity),
                                     siblings->children.end());
        }
    }
    m_registry.remove<ecs::ParentComponent>(entity);
}

bool ModelSceneRuntime::localTransformFromMatrix(const glm::mat4& matrix,
                                                 ecs::LocalTransformComponent& transform) const {
    if (std::abs(matrix[0][3]) > 1e-5f || std::abs(matrix[1][3]) > 1e-5f || std::abs(matrix[2][3]) > 1e-5f ||
        std::abs(matrix[3][3] - 1.0f) > 1e-5f) {
        return false;
    }
    glm::vec3 basis[3] = {glm::vec3(matrix[0]), glm::vec3(matrix[1]), glm::vec3(matrix[2])};
    glm::vec3 scale{glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2])};
    if (scale.x <= 1e-8f || scale.y <= 1e-8f || scale.z <= 1e-8f) {
        return false;
    }
    basis[0] /= scale.x;
    basis[1] /= scale.y;
    basis[2] /= scale.z;
    if (std::abs(glm::dot(basis[0], basis[1])) > 1e-4f || std::abs(glm::dot(basis[0], basis[2])) > 1e-4f ||
        std::abs(glm::dot(basis[1], basis[2])) > 1e-4f) {
        return false;
    }
    if (glm::determinant(glm::mat3(basis[0], basis[1], basis[2])) < 0.0f) {
        int reflectionAxis = 0;
        if (scale.y > scale.x)
            reflectionAxis = 1;
        if (scale.z > scale[reflectionAxis])
            reflectionAxis = 2;
        scale[reflectionAxis] = -scale[reflectionAxis];
        basis[reflectionAxis] = -basis[reflectionAxis];
    }
    const glm::quat orientation = glm::quat_cast(glm::mat3(basis[0], basis[1], basis[2]));
    const glm::vec3 rotation = glm::degrees(glm::eulerAngles(glm::normalize(orientation)));
    const glm::vec3 translation = glm::vec3(matrix[3]);
    const auto finite = [](const glm::vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (!finite(translation) || !finite(rotation) || !finite(scale)) {
        return false;
    }
    ecs::LocalTransformComponent candidate;
    candidate.localPosition = translation;
    candidate.localRotation = rotation;
    candidate.localScale = scale;
    const glm::mat4 reconstructed = candidate.toMatrix();
    float largestDifference = 0.0f;
    float largestMagnitude = 1.0f;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            largestDifference = std::max(largestDifference, std::abs(reconstructed[column][row] - matrix[column][row]));
            largestMagnitude = std::max(largestMagnitude, std::abs(matrix[column][row]));
        }
    }
    if (largestDifference > largestMagnitude * 1e-3f) {
        return false;
    }
    transform = candidate;
    return true;
}

bool ModelSceneRuntime::setParent(const entt::entity child, const entt::entity parent) {
    const auto isSceneEntity = [this](const entt::entity entity) {
        return m_registry.valid(entity) &&
               m_registry.all_of<scene::SceneEntityIdComponent, ecs::LocalTransformComponent,
                                 ecs::WorldTransformComponent, ecs::ChildrenComponent>(entity);
    };
    if (!isSceneEntity(child) || (parent != entt::null && !isSceneEntity(parent))) {
        setError("scene hierarchy requires valid scene entities");
        return false;
    }
    if (child == parent) {
        setError("a scene entity cannot be parented to itself");
        return false;
    }

    std::unordered_set<entt::entity> ancestors;
    for (entt::entity ancestor = parent; ancestor != entt::null;) {
        if (ancestor == child) {
            setError("scene hierarchy reparenting would create a cycle");
            return false;
        }
        if (!ancestors.insert(ancestor).second) {
            setError("scene hierarchy contains an existing cycle");
            return false;
        }
        const auto* next = m_registry.try_get<ecs::ParentComponent>(ancestor);
        ancestor = next != nullptr ? next->parent : entt::null;
    }

    syncTransforms();
    const glm::mat4 childWorld = m_registry.get<ecs::WorldTransformComponent>(child).worldMatrix;
    glm::mat4 localMatrix = childWorld;
    if (parent != entt::null) {
        const glm::mat4& parentWorld = m_registry.get<ecs::WorldTransformComponent>(parent).worldMatrix;
        const float determinant = glm::determinant(glm::mat3(parentWorld));
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f) {
            setError("cannot parent under a singular world transform");
            return false;
        }
        localMatrix = glm::inverse(parentWorld) * childWorld;
    }
    ecs::LocalTransformComponent localTransform;
    if (!localTransformFromMatrix(localMatrix, localTransform)) {
        setError("reparenting cannot preserve a world transform containing shear");
        return false;
    }

    detachFromParent(child);
    if (parent != entt::null) {
        m_registry.emplace_or_replace<ecs::ParentComponent>(child, parent);
        m_registry.get<ecs::ChildrenComponent>(parent).children.push_back(child);
    }
    m_registry.replace<ecs::LocalTransformComponent>(child, localTransform);
    syncTransforms();
    m_lastError.clear();
    return true;
}

bool ModelSceneRuntime::setWorldTransform(const entt::entity entity, const glm::mat4& worldMatrix) {
    if (!m_registry.valid(entity) ||
        !m_registry.all_of<ecs::LocalTransformComponent, ecs::WorldTransformComponent>(entity)) {
        setError("world transform editing requires a valid scene entity");
        return false;
    }
    glm::mat4 localMatrix = worldMatrix;
    if (const auto* parent = m_registry.try_get<ecs::ParentComponent>(entity)) {
        if (!m_registry.valid(parent->parent) || !m_registry.all_of<ecs::WorldTransformComponent>(parent->parent)) {
            setError("world transform editing found an invalid parent entity");
            return false;
        }
        const glm::mat4& parentWorld = m_registry.get<ecs::WorldTransformComponent>(parent->parent).worldMatrix;
        const float determinant = glm::determinant(glm::mat3(parentWorld));
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f) {
            setError("cannot edit a child under a singular parent transform");
            return false;
        }
        localMatrix = glm::inverse(parentWorld) * worldMatrix;
    }
    ecs::LocalTransformComponent localTransform;
    if (!localTransformFromMatrix(localMatrix, localTransform)) {
        setError("world transform cannot be represented as a local TRS transform");
        return false;
    }
    m_registry.replace<ecs::LocalTransformComponent>(entity, localTransform);
    syncTransforms();
    m_lastError.clear();
    return true;
}

const std::string& ModelSceneRuntime::assetName(const size_t index) const {
    if (index >= m_assets.size()) {
        std::abort();
    }
    return m_assets[index].name;
}

scene::SceneAssetId ModelSceneRuntime::assetId(const size_t index) const {
    if (index >= m_assets.size()) {
        std::abort();
    }
    return m_assets[index].id;
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
    clearScene();
    m_resourceMgr = nullptr;
}

void ModelSceneRuntime::clearScene() {
    for (MeshAsset& asset : m_assets) {
        if (asset.renderer) {
            asset.renderer->shutdown();
        }
    }
    m_assets.clear();
    m_assetIndices.clear();
    m_registry.clear();
    m_selectedEntity = entt::null;
    m_nextEntityId = 1u;
    m_nextAssetId = 1u;
    m_nextReflectionProbeId = 1u;
    m_reflectionProbes.clear();
    m_reflectionProbeLights.clear();
    m_reflectionProbeSceneSignature = 0u;
    m_reflectionProbeSignatureValid = false;
    m_reflectionProbeRevisionInvalidated = false;
    m_lastError.clear();
}

void ModelSceneRuntime::resetEnvironment() {
    if (!m_deferredRenderer) {
        std::abort();
    }
    setTimeOfDay(300.0f);
    setTimePaused(true);
    setTimeScale(1.0f);
    setWeather(WeatherType::Clear, true);
    if (!setRenderSettings(ModelSceneDeferredRenderer::defaultSettings())) {
        std::abort();
    }
}

bool ModelSceneRuntime::allocateReflectionProbeIdentities(std::vector<RuntimeReflectionProbe>& probes) {
    for (RuntimeReflectionProbe& probe : probes) {
        const std::optional<renderer::contracts::StableReflectionProbeId> stableId =
            renderer::contracts::allocateStableSceneId<renderer::contracts::StableReflectionProbeIdTag>();
        if (!stableId.has_value()) {
            setError("stable model scene reflection-probe identity space is exhausted");
            return false;
        }
        probe.stableId = *stableId;
    }
    return true;
}

const scene::SceneReflectionProbeDocument& ModelSceneRuntime::reflectionProbe(const std::size_t index) const {
    if (index >= m_reflectionProbes.size()) {
        std::abort();
    }
    return m_reflectionProbes[index].document;
}

scene::SceneReflectionProbeId ModelSceneRuntime::addReflectionProbe(const glm::vec3& position) {
    if (m_reflectionProbes.size() >= renderer::contracts::kReflectionProbeCaptureMaxProbeCount) {
        setError("model scene reflection-probe capture capacity is exhausted");
        return scene::kInvalidSceneReflectionProbeId;
    }
    if (m_nextReflectionProbeId == std::numeric_limits<scene::SceneReflectionProbeId>::max()) {
        setError("model scene reflection-probe ID space is exhausted");
        return scene::kInvalidSceneReflectionProbeId;
    }

    RuntimeReflectionProbe probe;
    probe.document.id = m_nextReflectionProbeId;
    probe.document.position = position;
    probe.document.influenceMin = position - glm::vec3(4.0f);
    probe.document.influenceMax = position + glm::vec3(4.0f);
    probe.document.boxProjectionMin = probe.document.influenceMin;
    probe.document.boxProjectionMax = probe.document.influenceMax;
    probe.document.blendDistance = 1.0f;
    std::string validationError;
    if (!scene::ModelSceneSerializer::validateReflectionProbe(probe.document, validationError)) {
        setError(std::move(validationError));
        return scene::kInvalidSceneReflectionProbeId;
    }
    std::vector<RuntimeReflectionProbe> added{probe};
    if (!allocateReflectionProbeIdentities(added)) {
        return scene::kInvalidSceneReflectionProbeId;
    }
    const scene::SceneReflectionProbeId id = m_nextReflectionProbeId;
    ++m_nextReflectionProbeId;
    m_reflectionProbes.push_back(std::move(added.front()));
    m_lastError.clear();
    return id;
}

bool ModelSceneRuntime::updateReflectionProbe(const scene::SceneReflectionProbeDocument& probe) {
    const auto found = std::find_if(m_reflectionProbes.begin(), m_reflectionProbes.end(),
                                    [&probe](const auto& entry) { return entry.document.id == probe.id; });
    if (found == m_reflectionProbes.end()) {
        setError("cannot update an unknown model scene reflection probe");
        return false;
    }
    std::string validationError;
    if (!scene::ModelSceneSerializer::validateReflectionProbe(probe, validationError)) {
        setError(std::move(validationError));
        return false;
    }
    if (found->captureRevision == std::numeric_limits<uint32_t>::max()) {
        setError("model scene reflection-probe capture revision is exhausted");
        return false;
    }
    found->document = probe;
    ++found->captureRevision;
    m_lastError.clear();
    return true;
}

bool ModelSceneRuntime::removeReflectionProbe(const scene::SceneReflectionProbeId id) {
    const auto found = std::find_if(m_reflectionProbes.begin(), m_reflectionProbes.end(),
                                    [id](const auto& entry) { return entry.document.id == id; });
    if (found == m_reflectionProbes.end()) {
        setError("cannot remove an unknown model scene reflection probe");
        return false;
    }
    m_reflectionProbes.erase(found);
    m_lastError.clear();
    return true;
}

bool ModelSceneRuntime::sceneWorldBounds(glm::vec3& boundsMin, glm::vec3& boundsMax) const {
    boundsMin = glm::vec3(std::numeric_limits<float>::max());
    boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    bool foundMesh = false;
    const auto meshView = m_registry.view<scene::PickableComponent, ecs::WorldTransformComponent>();
    for (const entt::entity entity : meshView) {
        const scene::PickableComponent& bounds = meshView.get<scene::PickableComponent>(entity);
        const glm::mat4& worldMatrix = meshView.get<ecs::WorldTransformComponent>(entity).worldMatrix;
        for (uint32_t corner = 0u; corner < 8u; ++corner) {
            const glm::vec3 local{(corner & 1u) != 0u ? bounds.localBoundsMax.x : bounds.localBoundsMin.x,
                                  (corner & 2u) != 0u ? bounds.localBoundsMax.y : bounds.localBoundsMin.y,
                                  (corner & 4u) != 0u ? bounds.localBoundsMax.z : bounds.localBoundsMin.z};
            const glm::vec3 world = glm::vec3(worldMatrix * glm::vec4(local, 1.0f));
            if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z)) {
                return false;
            }
            boundsMin = glm::min(boundsMin, world);
            boundsMax = glm::max(boundsMax, world);
        }
        foundMesh = true;
    }
    return foundMesh;
}

bool ModelSceneRuntime::generateReflectionProbeGrid(const float spacingMeters, const float boundsPaddingMeters) {
    if (!std::isfinite(spacingMeters) || spacingMeters <= 0.0f || !std::isfinite(boundsPaddingMeters) ||
        boundsPaddingMeters < 0.0f) {
        setError("reflection-probe grid spacing and padding are invalid");
        return false;
    }
    syncTransforms();
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    if (!sceneWorldBounds(boundsMin, boundsMax)) {
        setError("reflection-probe grid generation requires finite scene mesh bounds");
        return false;
    }
    boundsMin -= glm::vec3(boundsPaddingMeters);
    boundsMax += glm::vec3(boundsPaddingMeters);
    const glm::vec3 extent = boundsMax - boundsMin;
    if (extent.x <= 0.0f || extent.y <= 0.0f || extent.z <= 0.0f) {
        setError("reflection-probe grid bounds must have positive volume");
        return false;
    }
    const glm::vec3 dimensionValues = glm::ceil(extent / spacingMeters);
    const float maximumDimension = static_cast<float>(renderer::contracts::kReflectionProbeCaptureMaxProbeCount);
    if (!std::isfinite(dimensionValues.x) || !std::isfinite(dimensionValues.y) || !std::isfinite(dimensionValues.z) ||
        dimensionValues.x > maximumDimension || dimensionValues.y > maximumDimension ||
        dimensionValues.z > maximumDimension) {
        setError("reflection-probe grid exceeds the capture capacity");
        return false;
    }
    const glm::uvec3 dimensions{static_cast<uint32_t>(dimensionValues.x), static_cast<uint32_t>(dimensionValues.y),
                                static_cast<uint32_t>(dimensionValues.z)};
    const uint64_t probeCount =
        static_cast<uint64_t>(dimensions.x) * static_cast<uint64_t>(dimensions.y) * static_cast<uint64_t>(dimensions.z);
    if (probeCount == 0u || probeCount > renderer::contracts::kReflectionProbeCaptureMaxProbeCount) {
        setError("reflection-probe grid exceeds the capture capacity");
        return false;
    }

    std::vector<RuntimeReflectionProbe> generated;
    generated.reserve(static_cast<std::size_t>(probeCount));
    const glm::vec3 cellExtent = extent / glm::vec3(dimensions);
    for (uint32_t z = 0u; z < dimensions.z; ++z) {
        for (uint32_t y = 0u; y < dimensions.y; ++y) {
            for (uint32_t x = 0u; x < dimensions.x; ++x) {
                RuntimeReflectionProbe probe;
                probe.document.id = static_cast<scene::SceneReflectionProbeId>(generated.size() + 1u);
                probe.document.influenceMin = boundsMin + glm::vec3(x, y, z) * cellExtent;
                probe.document.influenceMax = {
                    x + 1u == dimensions.x ? boundsMax.x : probe.document.influenceMin.x + cellExtent.x,
                    y + 1u == dimensions.y ? boundsMax.y : probe.document.influenceMin.y + cellExtent.y,
                    z + 1u == dimensions.z ? boundsMax.z : probe.document.influenceMin.z + cellExtent.z,
                };
                probe.document.position = (probe.document.influenceMin + probe.document.influenceMax) * 0.5f;
                probe.document.boxProjectionMin = boundsMin;
                probe.document.boxProjectionMax = boundsMax;
                probe.document.blendDistance = std::min({cellExtent.x, cellExtent.y, cellExtent.z}) * 0.2f;
                std::string validationError;
                if (!scene::ModelSceneSerializer::validateReflectionProbe(probe.document, validationError)) {
                    setError(std::move(validationError));
                    return false;
                }
                generated.push_back(probe);
            }
        }
    }
    if (!allocateReflectionProbeIdentities(generated)) {
        return false;
    }
    m_reflectionProbes = std::move(generated);
    m_nextReflectionProbeId = static_cast<scene::SceneReflectionProbeId>(probeCount + 1u);
    m_lastError.clear();
    return true;
}

bool ModelSceneRuntime::loadDocument(const scene::ModelSceneDocument& document) {
    if (m_resourceMgr == nullptr || !m_deferredRenderer) {
        setError("scene document loading requires an initialized model scene");
        return false;
    }
    std::string validationError;
    if (!scene::ModelSceneSerializer::validate(document, validationError)) {
        setError(std::move(validationError));
        return false;
    }
    if (!ModelSceneDeferredRenderer::validateSettings(document.environment.renderSettings, validationError)) {
        setError(std::move(validationError));
        return false;
    }
    if (document.environment.renderSettings.upscale.fsr1Enabled && !m_deferredRenderer->isFsr1Supported()) {
        setError("FSR 1 requires the OpenGL graphics backend");
        return false;
    }
    if (document.environment.renderSettings.upscale.type == TemporalUpscalerType::Fsr31 &&
        !m_deferredRenderer->isFsr31Supported()) {
        setError("FSR 3.1 requires an enabled Vulkan FSR 3.1 build");
        return false;
    }

    std::vector<RuntimeReflectionProbe> loadedProbes;
    loadedProbes.reserve(document.reflectionProbes.size());
    for (const scene::SceneReflectionProbeDocument& entry : document.reflectionProbes) {
        RuntimeReflectionProbe probe;
        probe.document = entry;
        loadedProbes.push_back(probe);
    }
    std::sort(loadedProbes.begin(), loadedProbes.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.document.id < rhs.document.id; });
    if (!allocateReflectionProbeIdentities(loadedProbes)) {
        return false;
    }

    std::vector<MeshAsset> loadedAssets;
    loadedAssets.reserve(document.assets.size());
    std::unordered_map<scene::SceneAssetId, uint32_t> loadedAssetIndices;
    loadedAssetIndices.reserve(document.assets.size());
    for (const scene::SceneAssetDocument& entry : document.assets) {
        MeshAsset asset;
        if (!createMeshAsset(*m_resourceMgr, entry.id, entry.name, entry.path, asset)) {
            for (MeshAsset& loaded : loadedAssets) {
                loaded.renderer->shutdown();
            }
            return false;
        }
        const uint32_t index = static_cast<uint32_t>(loadedAssets.size());
        loadedAssetIndices.emplace(entry.id, index);
        loadedAssets.push_back(std::move(asset));
    }

    entt::registry loadedRegistry;
    std::vector<renderer::contracts::StableObjectId> stableObjectIds;
    stableObjectIds.reserve(document.entities.size());
    for (std::size_t index = 0u; index < document.entities.size(); ++index) {
        const std::optional<renderer::contracts::StableObjectId> objectId =
            renderer::contracts::allocateStableSceneId<renderer::contracts::StableObjectIdTag>();
        if (!objectId.has_value()) {
            for (MeshAsset& loaded : loadedAssets) {
                loaded.renderer->shutdown();
            }
            setError("stable model scene object identity space is exhausted");
            return false;
        }
        stableObjectIds.push_back(*objectId);
    }
    std::unordered_map<scene::SceneEntityId, entt::entity> loadedEntities;
    loadedEntities.reserve(document.entities.size());
    entt::entity selectedEntity = entt::null;
    scene::SceneEntityId selectedId = std::numeric_limits<scene::SceneEntityId>::max();
    for (std::size_t index = 0u; index < document.entities.size(); ++index) {
        const scene::SceneEntityDocument& entry = document.entities[index];
        const entt::entity entity = loadedRegistry.create();
        loadedEntities.emplace(entry.id, entity);
        loadedRegistry.emplace<scene::SceneEntityIdComponent>(entity, scene::SceneEntityIdComponent{entry.id});
        loadedRegistry.emplace<scene::NameComponent>(entity, scene::NameComponent{entry.name});
        loadedRegistry.emplace<scene::StableObjectIdComponent>(entity,
                                                               scene::StableObjectIdComponent{stableObjectIds[index]});
        ecs::LocalTransformComponent transform;
        transform.localPosition = entry.transform.position;
        transform.localRotation = entry.transform.rotation;
        transform.localScale = entry.transform.scale;
        loadedRegistry.emplace<ecs::LocalTransformComponent>(entity, transform);
        loadedRegistry.emplace<ecs::WorldTransformComponent>(entity);
        loadedRegistry.emplace<scene::PreviousWorldTransformComponent>(entity);
        loadedRegistry.emplace<ecs::ChildrenComponent>(entity);
        if (entry.assetId.has_value()) {
            const uint32_t assetIndex = loadedAssetIndices.at(*entry.assetId);
            const MeshAsset& asset = loadedAssets[assetIndex];
            loadedRegistry.emplace<scene::StaticMeshComponent>(entity, scene::StaticMeshComponent{*entry.assetId});
            loadedRegistry.emplace<scene::PickableComponent>(
                entity, scene::PickableComponent{asset.boundsMin, asset.boundsMax});
        }
        if (entry.id < selectedId) {
            selectedId = entry.id;
            selectedEntity = entity;
        }
    }
    for (const scene::SceneEntityDocument& entry : document.entities) {
        if (!entry.parentId.has_value()) {
            continue;
        }
        const entt::entity child = loadedEntities.at(entry.id);
        const entt::entity parent = loadedEntities.at(*entry.parentId);
        loadedRegistry.emplace<ecs::ParentComponent>(child, parent);
        loadedRegistry.get<ecs::ChildrenComponent>(parent).children.push_back(child);
    }
    if (!syncRegistryTransforms(loadedRegistry)) {
        for (MeshAsset& loaded : loadedAssets) {
            loaded.renderer->shutdown();
        }
        setError("validated scene hierarchy could not be synchronized");
        return false;
    }

    for (MeshAsset& asset : m_assets) {
        asset.renderer->shutdown();
    }
    m_assets = std::move(loadedAssets);
    m_assetIndices = std::move(loadedAssetIndices);
    m_reflectionProbes = std::move(loadedProbes);
    m_registry.swap(loadedRegistry);
    m_selectedEntity = selectedEntity;
    m_nextEntityId = 1u;
    for (const scene::SceneEntityDocument& entry : document.entities) {
        m_nextEntityId = std::max(m_nextEntityId, entry.id + 1u);
    }
    m_nextAssetId = 1u;
    for (const scene::SceneAssetDocument& entry : document.assets) {
        m_nextAssetId = std::max(m_nextAssetId, entry.id + 1u);
    }
    m_nextReflectionProbeId = 1u;
    for (const scene::SceneReflectionProbeDocument& entry : document.reflectionProbes) {
        m_nextReflectionProbeId = std::max(m_nextReflectionProbeId, entry.id + 1u);
    }
    setTimeOfDay(document.environment.timeOfDay);
    setTimePaused(document.environment.timePaused);
    setTimeScale(document.environment.timeScale);
    setWeather(document.environment.weather, document.environment.weatherTransitionInstant);
    if (!setRenderSettings(document.environment.renderSettings)) {
        std::abort();
    }
    m_lastError.clear();
    return true;
}

bool ModelSceneRuntime::loadMeshAsset(ResourceMgr& resourceMgr, const std::string& name, const std::string& path,
                                      scene::SceneAssetId& assetId) {
    if (m_nextAssetId == std::numeric_limits<scene::SceneAssetId>::max()) {
        setError("model scene asset ID space is exhausted");
        return false;
    }
    MeshAsset asset;
    assetId = m_nextAssetId;
    if (!createMeshAsset(resourceMgr, assetId, name, path, asset)) {
        return false;
    }
    ++m_nextAssetId;
    const uint32_t index = static_cast<uint32_t>(m_assets.size());
    m_assets.push_back(std::move(asset));
    m_assetIndices.emplace(assetId, index);
    return true;
}

bool ModelSceneRuntime::createMeshAsset(ResourceMgr& resourceMgr, const scene::SceneAssetId assetId,
                                        const std::string& name, const std::string& path, MeshAsset& asset) {
    auto renderer = std::make_unique<StaticMeshRenderer>();
    if (!renderer->init(resourceMgr, path, m_deferredRenderer->globalBindlessSet())) {
        setError(renderer->lastError());
        return false;
    }
    asset.id = assetId;
    asset.name = name;
    asset.path = path;
    renderer->assetBounds(asset.boundsMin, asset.boundsMax);
    asset.renderer = std::move(renderer);
    return true;
}

uint32_t ModelSceneRuntime::assetIndex(const scene::SceneAssetId id) const {
    const auto found = m_assetIndices.find(id);
    if (found == m_assetIndices.end()) {
        std::abort();
    }
    return found->second;
}

bool ModelSceneRuntime::ensureViewport(const uint32_t width, const uint32_t height) {
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

bool ModelSceneRuntime::renderViewport(const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPosition, const float nearPlane, const float farPlane,
                                       const float verticalFovDegrees, const RenderFrameClock& frameClock) {
    if (!m_deferredRenderer) {
        setError("model scene deferred renderer is unavailable");
        return false;
    }
    if (!configureReflectionProbeCapture()) {
        return false;
    }
    if (!m_deferredRenderer->render(view, projection, cameraPosition, nearPlane, farPlane, verticalFovDegrees,
                                    frameClock)) {
        setError(m_deferredRenderer->lastError());
        return false;
    }
    m_lastError.clear();
    return true;
}

bool ModelSceneRuntime::prepareGBuffer(RhiCommandList& commandList, const FrameContext& context) {
    for (MeshAsset& asset : m_assets) {
        asset.renderer->prepareStandaloneFrame();
        if (!asset.renderer->prepareGBuffer(commandList, context.camera.jitteredViewProj,
                                            context.previousViewProjWithCurrentJitter, context)) {
            return false;
        }
    }
    return true;
}

void ModelSceneRuntime::renderToGBuffer(RhiCommandList& commandList, const glm::mat4& viewProjection,
                                        const glm::mat4& previousViewProjection) {
    (void)viewProjection;
    (void)previousViewProjection;
    const auto view = m_registry.view<scene::StaticMeshComponent, scene::StableObjectIdComponent,
                                      ecs::WorldTransformComponent, scene::PreviousWorldTransformComponent>();
    for (const entt::entity entity : view) {
        const auto& mesh = view.get<scene::StaticMeshComponent>(entity);
        const auto& objectId = view.get<scene::StableObjectIdComponent>(entity).value;
        const auto& world = view.get<ecs::WorldTransformComponent>(entity);
        const auto& previous = view.get<scene::PreviousWorldTransformComponent>(entity);
        StaticMeshRenderer& renderer = *m_assets[assetIndex(mesh.assetId)].renderer;
        renderer.setInstanceTransform(world.worldMatrix, previous.worldMatrix);
        if (!renderer.setStableObjectId(objectId)) {
            std::abort();
        }
        renderer.renderToGBuffer(commandList);
    }
}

void ModelSceneRuntime::renderToShadowMap(RhiCommandList& commandList, const glm::mat4& shadowViewProjection) {
    const auto view =
        m_registry
            .view<scene::StaticMeshComponent, ecs::WorldTransformComponent, scene::PreviousWorldTransformComponent>();
    for (const entt::entity entity : view) {
        const auto& mesh = view.get<scene::StaticMeshComponent>(entity);
        StaticMeshRenderer& renderer = *m_assets[assetIndex(mesh.assetId)].renderer;
        renderer.setInstanceTransform(view.get<ecs::WorldTransformComponent>(entity).worldMatrix,
                                      view.get<scene::PreviousWorldTransformComponent>(entity).worldMatrix);
        renderer.renderToShadowMap(commandList, shadowViewProjection);
    }
}

bool ModelSceneRuntime::prepareShadowFrame() {
    for (MeshAsset& asset : m_assets) {
        if (asset.renderer == nullptr) {
            setError("model scene shadow preparation references an empty mesh asset");
            return false;
        }
        asset.renderer->prepareStandaloneFrame();
    }
    return true;
}

bool ModelSceneRuntime::collectSceneLights(const glm::vec3& cameraPosition,
                                           std::vector<renderer::contracts::SceneLight>& lights, std::string& error) {
    struct LightEntityEntry final {
        scene::SceneEntityId sceneEntityId = scene::kInvalidSceneEntityId;
        entt::entity entity = entt::null;
        StaticMeshRenderer* renderer = nullptr;
    };

    const auto view =
        m_registry.view<scene::StaticMeshComponent, scene::SceneEntityIdComponent, ecs::WorldTransformComponent>();
    std::vector<LightEntityEntry> entries;
    entries.reserve(view.size_hint());
    std::size_t totalLightCount = 0u;
    std::vector<renderer::contracts::SceneLight> collected;
    for (const entt::entity entity : view) {
        const auto& mesh = view.get<scene::StaticMeshComponent>(entity);
        const auto assetIt = m_assetIndices.find(mesh.assetId);
        if (assetIt == m_assetIndices.end() || assetIt->second >= m_assets.size() ||
            m_assets[assetIt->second].renderer == nullptr) {
            error = "model scene light collection references an unknown mesh asset";
            return false;
        }
        StaticMeshRenderer* const renderer = m_assets[assetIt->second].renderer.get();
        const std::size_t assetLightCount = renderer->punctualLightCount();
        if (assetLightCount > collected.max_size() - totalLightCount) {
            error = "model scene punctual-light count exceeds vector capacity";
            return false;
        }
        totalLightCount += assetLightCount;
        entries.push_back({view.get<scene::SceneEntityIdComponent>(entity).value, entity, renderer});
    }
    std::sort(entries.begin(), entries.end(), [](const LightEntityEntry& lhs, const LightEntityEntry& rhs) {
        return lhs.sceneEntityId < rhs.sceneEntityId;
    });

    collected.reserve(totalLightCount);
    for (const LightEntityEntry& entry : entries) {
        const entt::entity entity = entry.entity;
        StaticMeshRenderer& renderer = *entry.renderer;
        auto* identities = m_registry.try_get<scene::StaticMeshLightIdentityComponent>(entity);
        if (identities == nullptr) {
            scene::StaticMeshLightIdentityComponent created;
            created.values.reserve(renderer.punctualLightCount());
            for (std::size_t index = 0u; index < renderer.punctualLightCount(); ++index) {
                const std::optional<renderer::contracts::StableLightId> id =
                    renderer::contracts::allocateStableSceneId<renderer::contracts::StableLightIdTag>();
                if (!id.has_value()) {
                    error = "stable model punctual-light identity space is exhausted";
                    return false;
                }
                created.values.push_back(*id);
            }
            identities = &m_registry.emplace<scene::StaticMeshLightIdentityComponent>(entity, std::move(created));
        }
        if (identities->values.size() != renderer.punctualLightCount()) {
            error = "model scene punctual-light identity count changed after asset creation";
            return false;
        }
        std::string assetError;
        if (!renderer.appendPunctualLights(m_registry.get<ecs::WorldTransformComponent>(entity).worldMatrix,
                                           cameraPosition, identities->values, collected, assetError)) {
            error = std::move(assetError);
            return false;
        }
    }
    lights = std::move(collected);
    error.clear();
    return true;
}

bool ModelSceneRuntime::queryLocalShadowSceneRevisions(DeferredLocalShadowSceneRevisions& revisions,
                                                       std::string& error) const {
    if (!m_reflectionProbeSignatureValid || m_reflectionProbeSceneSignature == 0u) {
        error = "model scene local-shadow revisions are unavailable before scene synchronization";
        return false;
    }
    revisions.geometryContentRevision = m_reflectionProbeSceneSignature;
    revisions.activeGeometryRevision = m_reflectionProbeSceneSignature;
    revisions.dynamicOccluderRevision = 0u;
    error.clear();
    return true;
}

bool ModelSceneRuntime::collectRayTracingInstances(std::vector<renderer::rt::SceneTlasInstanceInput>& instances,
                                                   std::string& error) const {
    std::vector<renderer::rt::SceneTlasInstanceInput> collected;
    const auto view = m_registry.view<scene::StaticMeshComponent, scene::StableObjectIdComponent,
                                      scene::SceneEntityIdComponent, ecs::WorldTransformComponent>();
    collected.reserve(view.size_hint());
    for (const entt::entity entity : view) {
        const auto& mesh = view.get<scene::StaticMeshComponent>(entity);
        const MeshAsset& asset = m_assets[assetIndex(mesh.assetId)];
        if (asset.renderer == nullptr) {
            error = "model scene TLAS references an empty mesh asset";
            return false;
        }
        const renderer::rt::StaticMeshBlasStats& blasStats = asset.renderer->staticBlasStats();
        if (blasStats.geometryCount == 0u) {
            continue;
        }
        const renderer::rt::SceneBlasResourcePtr& blas = asset.renderer->staticBlasResource();
        const renderer::rt::StaticMeshRayTracingResourcePtr& hitData = asset.renderer->staticRayTracingResource();
        if (blas == nullptr || !blasStats.resident || hitData == nullptr) {
            error = "model scene solid mesh has no resident asset-level ray-tracing resources";
            return false;
        }
        uint8_t mask = renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::ShadowCaster) |
                       renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::ReflectionVisible);
        if (blasStats.containsOpaque) {
            mask |= renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiOpaque);
        }
        if (blasStats.containsCutout) {
            mask |= renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiCutout);
        }
        const renderer::contracts::StableObjectId objectId = view.get<scene::StableObjectIdComponent>(entity).value;
        const scene::SceneEntityId sceneEntityId = view.get<scene::SceneEntityIdComponent>(entity).value;
        collected.push_back({{renderer::rt::SceneTlasInstanceKind::StaticMesh, static_cast<int64_t>(objectId.value),
                              static_cast<int64_t>(sceneEntityId)},
                             blas,
                             view.get<ecs::WorldTransformComponent>(entity).worldMatrix,
                             mask,
                             blasStats.containsDoubleSided,
                             std::nullopt,
                             hitData});
    }
    instances = std::move(collected);
    error.clear();
    return true;
}

renderer::rt::StaticMeshBlasAggregateStats ModelSceneRuntime::staticBlasAggregateStats() const {
    renderer::rt::StaticMeshBlasAggregateStats stats;
    for (const MeshAsset& asset : m_assets) {
        if (asset.renderer == nullptr) {
            std::abort();
        }
        stats.add(asset.renderer->staticBlasStats());
    }
    return stats;
}

bool ModelSceneRuntime::configureClusteredLighting(const DeferredClusteredLightingResources& resources) {
    for (MeshAsset& asset : m_assets) {
        if (!asset.renderer->configureClusteredLighting(resources.bindGroupLayout, resources.bindGroup,
                                                        resources.grid)) {
            setError(asset.renderer->lastError());
            return false;
        }
    }
    return true;
}

bool ModelSceneRuntime::hasTransparentGeometry() const {
    const auto view = m_registry.view<scene::StaticMeshComponent>();
    return std::any_of(view.begin(), view.end(), [this, &view](const entt::entity entity) {
        const auto& mesh = view.get<scene::StaticMeshComponent>(entity);
        return m_assets[assetIndex(mesh.assetId)].renderer->hasTransparentPrimitives();
    });
}

bool ModelSceneRuntime::prepareTransparentResources(const DeferredTransparentResources& resources) {
    for (MeshAsset& asset : m_assets) {
        if (asset.renderer->hasTransparentPrimitives() &&
            !asset.renderer->prepareTransparentResources(resources.sceneColor, resources.opaqueDepth,
                                                         resources.skyCapture)) {
            setError(asset.renderer->lastError());
            return false;
        }
    }
    return true;
}

void ModelSceneRuntime::renderTransparent(RhiCommandList& commandList, const glm::vec3& cameraPosition,
                                          const float reflectionCompositeStrength) {
    struct QueuedDraw {
        StaticMeshRenderer* renderer = nullptr;
        StaticMeshRenderer::TransparentDraw draw;
    };
    std::vector<QueuedDraw> queued;
    std::vector<StaticMeshRenderer::TransparentDraw> assetDraws;
    const auto view = m_registry.view<scene::StaticMeshComponent, ecs::WorldTransformComponent>();
    for (const entt::entity entity : view) {
        const auto& mesh = view.get<scene::StaticMeshComponent>(entity);
        const auto& world = view.get<ecs::WorldTransformComponent>(entity);
        StaticMeshRenderer& renderer = *m_assets[assetIndex(mesh.assetId)].renderer;
        assetDraws.clear();
        renderer.appendTransparentDraws(world.worldMatrix, cameraPosition, assetDraws);
        queued.reserve(queued.size() + assetDraws.size());
        for (StaticMeshRenderer::TransparentDraw& draw : assetDraws) {
            queued.push_back({&renderer, std::move(draw)});
        }
    }
    std::sort(queued.begin(), queued.end(), [](const QueuedDraw& lhs, const QueuedDraw& rhs) {
        return lhs.draw.distanceSquared > rhs.draw.distanceSquared;
    });
    for (const QueuedDraw& queuedDraw : queued) {
        queuedDraw.renderer->renderTransparentDraw(commandList, queuedDraw.draw, reflectionCompositeStrength);
    }
}

bool ModelSceneRuntime::configureReflectionProbeCapture() {
    if (!m_deferredRenderer) {
        setError("model scene reflection-probe capture is not initialized");
        return false;
    }

    struct SignatureEntry final {
        scene::SceneEntityId entityId = scene::kInvalidSceneEntityId;
        scene::SceneAssetId assetId = scene::kInvalidSceneAssetId;
        glm::mat4 world{1.0f};
        glm::vec3 localBoundsMin{0.0f};
        glm::vec3 localBoundsMax{0.0f};
    };
    std::vector<SignatureEntry> entries;
    const auto meshView = m_registry.view<scene::SceneEntityIdComponent, scene::StaticMeshComponent,
                                          scene::PickableComponent, ecs::WorldTransformComponent>();
    entries.reserve(meshView.size_hint());
    for (const entt::entity entity : meshView) {
        const auto& mesh = meshView.get<scene::StaticMeshComponent>(entity);
        const auto& bounds = meshView.get<scene::PickableComponent>(entity);
        entries.push_back({meshView.get<scene::SceneEntityIdComponent>(entity).value, mesh.assetId,
                           meshView.get<ecs::WorldTransformComponent>(entity).worldMatrix, bounds.localBoundsMin,
                           bounds.localBoundsMax});
    }
    std::sort(entries.begin(), entries.end(),
              [](const SignatureEntry& lhs, const SignatureEntry& rhs) { return lhs.entityId < rhs.entityId; });

    uint64_t signature = 1469598103934665603ull;
    const auto appendSignature = [&signature](const auto& value) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
        for (std::size_t index = 0u; index < sizeof(value); ++index) {
            signature ^= static_cast<uint64_t>(bytes[index]);
            signature *= 1099511628211ull;
        }
    };
    for (const SignatureEntry& entry : entries) {
        appendSignature(entry.entityId);
        appendSignature(entry.assetId);
        appendSignature(entry.world);
        appendSignature(entry.localBoundsMin);
        appendSignature(entry.localBoundsMax);
    }
    const bool sceneChanged = m_reflectionProbeRevisionInvalidated || signature != m_reflectionProbeSceneSignature;
    if (!m_reflectionProbeSignatureValid) {
        m_reflectionProbeSceneSignature = signature;
        m_reflectionProbeSignatureValid = true;
    } else if (sceneChanged) {
        for (const RuntimeReflectionProbe& probe : m_reflectionProbes) {
            if (probe.captureRevision == std::numeric_limits<uint32_t>::max()) {
                setError("model scene reflection-probe capture revision is exhausted");
                return false;
            }
        }
        for (RuntimeReflectionProbe& probe : m_reflectionProbes) {
            ++probe.captureRevision;
        }
        m_reflectionProbeSceneSignature = signature;
    }
    m_reflectionProbeRevisionInvalidated = false;

    std::vector<ReflectionProbeCaptureSource> sources;
    sources.reserve(m_reflectionProbes.size());
    for (const RuntimeReflectionProbe& probe : m_reflectionProbes) {
        const scene::SceneReflectionProbeDocument& document = probe.document;
        ReflectionProbeCaptureSource source;
        source.probeId = probe.stableId;
        source.positionWorldMeters = document.position;
        source.exposureScale = document.exposureScale;
        source.influenceMinWorldMeters = document.influenceMin;
        source.influenceMaxWorldMeters = document.influenceMax;
        source.blendDistanceMeters = document.blendDistance;
        source.boxProjectionMinWorldMeters = document.boxProjectionMin;
        source.boxProjectionMaxWorldMeters = document.boxProjectionMax;
        source.requestedRevision = probe.captureRevision;
        sources.push_back(source);
    }

    if (!sources.empty()) {
        std::string lightError;
        if (!collectSceneLights(sources.front().positionWorldMeters, m_reflectionProbeLights, lightError)) {
            setError(lightError.empty() ? "failed to collect model scene probe-capture lights" : std::move(lightError));
            return false;
        }
        if (m_reflectionProbeLights.size() > renderer::contracts::kClusterMaxLightCount) {
            setError("model scene probe-capture light capacity is exceeded");
            return false;
        }
        for (MeshAsset& asset : m_assets) {
            if (!asset.renderer->ensureReflectionProbeCaptureLightCapacity(
                    static_cast<uint32_t>(m_reflectionProbeLights.size()))) {
                setError("failed to allocate model scene probe-capture light buffer");
                return false;
            }
        }
    } else {
        m_reflectionProbeLights.clear();
    }
    if (!m_deferredRenderer->configureReflectionProbeCapture(*this, std::move(sources))) {
        setError(m_deferredRenderer->lastError());
        return false;
    }
    return true;
}

bool ModelSceneRuntime::recordReflectionProbeRadianceOpaque(RhiCommandList& commandList, const FrameContext& context,
                                                            const ReflectionProbeCaptureWork& work) {
    if (!work.targetView.isValid() || !work.depthTargetView.isValid() ||
        work.face >= renderer::contracts::kReflectionProbeCubeFaceCount) {
        setError("model scene reflection-probe capture work is invalid");
        return false;
    }
    std::string lightError;
    if (!collectSceneLights(work.positionWorldMeters, m_reflectionProbeLights, lightError)) {
        setError(lightError.empty() ? "failed to collect model scene probe-capture lights" : std::move(lightError));
        return false;
    }
    for (MeshAsset& asset : m_assets) {
        asset.renderer->prepareStandaloneFrame();
        if (!asset.renderer->prepareReflectionProbeCapture(commandList, work.viewProjection, work.positionWorldMeters,
                                                           context, m_reflectionProbeLights)) {
            setError("failed to prepare model scene probe-capture material resources");
            return false;
        }
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = work.targetView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    const glm::vec3 skyRadiance = context.skyColors.horizon * context.skyIntensity;
    colorAttachment.clearColor[0] = skyRadiance.r;
    colorAttachment.clearColor[1] = skyRadiance.g;
    colorAttachment.clearColor[2] = skyRadiance.b;
    colorAttachment.clearColor[3] = 1.0f;
    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = work.depthTargetView;
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "ModelScene.ReflectionProbeCapture";
    renderingInfo.renderArea = {0, 0, renderer::contracts::kReflectionProbeCubeExtent,
                                renderer::contracts::kReflectionProbeCubeExtent};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;
    commandList.beginRendering(renderingInfo);
    commandList.setViewport({0.0f, 0.0f, static_cast<float>(renderer::contracts::kReflectionProbeCubeExtent),
                             static_cast<float>(renderer::contracts::kReflectionProbeCubeExtent), 0.0f, 1.0f});
    commandList.setScissor(renderingInfo.renderArea);

    const auto meshView =
        m_registry
            .view<scene::StaticMeshComponent, ecs::WorldTransformComponent, scene::PreviousWorldTransformComponent>();
    for (const entt::entity entity : meshView) {
        const auto& mesh = meshView.get<scene::StaticMeshComponent>(entity);
        const glm::mat4& world = meshView.get<ecs::WorldTransformComponent>(entity).worldMatrix;
        StaticMeshRenderer& renderer = *m_assets[assetIndex(mesh.assetId)].renderer;
        renderer.setInstanceTransform(world, meshView.get<scene::PreviousWorldTransformComponent>(entity).worldMatrix);
        renderer.renderReflectionProbeCaptureOpaque(commandList);
    }
    commandList.endRendering();
    return true;
}

bool ModelSceneRuntime::recordReflectionProbeRadianceTransparent(RhiCommandList& commandList, const FrameContext&,
                                                                 const ReflectionProbeCaptureWork& work) {
    if (!work.targetView.isValid() || !work.depthTargetView.isValid() ||
        work.face >= renderer::contracts::kReflectionProbeCubeFaceCount) {
        setError("model scene reflection-probe transparent capture work is invalid");
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = work.targetView;
    colorAttachment.loadOp = RhiLoadOp::Load;
    colorAttachment.storeOp = RhiStoreOp::Store;
    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = work.depthTargetView;
    depthAttachment.depthLoadOp = RhiLoadOp::Load;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "ModelScene.ReflectionProbeCapture.Transparent";
    renderingInfo.renderArea = {0, 0, renderer::contracts::kReflectionProbeCubeExtent,
                                renderer::contracts::kReflectionProbeCubeExtent};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;
    commandList.beginRendering(renderingInfo);
    commandList.setViewport({0.0f, 0.0f, static_cast<float>(renderer::contracts::kReflectionProbeCubeExtent),
                             static_cast<float>(renderer::contracts::kReflectionProbeCubeExtent), 0.0f, 1.0f});
    commandList.setScissor(renderingInfo.renderArea);

    struct QueuedDraw final {
        StaticMeshRenderer* renderer = nullptr;
        StaticMeshRenderer::TransparentDraw draw;
    };
    std::vector<QueuedDraw> transparentDraws;
    std::vector<StaticMeshRenderer::TransparentDraw> assetDraws;
    const auto meshView =
        m_registry
            .view<scene::StaticMeshComponent, ecs::WorldTransformComponent, scene::PreviousWorldTransformComponent>();
    for (const entt::entity entity : meshView) {
        const auto& mesh = meshView.get<scene::StaticMeshComponent>(entity);
        const glm::mat4& world = meshView.get<ecs::WorldTransformComponent>(entity).worldMatrix;
        StaticMeshRenderer& renderer = *m_assets[assetIndex(mesh.assetId)].renderer;
        renderer.setInstanceTransform(world, meshView.get<scene::PreviousWorldTransformComponent>(entity).worldMatrix);
        assetDraws.clear();
        renderer.appendTransparentDraws(world, work.positionWorldMeters, assetDraws);
        transparentDraws.reserve(transparentDraws.size() + assetDraws.size());
        for (StaticMeshRenderer::TransparentDraw& draw : assetDraws) {
            transparentDraws.push_back({&renderer, std::move(draw)});
        }
    }
    std::sort(transparentDraws.begin(), transparentDraws.end(), [](const QueuedDraw& lhs, const QueuedDraw& rhs) {
        return lhs.draw.distanceSquared > rhs.draw.distanceSquared;
    });
    for (const QueuedDraw& queued : transparentDraws) {
        queued.renderer->renderReflectionProbeCaptureTransparent(commandList, queued.draw);
    }
    commandList.endRendering();
    return true;
}

void ModelSceneRuntime::invalidateReflectionProbeCapture() {
    m_reflectionProbeRevisionInvalidated = true;
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

RhiTextureHandle ModelSceneRuntime::captureTextureHandle() const {
    return m_deferredRenderer ? m_deferredRenderer->captureTextureHandle() : RhiTextureHandle{};
}

RhiTextureFormat ModelSceneRuntime::captureTextureFormat() const {
    return m_deferredRenderer ? m_deferredRenderer->captureTextureFormat() : RhiTextureFormat::Undefined;
}

RhiTextureHandle ModelSceneRuntime::rtgiRawDiffuseTextureHandle() const {
    return m_deferredRenderer ? m_deferredRenderer->rtgiRawDiffuseTextureHandle() : RhiTextureHandle{};
}

RhiTextureHandle ModelSceneRuntime::nrdDiffuseTextureHandle() const {
    return m_deferredRenderer ? m_deferredRenderer->nrdDiffuseTextureHandle() : RhiTextureHandle{};
}

RhiTextureHandle ModelSceneRuntime::rtgiLeakageNormalTextureHandle() const {
    return m_deferredRenderer ? m_deferredRenderer->rtgiLeakageNormalTextureHandle() : RhiTextureHandle{};
}

RhiTextureHandle ModelSceneRuntime::rtgiLeakageViewZTextureHandle() const {
    return m_deferredRenderer ? m_deferredRenderer->rtgiLeakageViewZTextureHandle() : RhiTextureHandle{};
}

float ModelSceneRuntime::nrdDiffuseToPreExposedScale() const {
    return m_deferredRenderer ? m_deferredRenderer->nrdDiffuseToPreExposedScale() : 1.0f;
}

const GpuFrameStats* ModelSceneRuntime::gpuFrameStats() const {
    return m_deferredRenderer ? m_deferredRenderer->gpuFrameStats() : nullptr;
}

RenderGraphFrameStats ModelSceneRuntime::renderGraphFrameStats() const {
    return m_deferredRenderer ? m_deferredRenderer->renderGraphFrameStats() : RenderGraphFrameStats{};
}

ReflectionProbeCaptureFrameStats ModelSceneRuntime::reflectionProbeCaptureStats() const {
    return m_deferredRenderer ? m_deferredRenderer->reflectionProbeCaptureStats() : ReflectionProbeCaptureFrameStats{};
}

bool ModelSceneRuntime::isAccelerationStructureReady() const {
    return m_deferredRenderer != nullptr && m_deferredRenderer->isAccelerationStructureReady();
}

void ModelSceneRuntime::discardValidationTemporalFrame() {
    if (!m_deferredRenderer) {
        std::abort();
    }
    m_deferredRenderer->discardValidationTemporalFrame();
}

void ModelSceneRuntime::setTimeOfDay(const float timeOfDaySeconds) {
    if (!m_deferredRenderer) {
        std::abort();
    }
    m_deferredRenderer->setTimeOfDay(timeOfDaySeconds);
    invalidateReflectionProbeCapture();
}

void ModelSceneRuntime::setTimePaused(const bool paused) {
    if (!m_deferredRenderer) {
        std::abort();
    }
    m_deferredRenderer->setTimePaused(paused);
    invalidateReflectionProbeCapture();
}

bool ModelSceneRuntime::timePaused() const {
    if (!m_deferredRenderer) {
        std::abort();
    }
    return m_deferredRenderer->timePaused();
}

void ModelSceneRuntime::setTimeScale(const float scale) {
    if (!m_deferredRenderer) {
        std::abort();
    }
    m_deferredRenderer->setTimeScale(scale);
    invalidateReflectionProbeCapture();
}

float ModelSceneRuntime::timeScale() const {
    if (!m_deferredRenderer) {
        std::abort();
    }
    return m_deferredRenderer->timeScale();
}

void ModelSceneRuntime::setWeather(const WeatherType weather, const bool instant) {
    if (!m_deferredRenderer) {
        std::abort();
    }
    m_deferredRenderer->setWeather(weather, instant);
    invalidateReflectionProbeCapture();
}

WeatherType ModelSceneRuntime::weather() const {
    if (!m_deferredRenderer) {
        std::abort();
    }
    return m_deferredRenderer->weather();
}

bool ModelSceneRuntime::weatherTransitionInstant() const {
    if (!m_deferredRenderer) {
        std::abort();
    }
    return m_deferredRenderer->weatherTransitionInstant();
}

float ModelSceneRuntime::timeOfDay() const {
    if (!m_deferredRenderer) {
        std::abort();
    }
    return m_deferredRenderer->timeOfDay();
}

bool ModelSceneRuntime::setRenderSettings(const RenderSettings& settings) {
    if (!m_deferredRenderer) {
        std::abort();
    }
    std::string validationError;
    if (!ModelSceneDeferredRenderer::validateSettings(settings, validationError)) {
        setError(std::move(validationError));
        return false;
    }
    if (settings.upscale.fsr1Enabled && !m_deferredRenderer->isFsr1Supported()) {
        setError("FSR 1 requires the OpenGL graphics backend");
        return false;
    }
    if (settings.upscale.type == TemporalUpscalerType::Fsr31 && !m_deferredRenderer->isFsr31Supported()) {
        setError("FSR 3.1 requires an enabled Vulkan FSR 3.1 build");
        return false;
    }
    m_deferredRenderer->setSettings(settings);
    m_lastError.clear();
    return true;
}

const RenderSettings& ModelSceneRuntime::renderSettings() const {
    if (!m_deferredRenderer) {
        std::abort();
    }
    return m_deferredRenderer->settings();
}

bool ModelSceneRuntime::isFsr1Supported() const {
    return m_deferredRenderer && m_deferredRenderer->isFsr1Supported();
}

bool ModelSceneRuntime::isFsr31Supported() const {
    return m_deferredRenderer && m_deferredRenderer->isFsr31Supported();
}

entt::entity ModelSceneRuntime::pick(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const {
    entt::entity nearestEntity = entt::null;
    float nearestDistance = std::numeric_limits<float>::max();
    const auto view = m_registry.view<scene::PickableComponent, ecs::WorldTransformComponent>();
    for (const entt::entity entity : view) {
        const auto& bounds = view.get<scene::PickableComponent>(entity);
        const glm::mat4& world = view.get<ecs::WorldTransformComponent>(entity).worldMatrix;
        const float determinant = glm::determinant(glm::mat3(world));
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f) {
            continue;
        }
        const glm::mat4 inverseWorld = glm::inverse(world);
        const glm::vec3 localOrigin = glm::vec3(inverseWorld * glm::vec4(rayOrigin, 1.0f));
        const glm::vec3 localDirection = glm::vec3(inverseWorld * glm::vec4(rayDirection, 0.0f));
        float distance = 0.0f;
        if (intersectLocalBounds(localOrigin, localDirection, bounds.localBoundsMin, bounds.localBoundsMax, distance) &&
            distance < nearestDistance) {
            nearestDistance = distance;
            nearestEntity = entity;
        }
    }
    return nearestEntity;
}

bool ModelSceneRuntime::entityWorldBounds(const entt::entity entity, glm::vec3& boundsMin, glm::vec3& boundsMax) const {
    if (!m_registry.valid(entity) || !m_registry.all_of<ecs::WorldTransformComponent, ecs::ChildrenComponent>(entity)) {
        return false;
    }

    std::vector<entt::entity> entities{entity};
    std::unordered_set<entt::entity> visited;
    bool initialized = false;
    const auto includePoint = [&](const glm::vec3& point) {
        if (!initialized) {
            boundsMin = point;
            boundsMax = point;
            initialized = true;
            return;
        }
        boundsMin = glm::min(boundsMin, point);
        boundsMax = glm::max(boundsMax, point);
    };
    for (std::size_t index = 0u; index < entities.size(); ++index) {
        const entt::entity current = entities[index];
        if (!m_registry.valid(current) ||
            !m_registry.all_of<ecs::WorldTransformComponent, ecs::ChildrenComponent>(current) ||
            !visited.insert(current).second) {
            return false;
        }
        const glm::mat4& world = m_registry.get<ecs::WorldTransformComponent>(current).worldMatrix;
        includePoint(glm::vec3(world[3]));
        if (const auto* pickable = m_registry.try_get<scene::PickableComponent>(current)) {
            for (uint32_t corner = 0u; corner < 8u; ++corner) {
                const glm::vec3 local{(corner & 1u) != 0u ? pickable->localBoundsMax.x : pickable->localBoundsMin.x,
                                      (corner & 2u) != 0u ? pickable->localBoundsMax.y : pickable->localBoundsMin.y,
                                      (corner & 4u) != 0u ? pickable->localBoundsMax.z : pickable->localBoundsMin.z};
                includePoint(glm::vec3(world * glm::vec4(local, 1.0f)));
            }
        }
        const auto& children = m_registry.get<ecs::ChildrenComponent>(current).children;
        for (const entt::entity child : children) {
            const auto* parent = m_registry.try_get<ecs::ParentComponent>(child);
            if (parent == nullptr || parent->parent != current) {
                return false;
            }
            entities.push_back(child);
        }
    }
    return initialized;
}

void ModelSceneRuntime::syncTransforms() {
    if (!syncRegistryTransforms(m_registry)) {
        std::abort();
    }
}

scene::ModelSceneDocument
ModelSceneRuntime::captureDocument(const scene::SceneEditorCameraDocument& editorCamera) const {
    if (!m_deferredRenderer) {
        std::abort();
    }
    scene::ModelSceneDocument document;
    document.assets.reserve(m_assets.size());
    for (const MeshAsset& asset : m_assets) {
        document.assets.push_back({asset.id, asset.name, asset.path});
    }

    const auto entities =
        m_registry.view<scene::SceneEntityIdComponent, scene::NameComponent, ecs::LocalTransformComponent>();
    document.entities.reserve(entities.size_hint());
    for (const entt::entity entity : entities) {
        scene::SceneEntityDocument entry;
        entry.id = entities.get<scene::SceneEntityIdComponent>(entity).value;
        entry.name = entities.get<scene::NameComponent>(entity).value;
        const auto& transform = entities.get<ecs::LocalTransformComponent>(entity);
        entry.transform.position = transform.localPosition;
        entry.transform.rotation = transform.localRotation;
        entry.transform.scale = transform.localScale;
        if (const auto* parent = m_registry.try_get<ecs::ParentComponent>(entity)) {
            const scene::SceneEntityId parentId = this->entityId(parent->parent);
            if (parentId == scene::kInvalidSceneEntityId) {
                std::abort();
            }
            entry.parentId = parentId;
        }
        if (const auto* mesh = m_registry.try_get<scene::StaticMeshComponent>(entity)) {
            if (m_assetIndices.find(mesh->assetId) == m_assetIndices.end()) {
                std::abort();
            }
            entry.assetId = mesh->assetId;
        }
        document.entities.push_back(std::move(entry));
    }
    std::sort(
        document.entities.begin(), document.entities.end(),
        [](const scene::SceneEntityDocument& lhs, const scene::SceneEntityDocument& rhs) { return lhs.id < rhs.id; });
    document.reflectionProbes.reserve(m_reflectionProbes.size());
    for (const RuntimeReflectionProbe& probe : m_reflectionProbes) {
        document.reflectionProbes.push_back(probe.document);
    }
    std::sort(document.reflectionProbes.begin(), document.reflectionProbes.end(),
              [](const scene::SceneReflectionProbeDocument& lhs, const scene::SceneReflectionProbeDocument& rhs) {
                  return lhs.id < rhs.id;
              });
    document.environment.timeOfDay = timeOfDay();
    document.environment.timePaused = timePaused();
    document.environment.timeScale = timeScale();
    document.environment.weather = weather();
    document.environment.weatherTransitionInstant = weatherTransitionInstant();
    document.environment.renderSettings = renderSettings();
    document.editorCamera = editorCamera;
    return document;
}

void ModelSceneRuntime::setError(std::string message) {
    m_lastError = std::move(message);
}
