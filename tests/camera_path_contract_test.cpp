#include "renderer/contracts/CameraPathContract.h"

#include <glm/geometric.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

namespace {
using renderer::contracts::CameraPath;
using renderer::contracts::CameraPathError;
using renderer::contracts::CameraPathLoadResult;
using renderer::contracts::CameraPathPose;

constexpr const char *kValidPathJson = R"json(
{
  "kind": "mecraft.camera_path",
  "version": 1,
  "id": "contract_test",
  "coordinate_system": "right_handed_y_up",
  "interpolation": "linear_position_slerp_orientation",
  "keyframes": [
    {
      "time_seconds": 0.0,
      "position": [0.0, 0.0, 0.0],
      "look_at": [0.0, 0.0, -1.0],
      "up": [0.0, 1.0, 0.0],
      "vertical_fov_degrees": 60.0
    },
    {
      "time_seconds": 2.0,
      "position": [2.0, 0.0, 0.0],
      "look_at": [2.0, 0.0, -1.0],
      "up": [0.0, 1.0, 0.0],
      "vertical_fov_degrees": 80.0
    }
  ]
}
)json";

constexpr const char *kVoxelBaselineHash = "93b8b518406d50f9";
constexpr const char *kModelBaselineHash = "6f33df94f7766d10";

bool requireTrue(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[camera_path_contract_test] FAIL: " << message << '\n';
    return false;
  }
  return true;
}

bool nearlyEqual(const double lhs, const double rhs,
                 const double tolerance = 1.0e-9) {
  return std::abs(lhs - rhs) <= tolerance;
}

bool nearlyEqual(const glm::dvec3 &lhs, const glm::dvec3 &rhs,
                 const double tolerance = 1.0e-9) {
  return nearlyEqual(lhs.x, rhs.x, tolerance) &&
         nearlyEqual(lhs.y, rhs.y, tolerance) &&
         nearlyEqual(lhs.z, rhs.z, tolerance);
}

CameraPathLoadResult parseJson(const nlohmann::json &root) {
  return renderer::contracts::parseCameraPathJson(root.dump());
}

bool expectError(const nlohmann::json &root, const CameraPathError expected,
                 const char *message) {
  const CameraPathLoadResult result = parseJson(root);
  if (result.error != expected) {
    std::cerr << "[camera_path_contract_test] expected "
              << renderer::contracts::cameraPathErrorStableId(expected)
              << " but received "
              << renderer::contracts::cameraPathErrorStableId(result.error)
              << " at " << result.detail << '\n';
    return requireTrue(false, message);
  }
  return true;
}

bool testStableErrorIdentifiers() {
  return requireTrue(
             std::string(renderer::contracts::cameraPathErrorStableId(
                 CameraPathError::UnsupportedVersion)) == "UnsupportedVersion",
             "unsupported versions must expose a stable error identifier") &&
         requireTrue(
             std::string(renderer::contracts::cameraPathErrorStableId(
                 CameraPathError::SampleTimeOutOfRange)) ==
                 "SampleTimeOutOfRange",
             "sampling range failures must expose a stable error identifier");
}

