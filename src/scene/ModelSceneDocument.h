#ifndef MECRAFT_MODEL_SCENE_DOCUMENT_H
#define MECRAFT_MODEL_SCENE_DOCUMENT_H

#include "ModelSceneIds.h"
#include "renderer/contracts/GpuLightContract.h"
#include "renderer/core/RenderSettings.h"
#include "world/WeatherSystem.h"

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

/// Stores editable Point-light properties owned by a model-scene entity.
/// The owning entity's world transform determines the light position.
struct SceneManualPointLightDocument {
    glm::vec3 colorLinear{1.0f};
    float intensityCandela = 800.0f;
    float rangeMeters = 8.0f;
    float emitterRadiusMeters = 0.1f;
    float selfShadowRadiusMeters = 0.0f;
    renderer::contracts::GpuLightShadowPolicy shadowPolicy =
        renderer::contracts::GpuLightShadowPolicy::RasterDynamic;
};

struct SceneEntityDocument {
    SceneEntityId id = kInvalidSceneEntityId;
    std::string name;
    std::optional<SceneEntityId> parentId;
    std::optional<SceneAssetId> assetId;
    std::optional<SceneManualPointLightDocument> manualPointLight;
    SceneTransformDocument transform;
};

struct SceneEnvironmentDocument {
    float timeOfDay = 300.0f;
    bool timePaused = true;
    float timeScale = 1.0f;
    WeatherType weather = WeatherType::Clear;
    bool weatherTransitionInstant = true;
    RenderSettings renderSettings;
};

struct SceneEditorCameraDocument {
    glm::vec3 target{0.0f};
    float distance = 5.0f;
    float yaw = 35.0f;
    float pitch = 18.0f;
    float nearPlane = 0.05f;
    float farPlane = 500.0f;
};

struct SceneReflectionProbeDocument {
    SceneReflectionProbeId id = kInvalidSceneReflectionProbeId;
    glm::vec3 position{0.0f};
    glm::vec3 influenceMin{-1.0f};
    glm::vec3 influenceMax{1.0f};
    glm::vec3 boxProjectionMin{-1.0f};
    glm::vec3 boxProjectionMax{1.0f};
    float blendDistance = 0.25f;
    float exposureScale = 1.0f;
};

struct ModelSceneDocument {
    static constexpr uint32_t kCurrentVersion = 4u;
    static constexpr const char* kFormat = "mecraft.scene";

    std::string format = kFormat;
    uint32_t version = kCurrentVersion;
    std::vector<SceneAssetDocument> assets;
    std::vector<SceneEntityDocument> entities;
    std::vector<SceneReflectionProbeDocument> reflectionProbes;
    SceneEnvironmentDocument environment;
    SceneEditorCameraDocument editorCamera;
};

} // namespace scene

#endif // MECRAFT_MODEL_SCENE_DOCUMENT_H
