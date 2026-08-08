#include "app/validation/ValidationSceneContract.h"

#include "app/AppSettings.h"
#include "app/validation/ValidationVoxelFixture.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <utility>

namespace app::validation {
namespace {

using Json = nlohmann::json;
using renderer::contracts::StableContentHash;
using renderer::contracts::StableContentHashBuilder;

constexpr size_t kMaximumIdentifierLength = 128u;
constexpr int kMinimumRenderDistance = 2;
constexpr int kMaximumRenderDistance = 32;
constexpr double kWorldDaySeconds = 1200.0;

[[nodiscard]] bool validIdentifier(const std::string_view identifier) {
    if (identifier.empty() || identifier.size() > kMaximumIdentifierLength || identifier.front() < 'a' ||
        identifier.front() > 'z') {
        return false;
    }
    return std::all_of(identifier.begin(), identifier.end(), [](const char value) {
        return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_' || value == '-' ||
               value == '.';
    });
}

[[nodiscard]] bool expectedField(const std::string_view name, const std::initializer_list<const char*> expectedFields) {
    return std::any_of(expectedFields.begin(), expectedFields.end(),
                       [name](const char* field) { return name == field; });
}

[[nodiscard]] ValidationSceneContractError validateFields(const Json& object,
                                                          const std::initializer_list<const char*> expectedFields,
                                                          const std::string_view objectPath, std::string& detail) {
    for (const char* field : expectedFields) {
        if (object.find(field) == object.end()) {
            detail = std::string(objectPath) + "." + field;
            return ValidationSceneContractError::MissingField;
        }
    }
    if (object.size() == expectedFields.size()) {
        return ValidationSceneContractError::None;
    }
    for (auto field = object.begin(); field != object.end(); ++field) {
        if (!expectedField(field.key(), expectedFields)) {
            detail = std::string(objectPath) + "." + field.key();
            return ValidationSceneContractError::UnexpectedField;
        }
    }
    detail = std::string(objectPath);
    return ValidationSceneContractError::UnexpectedField;
}

[[nodiscard]] bool readString(const Json& object, const char* field, const std::string_view fieldPath,
                              std::string& output, ValidationSceneContractLoadResult& result) {
    const Json& value = *object.find(field);
    if (!value.is_string()) {
        result.error = ValidationSceneContractError::InvalidFieldType;
        result.detail = fieldPath;
        return false;
    }
    output = value.get_ref<const std::string&>();
    return true;
}

[[nodiscard]] bool readUint32(const Json& object, const char* field, const std::string_view fieldPath, uint32_t& output,
                              ValidationSceneContractLoadResult& result) {
    const Json& value = *object.find(field);
    uint64_t parsed = 0u;
    if (value.is_number_unsigned()) {
        parsed = value.get<uint64_t>();
    } else if (value.is_number_integer()) {
        const int64_t signedValue = value.get<int64_t>();
        if (signedValue < 0) {
            result.error = ValidationSceneContractError::InvalidFieldType;
            result.detail = fieldPath;
            return false;
        }
        parsed = static_cast<uint64_t>(signedValue);
    } else {
        result.error = ValidationSceneContractError::InvalidFieldType;
        result.detail = fieldPath;
        return false;
    }
    if (parsed > std::numeric_limits<uint32_t>::max()) {
        result.error = ValidationSceneContractError::InvalidFieldType;
        result.detail = fieldPath;
        return false;
    }
    output = static_cast<uint32_t>(parsed);
    return true;
}

[[nodiscard]] bool readInt(const Json& object, const char* field, const std::string_view fieldPath, int& output,
                           ValidationSceneContractLoadResult& result) {
    const Json& value = *object.find(field);
    if (!value.is_number_integer()) {
        result.error = ValidationSceneContractError::InvalidFieldType;
        result.detail = fieldPath;
        return false;
    }
    const int64_t parsed = value.get<int64_t>();
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        result.error = ValidationSceneContractError::InvalidFieldType;
        result.detail = fieldPath;
        return false;
    }
    output = static_cast<int>(parsed);
    return true;
}

[[nodiscard]] bool readDouble(const Json& object, const char* field, const std::string_view fieldPath, double& output,
                              ValidationSceneContractLoadResult& result) {
    const Json& value = *object.find(field);
    if (!value.is_number()) {
        result.error = ValidationSceneContractError::InvalidFieldType;
        result.detail = fieldPath;
        return false;
    }
    output = value.get<double>();
    if (!std::isfinite(output)) {
        result.error = ValidationSceneContractError::InvalidFieldType;
        result.detail = fieldPath;
        return false;
    }
    return true;
}

[[nodiscard]] bool readHash(const Json& object, const char* field, const std::string_view fieldPath,
                            StableContentHash& output, ValidationSceneContractLoadResult& result) {
    std::string text;
    if (!readString(object, field, fieldPath, text, result)) {
        return false;
    }
    if (!renderer::contracts::parseStableContentHashHex(text, output)) {
        result.error = ValidationSceneContractError::InvalidContentHash;
        result.detail = fieldPath;
        return false;
    }
    return true;
}

[[nodiscard]] bool readRelativePath(const Json& object, const char* field, const std::string_view fieldPath,
                                    const std::filesystem::path& baseDirectory, std::filesystem::path& source,
                                    std::filesystem::path& resolved, ValidationSceneContractLoadResult& result) {
    std::string text;
    if (!readString(object, field, fieldPath, text, result)) {
        return false;
    }
    source = std::filesystem::path(text).lexically_normal();
    if (source.empty() || source.is_absolute() || source == ".") {
        result.error = ValidationSceneContractError::InvalidPath;
        result.detail = fieldPath;
        return false;
    }
    resolved = (baseDirectory / source).lexically_normal();
    return true;
}

[[nodiscard]] StableContentHash renderSettingsContentHash(const RenderSettings& settings) {
    const std::string serialized = app::serializeRenderSettings(settings).dump();
    return renderer::contracts::stableContentHashBytes(serialized.data(), serialized.size());
}

void configureCommonValidationSettings(RenderSettings& settings) {
    settings.pipelineMode = PipelineMode::Deferred;
    // Keep the versioned reference captures on their locked NRD profile.
    // Runtime defaults may evolve independently of capture identity.
    settings.nrd.disocclusionThreshold = 0.01f;
    settings.ssao.asyncComputeEnabled = false;
    settings.cloud.asyncComputeEnabled = false;
    settings.cloud.updateInterval = 1;
    settings.volumetric.updateInterval = 1;
    settings.shadow.farCascadeInterleaved = false;
    settings.renderGraph.multithreadedRecordEnabled = false;
    settings.postProcess.motionBlurEnabled = false;
    settings.postProcess.dofEnabled = false;
    settings.upscale.type = TemporalUpscalerType::Native;
    settings.upscale.quality = TemporalUpscaleQuality::Native;
    settings.upscale.outputWidth = 0u;
    settings.upscale.outputHeight = 0u;
    settings.upscale.dynamicResolutionEnabled = false;
    settings.upscale.debugVisualizationEnabled = false;
    settings.upscale.fsr1Enabled = false;
    settings.nvidia.frameGeneration = FrameGenerationType::Disabled;
    settings.nvidia.reflexMode = ReflexLowLatencyMode::Off;
    settings.debug = {};
}

[[nodiscard]] ValidationSceneContractLoadResult parseRoot(const Json& root, const std::filesystem::path& sourcePath) {
    ValidationSceneContractLoadResult result;
    if (!root.is_object()) {
        result.error = ValidationSceneContractError::InvalidRoot;
        result.detail = "root";
        return result;
    }
    for (const char* field :
         {"kind", "version", "id", "scene", "content_hash", "camera_path", "render_settings", "environment"}) {
        if (root.find(field) == root.end()) {
            result.error = ValidationSceneContractError::MissingField;
            result.detail = std::string("root.") + field;
            return result;
        }
    }

    ValidationSceneContract& contract = result.contract;
    contract.sourcePath = sourcePath.lexically_normal();
    if (!readUint32(root, "version", "root.version", contract.version, result)) {
        return result;
    }
    if (contract.version != kValidationSceneContractVersion1 && contract.version != kValidationSceneContractVersion) {
        result.error = ValidationSceneContractError::UnsupportedVersion;
        result.detail = "root.version";
        return result;
    }

    std::string sceneText;
    if (!readString(root, "scene", "root.scene", sceneText, result)) {
        return result;
    }
    const std::optional<ValidationScene> scene = parseValidationScene(sceneText);
    if (!scene.has_value()) {
        result.error = ValidationSceneContractError::InvalidScene;
        result.detail = "root.scene";
        return result;
    }
    contract.scene = *scene;
    const auto expectedRootFields =
        *scene == ValidationScene::Voxel ? std::initializer_list<const char*>{"kind",
                                                                              "version",
                                                                              "id",
                                                                              "scene",
                                                                              "content_hash",
                                                                              "camera_path",
                                                                              "render_settings",
                                                                              "environment",
                                                                              "voxel_world"}
        : contract.version == kValidationSceneContractVersion1
            ? std::initializer_list<const char*>{"kind",
                                                 "version",
                                                 "id",
                                                 "scene",
                                                 "content_hash",
                                                 "camera_path",
                                                 "render_settings",
                                                 "environment",
                                                 "model_asset"}
            : std::initializer_list<const char*>{
                  "kind",        "version",         "id",          "scene",       "content_hash",
                  "camera_path", "render_settings", "environment", "model_asset", "reflection_probe_grid"};
    result.error = validateFields(root, expectedRootFields, "root", result.detail);
    if (result.error != ValidationSceneContractError::None) {
        return result;
    }

    std::string kind;
    if (!readString(root, "kind", "root.kind", kind, result) ||
        !readString(root, "id", "root.id", contract.id, result) ||
        !readHash(root, "content_hash", "root.content_hash", contract.contentHash, result)) {
        return result;
    }
    if (kind != kValidationSceneContractKind) {
        result.error = ValidationSceneContractError::InvalidKind;
        result.detail = "root.kind";
        return result;
    }
    if (!validIdentifier(contract.id)) {
        result.error = ValidationSceneContractError::InvalidIdentifier;
        result.detail = "root.id";
        return result;
    }

    const std::filesystem::path baseDirectory = contract.sourcePath.parent_path();
    const Json& camera = *root.find("camera_path");
    if (!camera.is_object()) {
        result.error = ValidationSceneContractError::InvalidFieldType;
        result.detail = "root.camera_path";
        return result;
    }
    result.error = validateFields(camera, {"source", "id", "content_hash"}, "root.camera_path", result.detail);
    if (result.error != ValidationSceneContractError::None ||
        !readRelativePath(camera, "source", "root.camera_path.source", baseDirectory, contract.cameraPath.source,
                          contract.cameraPath.resolvedPath, result) ||
        !readString(camera, "id", "root.camera_path.id", contract.cameraPath.id, result) ||
        !readHash(camera, "content_hash", "root.camera_path.content_hash", contract.cameraPath.contentHash, result)) {
        return result;
    }
    if (!validIdentifier(contract.cameraPath.id)) {
        result.error = ValidationSceneContractError::InvalidIdentifier;
        result.detail = "root.camera_path.id";
        return result;
    }

    const Json& renderSettings = *root.find("render_settings");
    if (!renderSettings.is_object()) {
        result.error = ValidationSceneContractError::InvalidFieldType;
        result.detail = "root.render_settings";
        return result;
    }
    result.error =
        validateFields(renderSettings, {"id", "version", "content_hash"}, "root.render_settings", result.detail);
    if (result.error != ValidationSceneContractError::None ||
        !readString(renderSettings, "id", "root.render_settings.id", contract.renderSettings.id, result) ||
        !readUint32(renderSettings, "version", "root.render_settings.version", contract.renderSettings.version,
                    result) ||
        !readHash(renderSettings, "content_hash", "root.render_settings.content_hash",
                  contract.renderSettings.contentHash, result)) {
        return result;
    }
    if (!validIdentifier(contract.renderSettings.id)) {
        result.error = ValidationSceneContractError::InvalidIdentifier;
        result.detail = "root.render_settings.id";
        return result;
    }

    const Json& environment = *root.find("environment");
    if (!environment.is_object()) {
        result.error = ValidationSceneContractError::InvalidFieldType;
        result.detail = "root.environment";
        return result;
    }
    result.error = validateFields(environment, {"time_of_day_seconds", "weather"}, "root.environment", result.detail);
    std::string weather;
    if (result.error != ValidationSceneContractError::None ||
        !readDouble(environment, "time_of_day_seconds", "root.environment.time_of_day_seconds",
                    contract.environment.timeOfDaySeconds, result) ||
        !readString(environment, "weather", "root.environment.weather", weather, result)) {
        return result;
    }
    if (contract.environment.timeOfDaySeconds < 0.0 || contract.environment.timeOfDaySeconds >= kWorldDaySeconds ||
        weather != "clear") {
        result.error = ValidationSceneContractError::InvalidEnvironment;
        result.detail = weather != "clear" ? "root.environment.weather" : "root.environment.time_of_day_seconds";
        return result;
    }

    if (contract.scene == ValidationScene::Voxel) {
        const Json& voxel = *root.find("voxel_world");
        if (!voxel.is_object()) {
            result.error = ValidationSceneContractError::InvalidFieldType;
            result.detail = "root.voxel_world";
            return result;
        }
        const auto expectedVoxelFields =
            contract.version == kValidationSceneContractVersion1
                ? std::initializer_list<const char*>{"generator", "seed", "render_distance", "content_hash"}
                : std::initializer_list<const char*>{"generator", "seed", "render_distance", "fixture", "content_hash"};
        result.error = validateFields(voxel, expectedVoxelFields, "root.voxel_world", result.detail);
        if (result.error != ValidationSceneContractError::None) {
            return result;
        }
        const Json& generator = *voxel.find("generator");
        if (!generator.is_object()) {
            result.error = ValidationSceneContractError::InvalidFieldType;
            result.detail = "root.voxel_world.generator";
            return result;
        }
        result.error = validateFields(generator, {"id", "version"}, "root.voxel_world.generator", result.detail);
        ValidationVoxelWorldIdentity world;
        if (result.error != ValidationSceneContractError::None ||
            !readString(generator, "id", "root.voxel_world.generator.id", world.generatorId, result) ||
            !readUint32(generator, "version", "root.voxel_world.generator.version", world.generatorVersion, result) ||
            !readInt(voxel, "seed", "root.voxel_world.seed", world.seed, result) ||
            !readInt(voxel, "render_distance", "root.voxel_world.render_distance", world.renderDistance, result) ||
            !readHash(voxel, "content_hash", "root.voxel_world.content_hash", world.contentHash, result)) {
            return result;
        }
        if (world.generatorId != kValidationVoxelGeneratorId ||
            world.generatorVersion != kValidationVoxelGeneratorVersion ||
            world.renderDistance < kMinimumRenderDistance || world.renderDistance > kMaximumRenderDistance) {
            result.error = ValidationSceneContractError::InvalidWorld;
            result.detail = "root.voxel_world";
            return result;
        }
        if (contract.version == kValidationSceneContractVersion) {
            const Json& fixtureObject = *voxel.find("fixture");
            if (!fixtureObject.is_object()) {
                result.error = ValidationSceneContractError::InvalidFieldType;
                result.detail = "root.voxel_world.fixture";
                return result;
            }
            result.error = validateFields(fixtureObject, {"id", "version", "content_hash"}, "root.voxel_world.fixture",
                                          result.detail);
            ValidationVoxelFixtureIdentity fixture;
            if (result.error != ValidationSceneContractError::None ||
                !readString(fixtureObject, "id", "root.voxel_world.fixture.id", fixture.id, result) ||
                !readUint32(fixtureObject, "version", "root.voxel_world.fixture.version", fixture.version, result) ||
                !readHash(fixtureObject, "content_hash", "root.voxel_world.fixture.content_hash", fixture.contentHash,
                          result)) {
                return result;
            }
            if (!validIdentifier(fixture.id)) {
                result.error = ValidationSceneContractError::InvalidFixture;
                result.detail = "root.voxel_world.fixture";
                return result;
            }
            const std::optional<StableContentHash> actualFixtureHash = validationVoxelFixtureContentHash(fixture);
            if (!actualFixtureHash.has_value()) {
                result.error = ValidationSceneContractError::InvalidFixture;
                result.detail = "root.voxel_world.fixture";
                return result;
            }
            if (*actualFixtureHash != fixture.contentHash) {
                result.error = ValidationSceneContractError::FixtureHashMismatch;
                result.detail = renderer::contracts::stableContentHashHex(*actualFixtureHash);
                return result;
            }
            world.fixture = std::move(fixture);
        }
        const StableContentHash actualWorldHash = validationVoxelWorldContentHash(world);
        if (actualWorldHash != world.contentHash) {
            result.error = ValidationSceneContractError::WorldHashMismatch;
            result.detail = renderer::contracts::stableContentHashHex(actualWorldHash);
            return result;
        }
        contract.voxelWorld = std::move(world);
    } else {
        const Json& asset = *root.find("model_asset");
        if (!asset.is_object()) {
            result.error = ValidationSceneContractError::InvalidFieldType;
            result.detail = "root.model_asset";
            return result;
        }
        result.error = validateFields(asset, {"source", "content_hash"}, "root.model_asset", result.detail);
        ValidationModelAssetIdentity modelAsset;
        if (result.error != ValidationSceneContractError::None ||
            !readRelativePath(asset, "source", "root.model_asset.source", baseDirectory, modelAsset.source,
                              modelAsset.resolvedPath, result) ||
            !readHash(asset, "content_hash", "root.model_asset.content_hash", modelAsset.contentHash, result)) {
            return result;
        }
        const renderer::contracts::FileContentHashResult assetHash =
            renderer::contracts::stableFileContentHash(modelAsset.resolvedPath);
        if (!assetHash.succeeded()) {
            result.error = ValidationSceneContractError::AssetHashFailed;
            result.detail = std::string(renderer::contracts::contentHashErrorStableId(assetHash.error)) + ":" +
                            modelAsset.resolvedPath.generic_u8string();
            return result;
        }
        if (assetHash.hash != modelAsset.contentHash) {
            result.error = ValidationSceneContractError::AssetHashMismatch;
            result.detail = renderer::contracts::stableContentHashHex(assetHash.hash);
            return result;
        }
        contract.modelAsset = std::move(modelAsset);
        if (contract.version == kValidationSceneContractVersion) {
            const Json& probeGridObject = *root.find("reflection_probe_grid");
            if (!probeGridObject.is_object()) {
                result.error = ValidationSceneContractError::InvalidFieldType;
                result.detail = "root.reflection_probe_grid";
                return result;
            }
            result.error = validateFields(probeGridObject, {"spacing_meters", "bounds_padding_meters"},
                                          "root.reflection_probe_grid", result.detail);
            ValidationModelProbeGridIdentity probeGrid;
            if (result.error != ValidationSceneContractError::None ||
                !readDouble(probeGridObject, "spacing_meters", "root.reflection_probe_grid.spacing_meters",
                            probeGrid.spacingMeters, result) ||
                !readDouble(probeGridObject, "bounds_padding_meters",
                            "root.reflection_probe_grid.bounds_padding_meters", probeGrid.boundsPaddingMeters,
                            result)) {
                return result;
            }
            if (probeGrid.spacingMeters <= 0.0 || probeGrid.boundsPaddingMeters < 0.0 ||
                probeGrid.spacingMeters > static_cast<double>(std::numeric_limits<float>::max()) ||
                probeGrid.boundsPaddingMeters > static_cast<double>(std::numeric_limits<float>::max())) {
                result.error = ValidationSceneContractError::InvalidProbeGrid;
                result.detail = "root.reflection_probe_grid";
                return result;
            }
            contract.modelProbeGrid = probeGrid;
        }
    }

    const renderer::contracts::CameraPathLoadResult cameraPath =
        renderer::contracts::loadCameraPath(contract.cameraPath.resolvedPath);
    if (!cameraPath.succeeded()) {
        result.error = ValidationSceneContractError::CameraPathLoadFailed;
        result.detail =
            std::string(renderer::contracts::cameraPathErrorStableId(cameraPath.error)) + ":" + cameraPath.detail;
        return result;
    }
    if (cameraPath.path.id != contract.cameraPath.id ||
        cameraPath.path.contentHash != contract.cameraPath.contentHash) {
        result.error = ValidationSceneContractError::CameraPathIdentityMismatch;
        result.detail =
            cameraPath.path.id + ":" + renderer::contracts::cameraPathContentHashHex(cameraPath.path.contentHash);
        return result;
    }
    result.cameraPath = cameraPath.path;

    const ValidationRenderSettingsProfile profile =
        makeValidationRenderSettingsProfile(contract.scene, contract.renderSettings);
    if (profile.id != contract.renderSettings.id || profile.version != contract.renderSettings.version ||
        profile.contentHash != contract.renderSettings.contentHash) {
        result.error = ValidationSceneContractError::RenderSettingsIdentityMismatch;
        result.detail = profile.id + ":" + renderer::contracts::stableContentHashHex(profile.contentHash);
        return result;
    }

    const StableContentHash actualSceneHash = validationSceneContentHash(contract);
    if (actualSceneHash != contract.contentHash) {
        result.error = ValidationSceneContractError::SceneHashMismatch;
        result.detail = renderer::contracts::stableContentHashHex(actualSceneHash);
        return result;
    }
    return result;
}

} // namespace