bool testParsingHashAndSampling() {
  const CameraPathLoadResult result =
      renderer::contracts::parseCameraPathJson(kValidPathJson);
  if (!requireTrue(result.succeeded(),
                   "a complete version 1 path must parse") ||
      !requireTrue(result.path.id == "contract_test" &&
                       result.path.keyframes.size() == 2u &&
                       result.path.durationSeconds == 2.0,
                   "parsed path metadata must match the document") ||
      !requireTrue(result.path.contentHash != 0u,
                   "validated paths must expose a semantic content hash") ||
      !requireTrue(
          renderer::contracts::cameraPathContentHashHex(result.path.contentHash)
                  .size() == 16u,
          "content hashes must use sixteen hexadecimal digits")) {
    return false;
  }

  nlohmann::json reordered =
      nlohmann::json::parse(kValidPathJson, nullptr, false);
  reordered["keyframes"][0]["position"] = {-0.0, 0, 0};
  const CameraPathLoadResult equivalent = parseJson(reordered);
  if (!requireTrue(equivalent.succeeded() &&
                       equivalent.path.contentHash == result.path.contentHash,
                   "JSON formatting, numeric representation, and signed zero "
                   "must not change the hash")) {
    return false;
  }
  reordered["keyframes"][1]["vertical_fov_degrees"] = 81.0;
  const CameraPathLoadResult changed = parseJson(reordered);
  if (!requireTrue(changed.succeeded() &&
                       changed.path.contentHash != result.path.contentHash,
                   "semantic camera changes must change the content hash")) {
    return false;
  }

  CameraPathPose pose;
  if (!requireTrue(renderer::contracts::sampleCameraPath(
                       result.path, 1.0, pose) == CameraPathError::None,
                   "an in-range time must sample successfully") ||
      !requireTrue(nearlyEqual(pose.position, {1.0, 0.0, 0.0}),
                   "position sampling must be linear") ||
      !requireTrue(nearlyEqual(pose.forward, {0.0, 0.0, -1.0}),
                   "a fixed orientation must retain its forward axis") ||
      !requireTrue(nearlyEqual(pose.up, {0.0, 1.0, 0.0}),
                   "a fixed orientation must retain its up axis") ||
      !requireTrue(nearlyEqual(pose.verticalFovDegrees, 70.0),
                   "field-of-view sampling must be linear")) {
    return false;
  }

  if (!requireTrue(
          renderer::contracts::sampleCameraPath(result.path, 2.0, pose) ==
                  CameraPathError::None &&
              nearlyEqual(pose.position, {2.0, 0.0, 0.0}),
          "the inclusive duration endpoint must return the final keyframe") ||
      !requireTrue(
          renderer::contracts::sampleCameraPath(result.path, -0.01, pose) ==
              CameraPathError::SampleTimeOutOfRange,
          "negative sample times must not be clamped") ||
      !requireTrue(
          renderer::contracts::sampleCameraPath(result.path, 2.01, pose) ==
              CameraPathError::SampleTimeOutOfRange,
          "times after the duration must not be clamped") ||
      !requireTrue(renderer::contracts::sampleCameraPath(
                       result.path, std::numeric_limits<double>::quiet_NaN(),
                       pose) == CameraPathError::SampleTimeNotFinite,
                   "non-finite sample times must be rejected")) {
    return false;
  }
  return true;
}

bool testValidationFailures() {
  const nlohmann::json valid =
      nlohmann::json::parse(kValidPathJson, nullptr, false);
  if (!requireTrue(!valid.is_discarded(),
                   "the test fixture itself must be valid JSON")) {
    return false;
  }

  nlohmann::json changed = valid;
  changed["version"] = 2;
  if (!expectError(changed, CameraPathError::UnsupportedVersion,
                   "unknown versions must be rejected")) {
    return false;
  }
  changed = valid;
  changed["unexpected"] = true;
  if (!expectError(changed, CameraPathError::UnexpectedField,
                   "version 1 must reject unknown root fields")) {
    return false;
  }
  changed = valid;
  changed.erase("id");
  if (!expectError(changed, CameraPathError::MissingField,
                   "required fields must not be omitted")) {
    return false;
  }
  changed = valid;
  changed["id"] = "UppercaseId";
  if (!expectError(changed, CameraPathError::InvalidIdentifier,
                   "path identifiers must use the stable lowercase format")) {
    return false;
  }
  changed = valid;
  changed["keyframes"].erase(changed["keyframes"].begin() + 1);
  if (!expectError(changed, CameraPathError::InvalidKeyframeCount,
                   "a path must contain at least two keyframes")) {
    return false;
  }
  changed = valid;
  changed["keyframes"][1]["time_seconds"] = 0.0;
  if (!expectError(changed, CameraPathError::InvalidKeyframeTime,
                   "keyframe times must increase strictly")) {
    return false;
  }
  changed = valid;
  changed["keyframes"][0]["look_at"] = {0.0, 0.0, 0.0};
  if (!expectError(changed, CameraPathError::InvalidLookAt,
                   "a camera must look away from its position")) {
    return false;
  }
  changed = valid;
  changed["keyframes"][0]["up"] = {0.0, 0.0, -1.0};
  if (!expectError(changed, CameraPathError::InvalidUpVector,
                   "the up axis must not be parallel to the view direction")) {
    return false;
  }
  changed = valid;
  changed["keyframes"][0]["vertical_fov_degrees"] = 179.0;
  if (!expectError(
          changed, CameraPathError::InvalidFieldOfView,
          "field of view must remain inside the open supported range")) {
    return false;
  }

  const CameraPathLoadResult invalidJson =
      renderer::contracts::parseCameraPathJson("{");
  if (!requireTrue(invalidJson.error == CameraPathError::InvalidJson,
                   "invalid JSON must report InvalidJson")) {
    return false;
  }
  const CameraPathLoadResult missingFile = renderer::contracts::loadCameraPath(
      std::filesystem::path(MECRAFT_TEST_SOURCE_DIR) /
      "assets/validation/camera_paths/missing.json");
  return requireTrue(missingFile.error == CameraPathError::FileOpenFailed,
                     "missing path assets must report FileOpenFailed");
}

