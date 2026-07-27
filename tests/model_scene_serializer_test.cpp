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
    document.environment.renderSettings =
        ModelSceneDeferredRenderer::defaultSettings();
    document.assets.push_back({
        7u, "Helmet", "/tmp/assets/DamagedHelmet.glb"});

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
    RenderSettings modelSettings =
        ModelSceneDeferredRenderer::defaultSettings();
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

    const nlohmann::json encoded =
        scene::ModelSceneSerializer::serialize(source);
    scene::ModelSceneDocument decoded;
    if (!scene::ModelSceneSerializer::deserialize(encoded, decoded, error)) {
        return fail("serialized document did not deserialize: " + error);
    }
    if (decoded.format != scene::ModelSceneDocument::kFormat ||
        decoded.version != scene::ModelSceneDocument::kCurrentVersion ||
        decoded.assets.size() != 1u || decoded.entities.size() != 2u ||
        decoded.entities[1].parentId != source.entities[0].id ||
        decoded.entities[1].assetId != source.assets[0].id ||
        !near(decoded.environment.timeOfDay, 725.0f) ||
        decoded.environment.timePaused ||
        !near(decoded.environment.timeScale, 2.5f) ||
        decoded.environment.weather != WeatherType::Storm ||
        decoded.environment.weatherTransitionInstant ||
        decoded.environment.renderSettings.shadow.resolution != 4096 ||
        !near(decoded.environment.renderSettings.ssao.strength, 0.42f) ||
        !near(decoded.environment.renderSettings.postProcess.saturation, 1.25f) ||
        !near(decoded.editorCamera.distance, 12.0f) ||
        !near(decoded.editorCamera.nearPlane, 0.025f) ||
        !near(decoded.editorCamera.farPlane, 2500.0f)) {
        return fail("JSON round trip changed stable scene data");
    }

    nlohmann::json versionOne = encoded;
    versionOne["version"] = 1u;
    versionOne["environment"].erase("timePaused");
    versionOne["environment"].erase("timeScale");
    versionOne["environment"].erase("weather");
    versionOne["environment"].erase("weatherTransitionInstant");
    versionOne["editorCamera"].erase("nearPlane");
    versionOne["editorCamera"].erase("farPlane");
    if (!scene::ModelSceneSerializer::deserialize(
            versionOne, decoded, error) ||
        decoded.version != scene::ModelSceneDocument::kCurrentVersion ||
        !decoded.environment.timePaused ||
        !near(decoded.environment.timeScale, 1.0f) ||
        decoded.environment.weather != WeatherType::Clear ||
        !decoded.environment.weatherTransitionInstant ||
        !near(decoded.editorCamera.nearPlane, 0.05f) ||
        !near(decoded.editorCamera.farPlane, 500.0f)) {
        return fail("version 1 scene migration did not apply version 2 defaults");
    }

    nlohmann::json incompleteSettings = encoded;
    incompleteSettings["environment"]["renderSettings"]["shadow"].erase(
        "resolution");
    if (scene::ModelSceneSerializer::deserialize(
            incompleteSettings, decoded, error)) {
        return fail("missing render setting was accepted");
    }
    nlohmann::json fractionalInteger = encoded;
    fractionalInteger["environment"]["renderSettings"]["shadow"]["resolution"] =
        2048.5;
    if (scene::ModelSceneSerializer::deserialize(
            fractionalInteger, decoded, error)) {
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

    const auto unique = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("mecraft-model-scene-" + std::to_string(unique) + ".scene");
    if (!scene::ModelSceneSerializer::saveToFile(
            path.string(), source, error) ||
        !scene::ModelSceneSerializer::saveToFile(
            path.string(), source, error)) {
        return fail("atomic scene save failed: " + error);
    }
    std::ifstream savedInput(path);
    const nlohmann::json savedJson =
        nlohmann::json::parse(savedInput, nullptr, false);
    if (savedJson.is_discarded() ||
        std::filesystem::u8path(savedJson["assets"][0]["path"].get<std::string>())
            .is_absolute()) {
        return fail("scene file did not store a portable relative asset path");
    }
    scene::ModelSceneDocument loaded;
    if (!scene::ModelSceneSerializer::loadFromFile(
            path.string(), loaded, error)) {
        return fail("saved scene could not be loaded: " + error);
    }
    if (loaded.entities.size() != source.entities.size() ||
        loaded.entities[1].name != source.entities[1].name ||
        loaded.assets[0].path != source.assets[0].path) {
        return fail("file round trip changed scene entities");
    }

    std::error_code filesystemError;
    std::filesystem::remove(path, filesystemError);
    std::filesystem::remove(path.string() + ".bak", filesystemError);
    std::filesystem::remove(path.string() + ".tmp", filesystemError);

    std::cout << "[model_scene_serializer_test] PASS\n";
    return EXIT_SUCCESS;
}
