#ifndef MECRAFT_VALIDATION_SCENE_CONTRACT_H
#define MECRAFT_VALIDATION_SCENE_CONTRACT_H

#include "app/AppLaunchOptions.h"
#include "renderer/contracts/CameraPathContract.h"
#include "renderer/contracts/ContentHashContract.h"
#include "renderer/core/RenderSettings.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace app::validation {

inline constexpr uint32_t kValidationSceneContractVersion = 1u;
inline constexpr const char* kValidationSceneContractKind =
    "mecraft.validation_scene";
inline constexpr uint32_t kValidationRenderSettingsVersion = 1u;
inline constexpr const char* kValidationVoxelGeneratorId =
    "mecraft.overworld";
inline constexpr uint32_t kValidationVoxelGeneratorVersion = 1u;

/// Identifies the deterministic weather state encoded by a validation scene.
enum class ValidationWeather : uint8_t {
    Clear
};

/// Stores one immutable renderer configuration used by validation scenes.
struct ValidationRenderSettingsProfile {
    std::string id;
    uint32_t version = 0u;
    RenderSettings settings;
    renderer::contracts::StableContentHash contentHash = 0u;
};

/// Identifies one versioned Camera Path and its resolved repository asset.
struct ValidationCameraPathIdentity {
    std::filesystem::path source;
    std::filesystem::path resolvedPath;
    std::string id;
    renderer::contracts::StableContentHash contentHash = 0u;
};

/// Identifies the exact fixed renderer settings required by one scene.
struct ValidationRenderSettingsIdentity {
    std::string id;
    uint32_t version = 0u;
    renderer::contracts::StableContentHash contentHash = 0u;
};

/// Stores deterministic environment inputs shared by voxel and model scenes.
struct ValidationEnvironmentIdentity {
    double timeOfDaySeconds = 0.0;
    ValidationWeather weather = ValidationWeather::Clear;
};

/// Identifies a generated voxel world recipe and its stable semantic hash.
struct ValidationVoxelWorldIdentity {
    std::string generatorId;
    uint32_t generatorVersion = 0u;
    int seed = 0;
    int renderDistance = 0;
    renderer::contracts::StableContentHash contentHash = 0u;
};

/// Identifies one exact model file by repository-relative path and byte hash.
struct ValidationModelAssetIdentity {
    std::filesystem::path source;
    std::filesystem::path resolvedPath;
    renderer::contracts::StableContentHash contentHash = 0u;
};

/// Describes every versioned identity input required by one validation run.
struct ValidationSceneContract {
    uint32_t version = 0u;
    std::string id;
    ValidationScene scene = ValidationScene::None;
    std::filesystem::path sourcePath;
    renderer::contracts::StableContentHash contentHash = 0u;
    ValidationCameraPathIdentity cameraPath;
    ValidationRenderSettingsIdentity renderSettings;
    ValidationEnvironmentIdentity environment;
    std::optional<ValidationVoxelWorldIdentity> voxelWorld;
    std::optional<ValidationModelAssetIdentity> modelAsset;

    /// Reports whether the scene contains exactly one complete scene identity.
    /// @return True after strict parsing and all referenced-asset verification.
    [[nodiscard]] bool isValid() const;
};

/// Identifies every scene parsing or referenced-content verification failure.
enum class ValidationSceneContractError : uint8_t {
    None,
    FileOpenFailed,
    FileReadFailed,
    InvalidJson,
    InvalidRoot,
    MissingField,
    UnexpectedField,
    InvalidFieldType,
    InvalidKind,
    UnsupportedVersion,
    InvalidIdentifier,
    InvalidScene,
    InvalidContentHash,
    InvalidPath,
    CameraPathLoadFailed,
    CameraPathIdentityMismatch,
    RenderSettingsIdentityMismatch,
    InvalidEnvironment,
    InvalidWorld,
    WorldHashMismatch,
    AssetHashFailed,
    AssetHashMismatch,
    SceneHashMismatch
};

/// Returns a complete scene and Camera Path or one stable structured error.
struct ValidationSceneContractLoadResult {
    ValidationSceneContract contract;
    renderer::contracts::CameraPath cameraPath;
    ValidationSceneContractError error = ValidationSceneContractError::None;
    std::string detail;

    /// Reports whether the descriptor and all referenced identities match.
    /// @return True only when both contract and Camera Path are valid.
    [[nodiscard]] bool succeeded() const;
};

/// Builds the renderer profile whose hash is locked by scene descriptors.
/// @param scene Concrete validation scene that consumes the settings.
/// @return Complete settings, stable metadata, and semantic content hash.
[[nodiscard]] ValidationRenderSettingsProfile
makeValidationRenderSettingsProfile(ValidationScene scene);

/// Computes the semantic hash of one generated voxel world recipe.
/// @param world Generator identity, seed, and render distance to hash.
/// @return Stable world identity independent of JSON formatting.
[[nodiscard]] renderer::contracts::StableContentHash
validationVoxelWorldContentHash(const ValidationVoxelWorldIdentity& world);

/// Computes the semantic hash of one complete validation scene descriptor.
/// @param contract Parsed scene fields excluding the expected root hash.
/// @return Stable identity independent of JSON formatting and key order.
[[nodiscard]] renderer::contracts::StableContentHash
validationSceneContentHash(const ValidationSceneContract& contract);

/// Parses and verifies a scene descriptor and every referenced content hash.
/// @param jsonText Complete UTF-8 scene descriptor.
/// @param sourcePath Logical descriptor path used to resolve relative assets.
/// @return Verified scene contract, Camera Path, or a structured error.
[[nodiscard]] ValidationSceneContractLoadResult
parseValidationSceneContractJson(
    std::string_view jsonText,
    const std::filesystem::path& sourcePath);

/// Loads and verifies one versioned validation scene descriptor.
/// @param path Scene JSON file whose directory anchors relative asset paths.
/// @return Verified scene contract, Camera Path, or a structured error.
[[nodiscard]] ValidationSceneContractLoadResult
loadValidationSceneContract(const std::filesystem::path& path);

/// Returns the stable lowercase weather identifier used by manifests.
/// @param weather Validation weather state to identify.
/// @return Process-lifetime lowercase identifier.
[[nodiscard]] const char* validationWeatherStableId(ValidationWeather weather);

/// Returns the stable identifier used by diagnostics and automated tests.
/// @param error Scene contract error to identify.
/// @return Process-lifetime error identifier.
[[nodiscard]] const char* validationSceneContractErrorStableId(
    ValidationSceneContractError error);

} // namespace app::validation

#endif // MECRAFT_VALIDATION_SCENE_CONTRACT_H