bool ValidationSceneContract::isValid() const {
    const bool supportedVersion =
        version == kValidationSceneContractVersion1 || version == kValidationSceneContractVersion;
    const bool voxelFixtureValid =
        !voxelWorld.has_value() || (version == kValidationSceneContractVersion1 ? !voxelWorld->fixture.has_value()
                                                                                : voxelWorld->fixture.has_value());
    const bool modelProbeGridValid =
        !modelAsset.has_value() || (version == kValidationSceneContractVersion1
                                        ? !modelProbeGrid.has_value()
                                        : modelProbeGrid.has_value() && modelProbeGrid->spacingMeters > 0.0 &&
                                              modelProbeGrid->boundsPaddingMeters >= 0.0);
    const bool sceneIdentityValid =
        (scene == ValidationScene::Voxel && voxelWorld.has_value() && !modelAsset.has_value() &&
         !modelProbeGrid.has_value()) ||
        (scene == ValidationScene::Model && modelAsset.has_value() && !voxelWorld.has_value());
    return supportedVersion && voxelFixtureValid && modelProbeGridValid && !id.empty() && !sourcePath.empty() &&
           contentHash != 0u && !cameraPath.source.empty() && !cameraPath.resolvedPath.empty() &&
           !cameraPath.id.empty() && cameraPath.contentHash != 0u && !renderSettings.id.empty() &&
           renderSettings.version != 0u && renderSettings.contentHash != 0u && sceneIdentityValid;
}