bool testVersionedBaselineAssets() {
  const std::filesystem::path root =
      std::filesystem::path(MECRAFT_TEST_SOURCE_DIR) /
      "assets/validation/camera_paths";
  const CameraPathLoadResult voxel =
      renderer::contracts::loadCameraPath(root / "m0_voxel_baseline.json");
  const CameraPathLoadResult model = renderer::contracts::loadCameraPath(
      root / "m0_model_damaged_helmet.json");
  if (!requireTrue(voxel.succeeded() && model.succeeded(),
                   "both M0 scene classes must ship a valid Camera Path") ||
      !requireTrue(voxel.path.id == "m0_voxel_baseline" &&
                       voxel.path.durationSeconds == 6.0 &&
                       voxel.path.keyframes.size() == 4u,
                   "the voxel baseline path metadata must remain versioned") ||
      !requireTrue(model.path.id == "m0_model_damaged_helmet" &&
                       model.path.durationSeconds == 4.5 &&
                       model.path.keyframes.size() == 4u,
                   "the model baseline path metadata must remain versioned") ||
      !requireTrue(renderer::contracts::cameraPathContentHashHex(
                       voxel.path.contentHash) == kVoxelBaselineHash,
                   "the voxel baseline semantic hash must remain versioned") ||
      !requireTrue(renderer::contracts::cameraPathContentHashHex(
                       model.path.contentHash) == kModelBaselineHash,
                   "the model baseline semantic hash must remain versioned") ||
      !requireTrue(
          voxel.path.contentHash != model.path.contentHash,
          "different baseline paths must have different semantic hashes")) {
    return false;
  }

  CameraPathPose pose;
  if (!requireTrue(renderer::contracts::sampleCameraPath(
                       voxel.path, voxel.path.durationSeconds * 0.5, pose) ==
                       CameraPathError::None,
                   "the voxel baseline midpoint must sample") ||
      !requireTrue(
          nearlyEqual(glm::length(pose.forward), 1.0, 1.0e-8) &&
              nearlyEqual(glm::length(pose.up), 1.0, 1.0e-8) &&
              nearlyEqual(glm::dot(pose.forward, pose.up), 0.0, 1.0e-8),
          "sampled camera axes must remain orthonormal")) {
    return false;
  }
  return true;
}
} // namespace

int main() {
  if (!testStableErrorIdentifiers() || !testParsingHashAndSampling() ||
      !testValidationFailures() || !testVersionedBaselineAssets()) {
    return 1;
  }
  std::cout << "[camera_path_contract_test] PASS\n";
  return 0;
}
