#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "scene/ModelSceneSerializer.h"
#include "scene/ModelSceneDeferredRenderer.h"
#include "renderer/contracts/ReflectionProbeContract.h"

namespace {

int fail(const std::string& message) {
    std::cerr << "[model_scene_serializer_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool near(const float lhs, const float rhs) {
    return std::abs(lhs - rhs) <= 1e-5f;
}

scene::ModelSceneDocument makeDocument() {
    scene::ModelSceneDocument document;
    document.environment.renderSettings = ModelSceneDeferredRenderer::defaultSettings();
    document.assets.push_back({7u, "Helmet", "/tmp/assets/DamagedHelmet.glb"});

    scene::SceneEntityDocument root;
    root.id = 11u;
    root.name = "Root";
    root.transform.position = {1.0f, 2.0f, 3.0f};
    document.entities.push_back(root);

    scene::SceneEntityDocument child;
    child.id = 19u;
    child.name = "Helmet Instance";
    child.parentId = root.id;
    child.assetId = document.assets.front().id;
    child.transform.rotation = {10.0f, 20.0f, 30.0f};
    child.transform.scale = {1.5f, 1.5f, 1.5f};
    document.entities.push_back(child);

    scene::SceneReflectionProbeDocument probe;
    probe.id = 23u;
    probe.position = {1.0f, 2.0f, 3.0f};
    probe.influenceMin = {-2.0f, -1.0f, 0.0f};
    probe.influenceMax = {4.0f, 5.0f, 6.0f};
    probe.boxProjectionMin = {-3.0f, -2.0f, -1.0f};
    probe.boxProjectionMax = {5.0f, 6.0f, 7.0f};
    probe.blendDistance = 1.5f;
    probe.exposureScale = 1.25f;
    document.reflectionProbes.push_back(probe);

    document.environment.timeOfDay = 725.0f;
    document.environment.timePaused = false;
    document.environment.timeScale = 2.5f;
    document.environment.weather = WeatherType::Storm;
    document.environment.weatherTransitionInstant = false;
    document.environment.renderSettings.shadow.resolution = 4096;
    document.environment.renderSettings.ssao.strength = 0.42f;
    document.environment.renderSettings.postProcess.saturation = 1.25f;
    document.editorCamera.target = {4.0f, 5.0f, 6.0f};
    document.editorCamera.distance = 12.0f;
    document.editorCamera.yaw = -45.0f;
    document.editorCamera.pitch = 25.0f;
    document.editorCamera.nearPlane = 0.025f;
    document.editorCamera.farPlane = 2500.0f;
    return document;
}

} // namespace

