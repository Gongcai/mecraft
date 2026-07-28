#ifndef MECRAFT_CAMERA_PATH_CONTRACT_H
#define MECRAFT_CAMERA_PATH_CONTRACT_H

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace renderer::contracts {

inline constexpr uint32_t kCameraPathContractVersion = 1u;
inline constexpr const char *kCameraPathContractKind = "mecraft.camera_path";
inline constexpr const char *kCameraPathCoordinateSystem = "right_handed_y_up";
inline constexpr const char *kCameraPathInterpolation =
    "linear_position_slerp_orientation";

/// Identifies every deterministic Camera Path parsing or sampling failure.
enum class CameraPathError : uint8_t {
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
  InvalidCoordinateSystem,
  InvalidInterpolation,
  InvalidKeyframeCount,
  InvalidKeyframeTime,
  InvalidPosition,
  InvalidLookAt,
  InvalidUpVector,
  InvalidFieldOfView,
  InvalidPathState,
  SampleTimeNotFinite,
  SampleTimeOutOfRange
};

/// Stores one validated Camera Path keyframe and its derived orientation.
struct CameraPathKeyframe {
  double timeSeconds = 0.0;
  glm::dvec3 position{0.0};
  glm::dvec3 lookAt{0.0, 0.0, -1.0};
  glm::dvec3 up{0.0, 1.0, 0.0};
  glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
  double verticalFovDegrees = 60.0;
};

/// Describes one versioned deterministic Camera Path asset.
struct CameraPath {
  std::string id;
  std::vector<CameraPathKeyframe> keyframes;
  double durationSeconds = 0.0;
  uint64_t contentHash = 0u;

  /// Reports whether the path contains a complete validated time range.
  /// @return True when the path has at least two ordered keyframes.
  [[nodiscard]] bool isValid() const;
};

/// Reports the camera pose sampled from one validated path time.
struct CameraPathPose {
  glm::dvec3 position{0.0};
  glm::dvec3 forward{0.0, 0.0, -1.0};
  glm::dvec3 up{0.0, 1.0, 0.0};
  double verticalFovDegrees = 60.0;
};

/// Returns a parsed path or a stable error with field-specific detail.
struct CameraPathLoadResult {
  CameraPath path;
  CameraPathError error = CameraPathError::None;
  std::string detail;

  /// Reports whether parsing and semantic validation succeeded.
  /// @return True only when error is None and path is valid.
  [[nodiscard]] bool succeeded() const;
};

/// Returns the stable identifier used by logs, reports, and automated tests.
/// @param error Camera Path error to identify.
/// @return Process-lifetime string containing the stable error identifier.
[[nodiscard]] const char *cameraPathErrorStableId(CameraPathError error);

/// Parses and validates one version 1 Camera Path JSON document.
/// @param jsonText Complete UTF-8 JSON document.
/// @return Parsed path, stable semantic hash, or a structured error.
[[nodiscard]] CameraPathLoadResult
parseCameraPathJson(std::string_view jsonText);

/// Loads and validates one versioned Camera Path file.
/// @param path File containing a Camera Path JSON document.
/// @return Parsed path, stable semantic hash, or a structured error.
[[nodiscard]] CameraPathLoadResult
loadCameraPath(const std::filesystem::path &path);

/// Samples one deterministic camera pose without clamping the requested time.
/// @param path Previously validated Camera Path.
/// @param timeSeconds Time in the inclusive range [0, durationSeconds].
/// @param pose Receives position, orientation axes, and vertical field of view.
/// @return None on success or a stable path/sampling error.
[[nodiscard]] CameraPathError sampleCameraPath(const CameraPath &path,
                                               double timeSeconds,
                                               CameraPathPose &pose);

/// Formats a Camera Path content hash for manifests and benchmark reports.
/// @param contentHash Stable semantic hash returned by the parser.
/// @return Sixteen lowercase hexadecimal digits.
[[nodiscard]] std::string cameraPathContentHashHex(uint64_t contentHash);

} // namespace renderer::contracts

#endif // MECRAFT_CAMERA_PATH_CONTRACT_H