bool ValidationSceneContractLoadResult::succeeded() const {
    return error == ValidationSceneContractError::None && contract.isValid() && cameraPath.isValid();
}

ValidationRenderSettingsProfile makeValidationRenderSettingsProfile(const ValidationScene scene) {
    ValidationRenderSettingsProfile profile;
    profile.version = kValidationRenderSettingsVersion;
    configureCommonValidationSettings(profile.settings);
    switch (scene) {
    case ValidationScene::Voxel: profile.id = "m0_voxel_render_settings"; break;
    case ValidationScene::Model:
        profile.id = "m0_model_render_settings";
        profile.settings.occlusion.hiZEnabled = false;
        profile.settings.transparent.waterEffectsEnabled = false;
        profile.settings.transparent.compositeEnabled = false;
        profile.settings.weather.particlesEnabled = false;
        profile.settings.weather.rainLinesEnabled = false;
        profile.settings.taa.enabled = false;
        profile.settings.shadow.gpuCascadeCullEnabled = false;
        profile.settings.fog.autoDistanceByRenderDistance = false;
        break;
    case ValidationScene::None: std::abort();
    }
    profile.contentHash = renderSettingsContentHash(profile.settings);
    return profile;
}

ValidationRenderSettingsProfile
makeValidationRenderSettingsProfile(const ValidationScene scene, const ValidationRenderSettingsIdentity& identity) {
    ValidationRenderSettingsProfile profile = makeValidationRenderSettingsProfile(scene);
    if (identity.id == profile.id && identity.version == profile.version) {
        return profile;
    }

    const char* expectedRtgiId = scene == ValidationScene::Voxel ? kValidationRtgiVoxelRenderSettingsId
                                                                 : kValidationRtgiModelRenderSettingsId;
    const uint32_t expectedRtgiVersion =
        scene == ValidationScene::Voxel ? kValidationRtgiVoxelRenderSettingsVersion
                                        : kValidationRtgiModelRenderSettingsVersion;
    if (scene == ValidationScene::None || identity.id != expectedRtgiId ||
        identity.version != expectedRtgiVersion) {
        return {};
    }
    profile.id = expectedRtgiId;
    profile.version = expectedRtgiVersion;
    profile.settings.rtgi.enabled = true;
    profile.settings.rtgi.intensity = 1.0f;
    profile.settings.nrd.enabled = true;
    profile.settings.nrd.method = NrdDiffuseMethod::Relax;
    profile.settings.nrd.disocclusionThreshold = 0.02f;
    profile.settings.ssgi.enabled = false;
    if (scene == ValidationScene::Voxel) {
        // The versioned RTGI voxel baseline must not execute known-incorrect occlusion paths.
        profile.settings.occlusion.hiZEnabled = false;
        profile.settings.shadow.gpuCascadeCullEnabled = false;
    }
    profile.contentHash = renderSettingsContentHash(profile.settings);
    return profile;
}

