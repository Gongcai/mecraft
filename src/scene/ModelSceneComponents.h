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

struct PreviousWorldTransformComponent {
    glm::mat4 worldMatrix{1.0f};
};

struct PickableComponent {
    glm::vec3 localBoundsMin{0.0f};
    glm::vec3 localBoundsMax{0.0f};
};

} // namespace scene

#endif // MECRAFT_MODEL_SCENE_COMPONENTS_H
