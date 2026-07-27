#ifndef MECRAFT_MODEL_SCENE_COMPONENTS_H
#define MECRAFT_MODEL_SCENE_COMPONENTS_H

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "ModelSceneIds.h"

namespace scene {

struct NameComponent {
    std::string value;
};

struct SceneEntityIdComponent {
    SceneEntityId value = kInvalidSceneEntityId;
};

struct StaticMeshComponent {
    SceneAssetId assetId = kInvalidSceneAssetId;
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