StableContentHash validationVoxelWorldContentHash(const ValidationVoxelWorldIdentity& world) {
    StableContentHashBuilder hash;
    hash.addString(world.generatorId);
    hash.addUint64(world.generatorVersion);
    hash.addInt64(world.seed);
    hash.addInt64(world.renderDistance);
    if (world.fixture.has_value()) {
        hash.addString(world.fixture->id);
        hash.addUint64(world.fixture->version);
        hash.addUint64(world.fixture->contentHash);
    }
    return hash.value();
}

StableContentHash validationSceneContentHash(const ValidationSceneContract& contract) {
    StableContentHashBuilder hash;
    hash.addString(kValidationSceneContractKind);
    hash.addUint64(contract.version);
    hash.addString(contract.id);
    hash.addString(validationSceneStableId(contract.scene));
    hash.addString(contract.cameraPath.source.generic_u8string());
    hash.addString(contract.cameraPath.id);
    hash.addUint64(contract.cameraPath.contentHash);
    hash.addString(contract.renderSettings.id);
    hash.addUint64(contract.renderSettings.version);
    hash.addUint64(contract.renderSettings.contentHash);
    hash.addDouble(contract.environment.timeOfDaySeconds);
    hash.addString(validationWeatherStableId(contract.environment.weather));
    if (contract.scene == ValidationScene::Voxel) {
        if (!contract.voxelWorld.has_value()) {
            std::abort();
        }
        const ValidationVoxelWorldIdentity& world = *contract.voxelWorld;
        hash.addString(world.generatorId);
        hash.addUint64(world.generatorVersion);
        hash.addInt64(world.seed);
        hash.addInt64(world.renderDistance);
        if (world.fixture.has_value()) {
            hash.addString(world.fixture->id);
            hash.addUint64(world.fixture->version);
            hash.addUint64(world.fixture->contentHash);
        }
        hash.addUint64(world.contentHash);
    } else if (contract.scene == ValidationScene::Model) {
        if (!contract.modelAsset.has_value()) {
            std::abort();
        }
        hash.addString(contract.modelAsset->source.generic_u8string());
        hash.addUint64(contract.modelAsset->contentHash);
        if (contract.modelProbeGrid.has_value()) {
            hash.addDouble(contract.modelProbeGrid->spacingMeters);
            hash.addDouble(contract.modelProbeGrid->boundsPaddingMeters);
        }
    } else {
        std::abort();
    }
    return hash.value();
}