int main() {
    RenderSettings modelSettings = ModelSceneDeferredRenderer::defaultSettings();
    std::string error;
    if (!ModelSceneDeferredRenderer::validateSettings(modelSettings, error)) {
        return fail("default model scene settings were rejected: " + error);
    }
    modelSettings.weather.particlesEnabled = true;
    if (ModelSceneDeferredRenderer::validateSettings(modelSettings, error)) {
        return fail("gameplay particle settings were accepted for a model scene");
    }

    const scene::ModelSceneDocument source = makeDocument();
    if (!scene::ModelSceneSerializer::validate(source, error)) {
        return fail("valid source document was rejected: " + error);
    }

    const nlohmann::json encoded = scene::ModelSceneSerializer::serialize(source);
    scene::ModelSceneDocument decoded;
    if (!scene::ModelSceneSerializer::deserialize(encoded, decoded, error)) {
        return fail("serialized document did not deserialize: " + error);
    }
    if (decoded.format != scene::ModelSceneDocument::kFormat ||
        decoded.version != scene::ModelSceneDocument::kCurrentVersion || decoded.assets.size() != 1u ||
        decoded.entities.size() != 2u || decoded.reflectionProbes.size() != 1u ||
        decoded.entities[1].parentId != source.entities[0].id || decoded.entities[1].assetId != source.assets[0].id ||
        !near(decoded.environment.timeOfDay, 725.0f) || decoded.environment.timePaused ||
        !near(decoded.environment.timeScale, 2.5f) || decoded.environment.weather != WeatherType::Storm ||
        decoded.environment.weatherTransitionInstant || decoded.environment.renderSettings.shadow.resolution != 4096 ||
        !near(decoded.environment.renderSettings.ssao.strength, 0.42f) ||
        !near(decoded.environment.renderSettings.postProcess.saturation, 1.25f) ||
        !near(decoded.editorCamera.distance, 12.0f) || !near(decoded.editorCamera.nearPlane, 0.025f) ||
        !near(decoded.editorCamera.farPlane, 2500.0f) || decoded.reflectionProbes[0].id != 23u ||
        !near(decoded.reflectionProbes[0].position.y, 2.0f) || !near(decoded.reflectionProbes[0].blendDistance, 1.5f) ||
        !near(decoded.reflectionProbes[0].exposureScale, 1.25f)) {
        return fail("JSON round trip changed stable scene data");
    }

    nlohmann::json oldVersion = encoded;
    oldVersion["version"] = 2u;
    if (scene::ModelSceneSerializer::deserialize(oldVersion, decoded, error)) {
        return fail("obsolete scene version was accepted");
    }
    nlohmann::json missingProbes = encoded;
    missingProbes.erase("reflectionProbes");
    if (scene::ModelSceneSerializer::deserialize(missingProbes, decoded, error)) {
        return fail("missing reflection-probe array was accepted");
    }

    nlohmann::json incompleteSettings = encoded;
    incompleteSettings["environment"]["renderSettings"]["shadow"].erase("resolution");
    if (scene::ModelSceneSerializer::deserialize(incompleteSettings, decoded, error)) {
        return fail("missing render setting was accepted");
    }
    nlohmann::json fractionalInteger = encoded;
    fractionalInteger["environment"]["renderSettings"]["shadow"]["resolution"] = 2048.5;
    if (scene::ModelSceneSerializer::deserialize(fractionalInteger, decoded, error)) {
        return fail("fractional integer render setting was accepted");
    }

    scene::ModelSceneDocument duplicate = source;
    duplicate.entities[1].id = duplicate.entities[0].id;
    if (scene::ModelSceneSerializer::validate(duplicate, error)) {
        return fail("duplicate entity ID was accepted");
    }

    scene::ModelSceneDocument dangling = source;
    dangling.entities[1].assetId = 999u;
    if (scene::ModelSceneSerializer::validate(dangling, error)) {
        return fail("unknown asset reference was accepted");
    }

    scene::ModelSceneDocument cyclic = source;
    cyclic.entities[0].parentId = cyclic.entities[1].id;
    if (scene::ModelSceneSerializer::validate(cyclic, error)) {
        return fail("cyclic entity hierarchy was accepted");
    }

    scene::ModelSceneDocument duplicateProbe = source;
    duplicateProbe.reflectionProbes.push_back(source.reflectionProbes.front());
    if (scene::ModelSceneSerializer::validate(duplicateProbe, error)) {
        return fail("duplicate reflection-probe ID was accepted");
    }
    scene::ModelSceneDocument invalidProbe = source;
    invalidProbe.reflectionProbes.front().position = invalidProbe.reflectionProbes.front().influenceMax;
    if (scene::ModelSceneSerializer::validate(invalidProbe, error)) {
        return fail("reflection-probe position on the influence boundary was accepted");
    }
    scene::ModelSceneDocument excessiveProbes = source;
    excessiveProbes.reflectionProbes.clear();
    for (uint32_t index = 0u; index <= renderer::contracts::kReflectionProbeCaptureMaxProbeCount; ++index) {
        scene::SceneReflectionProbeDocument probe = source.reflectionProbes.front();
        probe.id = index + 1u;
        excessiveProbes.reflectionProbes.push_back(probe);
    }
    if (scene::ModelSceneSerializer::validate(excessiveProbes, error)) {
        return fail("reflection-probe capture capacity overflow was accepted");
    }

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("mecraft-model-scene-" + std::to_string(unique) + ".scene");
    if (!scene::ModelSceneSerializer::saveToFile(path.string(), source, error) ||
        !scene::ModelSceneSerializer::saveToFile(path.string(), source, error)) {
        return fail("atomic scene save failed: " + error);
    }
    std::ifstream savedInput(path);
    const nlohmann::json savedJson = nlohmann::json::parse(savedInput, nullptr, false);
    if (savedJson.is_discarded() ||
        std::filesystem::u8path(savedJson["assets"][0]["path"].get<std::string>()).is_absolute()) {
        return fail("scene file did not store a portable relative asset path");
    }
    scene::ModelSceneDocument loaded;
    if (!scene::ModelSceneSerializer::loadFromFile(path.string(), loaded, error)) {
        return fail("saved scene could not be loaded: " + error);
    }
    if (loaded.entities.size() != source.entities.size() ||
        loaded.reflectionProbes.size() != source.reflectionProbes.size() ||
        loaded.entities[1].name != source.entities[1].name || loaded.assets[0].path != source.assets[0].path ||
        !near(loaded.reflectionProbes[0].boxProjectionMax.z, 7.0f)) {
        return fail("file round trip changed scene entities");
    }

    std::error_code filesystemError;
    std::filesystem::remove(path, filesystemError);
    std::filesystem::remove(path.string() + ".bak", filesystemError);
    std::filesystem::remove(path.string() + ".tmp", filesystemError);

    std::cout << "[model_scene_serializer_test] PASS\n";
    return EXIT_SUCCESS;
}
