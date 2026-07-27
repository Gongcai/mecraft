#ifndef MECRAFT_MODEL_SCENE_DOCUMENT_H
#define MECRAFT_MODEL_SCENE_DOCUMENT_H

#include "ModelSceneIds.h"
#include "renderer/core/RenderSettings.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace scene {

struct SceneTransformDocument {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    glm::vec3 scale{1.0f};
};

struct SceneAssetDocument {
    SceneAssetId id = kInvalidSceneAssetId;
    std::string name;
    std::string path;
};

struct SceneEntityDocument {
    SceneEntityId id = kInvalidSceneEntityId;
    std::string name;
    std::optional<SceneEntityId> parentId;
    std::optional<SceneAssetId> assetId;
    SceneTransformDocument transform;
};

struct SceneEnvironmentDocument {
    float timeOfDay = 300.0f;
    RenderSettings renderSettings;
};

struct SceneEditorCameraDocument {
    glm::vec3 target{0.0f};
    float distance = 5.0f;
    float yaw = 35.0f;
    float pitch = 18.0f;
};

struct ModelSceneDocument {
    static constexpr uint32_t kCurrentVersion = 1u;
    static constexpr const char* kFormat = "mecraft.scene";

    std::string format = kFormat;
    uint32_t version = kCurrentVersion;
    std::vector<SceneAssetDocument> assets;
    std::vector<SceneEntityDocument> entities;
    SceneEnvironmentDocument environment;
    SceneEditorCameraDocument editorCamera;
};

} // namespace scene

#endif // MECRAFT_MODEL_SCENE_DOCUMENT_H