ValidationSceneContractLoadResult parseValidationSceneContractJson(const std::string_view jsonText,
                                                                   const std::filesystem::path& sourcePath) {
    const Json root = Json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
    if (root.is_discarded()) {
        ValidationSceneContractLoadResult result;
        result.error = ValidationSceneContractError::InvalidJson;
        result.detail = sourcePath.generic_u8string();
        return result;
    }
    return parseRoot(root, sourcePath);
}

ValidationSceneContractLoadResult loadValidationSceneContract(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        ValidationSceneContractLoadResult result;
        result.error = ValidationSceneContractError::FileOpenFailed;
        result.detail = path.generic_u8string();
        return result;
    }
    std::ostringstream text;
    text << input.rdbuf();
    if (input.bad()) {
        ValidationSceneContractLoadResult result;
        result.error = ValidationSceneContractError::FileReadFailed;
        result.detail = path.generic_u8string();
        return result;
    }
    return parseValidationSceneContractJson(text.str(), path);
}

const char* validationWeatherStableId(const ValidationWeather weather) {
    switch (weather) {
    case ValidationWeather::Clear: return "clear";
    }
    std::abort();
}

const char* validationSceneContractErrorStableId(const ValidationSceneContractError error) {
    switch (error) {
    case ValidationSceneContractError::None: return "None";
    case ValidationSceneContractError::FileOpenFailed: return "FileOpenFailed";
    case ValidationSceneContractError::FileReadFailed: return "FileReadFailed";
    case ValidationSceneContractError::InvalidJson: return "InvalidJson";
    case ValidationSceneContractError::InvalidRoot: return "InvalidRoot";
    case ValidationSceneContractError::MissingField: return "MissingField";
    case ValidationSceneContractError::UnexpectedField: return "UnexpectedField";
    case ValidationSceneContractError::InvalidFieldType: return "InvalidFieldType";
    case ValidationSceneContractError::InvalidKind: return "InvalidKind";
    case ValidationSceneContractError::UnsupportedVersion: return "UnsupportedVersion";
    case ValidationSceneContractError::InvalidIdentifier: return "InvalidIdentifier";
    case ValidationSceneContractError::InvalidScene: return "InvalidScene";
    case ValidationSceneContractError::InvalidContentHash: return "InvalidContentHash";
    case ValidationSceneContractError::InvalidPath: return "InvalidPath";
    case ValidationSceneContractError::CameraPathLoadFailed: return "CameraPathLoadFailed";
    case ValidationSceneContractError::CameraPathIdentityMismatch: return "CameraPathIdentityMismatch";
    case ValidationSceneContractError::RenderSettingsIdentityMismatch: return "RenderSettingsIdentityMismatch";
    case ValidationSceneContractError::InvalidEnvironment: return "InvalidEnvironment";
    case ValidationSceneContractError::InvalidWorld: return "InvalidWorld";
    case ValidationSceneContractError::InvalidFixture: return "InvalidFixture";
    case ValidationSceneContractError::FixtureHashMismatch: return "FixtureHashMismatch";
    case ValidationSceneContractError::WorldHashMismatch: return "WorldHashMismatch";
    case ValidationSceneContractError::InvalidProbeGrid: return "InvalidProbeGrid";
    case ValidationSceneContractError::AssetHashFailed: return "AssetHashFailed";
    case ValidationSceneContractError::AssetHashMismatch: return "AssetHashMismatch";
    case ValidationSceneContractError::SceneHashMismatch: return "SceneHashMismatch";
    }
    std::abort();
}

} // namespace app::validation
