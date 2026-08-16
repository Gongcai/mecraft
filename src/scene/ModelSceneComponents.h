#ifndef MECRAFT_MODEL_SCENE_COMPONENTS_H
#define MECRAFT_MODEL_SCENE_COMPONENTS_H

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ModelSceneIds.h"
#include "renderer/contracts/SceneIdentityContract.h"

namespace scene {

struct NameComponent {
    std::string value;
};

struct SceneEntityIdComponent {
    SceneEntityId value = kInvalidSceneEntityId;
};

struct StableObjectIdComponent {
    renderer::contracts::StableObjectId value;
};

struct StaticMeshComponent {
    SceneAssetId assetId = kInvalidSceneAssetId;
};

/// Owns one stable light identity for every punctual light in a mesh asset.
/// The vector is runtime-only and follows the visible entity lifetime.
struct StaticMeshLightIdentityComponent {
    std::vector<renderer::contracts::StableLightId> values;
};

/// Keeps one editable Point-light payload and its stable GPU-light identity on a scene entity.
struct ManualPointLightComponent {
    renderer::contracts::StableLightId stableId;
    glm::vec3 colorLinear{1.0f};
    float intensityCandela = 800.0f;
    float rangeMeters = 8.0f;
    float emitterRadiusMeters = 0.1f;
    float selfShadowRadiusMeters = 0.0f;
    renderer::contracts::GpuLightShadowPolicy shadowPolicy =
        renderer::contracts::GpuLightShadowPolicy::RasterDynamic;
};

struct PreviousWorldTransformComponent {
    glm::mat4 worldMatrix{1.0f};
};

struct PickableComponent {
    glm::vec3 localBoundsMin{0.0f};
    glm::vec3 localBoundsMax{0.0f};
};

} // namespace scene

#endif // MECRAFT_MODEL_SCENE_COMPONENTS_H
