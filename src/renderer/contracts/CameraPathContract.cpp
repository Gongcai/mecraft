#include "renderer/contracts/CameraPathContract.h"
#include "renderer/contracts/ContentHashContract.h"

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <limits>

namespace renderer::contracts {
namespace {
using Json = nlohmann::json;

constexpr size_t kMaximumIdentifierLength = 128u;
constexpr size_t kMaximumKeyframeCount = 65536u;
constexpr double kMinimumVectorLengthSquared = 1.0e-12;

[[nodiscard]] bool finiteVector(const glm::dvec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finiteQuaternion(const glm::dquat& value) {
    return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool expectedField(const std::string_view name, const std::initializer_list<const char*> expectedFields) {
    return std::any_of(expectedFields.begin(), expectedFields.end(),
                       [name](const char* field) { return name == field; });
}

[[nodiscard]] CameraPathError validateFields(const Json& object,
                                             const std::initializer_list<const char*> expectedFields,
                                             const std::string_view objectPath, std::string& detail) {
    for (const char* field : expectedFields) {
        if (object.find(field) == object.end()) {
            detail = std::string(objectPath) + "." + field;
            return CameraPathError::MissingField;
        }
    }
    if (object.size() == expectedFields.size()) {
        return CameraPathError::None;
    }
    for (auto field = object.begin(); field != object.end(); ++field) {
        if (!expectedField(field.key(), expectedFields)) {
            detail = std::string(objectPath) + "." + field.key();
            return CameraPathError::UnexpectedField;
        }
    }
    detail = std::string(objectPath);
    return CameraPathError::UnexpectedField;
}

[[nodiscard]] bool readString(const Json& object, const char* field, const std::string_view fieldPath,
                              std::string& output, CameraPathLoadResult& result) {
    const Json& value = *object.find(field);
    if (!value.is_string()) {
        result.error = CameraPathError::InvalidFieldType;
        result.detail = fieldPath;
        return false;
    }
    output = value.get_ref<const std::string&>();
    return true;
}

[[nodiscard]] bool readVersion(const Json& root, uint32_t& version, CameraPathLoadResult& result) {
    const Json& value = *root.find("version");
    uint64_t parsed = 0u;
    if (value.is_number_unsigned()) {
        parsed = value.get<uint64_t>();
    } else if (value.is_number_integer()) {
        const int64_t signedValue = value.get<int64_t>();
        if (signedValue < 0) {
            result.error = CameraPathError::UnsupportedVersion;
            result.detail = "root.version";
            return false;
        }
        parsed = static_cast<uint64_t>(signedValue);
    } else {
        result.error = CameraPathError::InvalidFieldType;
        result.detail = "root.version";
        return false;
    }
    if (parsed > std::numeric_limits<uint32_t>::max()) {
        result.error = CameraPathError::UnsupportedVersion;
        result.detail = "root.version";
        return false;
    }
    version = static_cast<uint32_t>(parsed);
    return true;
}

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

[[nodiscard]] bool readFiniteNumber(const Json& value, const CameraPathError error, const std::string& fieldPath,
                                    double& output, CameraPathLoadResult& result) {
    if (!value.is_number()) {
        result.error = error;
        result.detail = fieldPath;
        return false;
    }
    output = value.get<double>();
    if (!std::isfinite(output)) {
        result.error = error;
        result.detail = fieldPath;
        return false;
    }
    return true;
}

[[nodiscard]] bool readVector(const Json& value, const CameraPathError error, const std::string& fieldPath,
                              glm::dvec3& output, CameraPathLoadResult& result) {
    if (!value.is_array() || value.size() != 3u) {
        result.error = error;
        result.detail = fieldPath;
        return false;
    }
    for (size_t component = 0u; component < 3u; ++component) {
        double parsed = 0.0;
        if (!readFiniteNumber(value[component], error, fieldPath + "[" + std::to_string(component) + "]", parsed,
                              result)) {
            return false;
        }
        output[component] = parsed;
    }
    return true;
}

[[nodiscard]] bool quaternionRequiresNegation(const glm::dquat& value) {
    if (value.w != 0.0)
        return value.w < 0.0;
    if (value.x != 0.0)
        return value.x < 0.0;
    if (value.y != 0.0)
        return value.y < 0.0;
    return value.z < 0.0;
}

[[nodiscard]] bool buildOrientation(CameraPathKeyframe& keyframe, const size_t keyframeIndex,
                                    CameraPathLoadResult& result) {
    const glm::dvec3 direction = keyframe.lookAt - keyframe.position;
    const double directionLengthSquared = glm::dot(direction, direction);
    if (!std::isfinite(directionLengthSquared) || directionLengthSquared <= kMinimumVectorLengthSquared) {
        result.error = CameraPathError::InvalidLookAt;
        result.detail = "root.keyframes[" + std::to_string(keyframeIndex) + "].look_at";
        return false;
    }
    const double upLengthSquared = glm::dot(keyframe.up, keyframe.up);
    if (!std::isfinite(upLengthSquared) || upLengthSquared <= kMinimumVectorLengthSquared) {
        result.error = CameraPathError::InvalidUpVector;
        result.detail = "root.keyframes[" + std::to_string(keyframeIndex) + "].up";
        return false;
    }
    const glm::dvec3 forward = direction / std::sqrt(directionLengthSquared);
    const glm::dvec3 requestedUp = keyframe.up / std::sqrt(upLengthSquared);
    glm::dvec3 right = glm::cross(forward, requestedUp);
    const double rightLengthSquared = glm::dot(right, right);
    if (!std::isfinite(rightLengthSquared) || rightLengthSquared <= kMinimumVectorLengthSquared) {
        result.error = CameraPathError::InvalidUpVector;
        result.detail = "root.keyframes[" + std::to_string(keyframeIndex) + "].up";
        return false;
    }
    right /= std::sqrt(rightLengthSquared);
    const glm::dvec3 correctedUp = glm::normalize(glm::cross(right, forward));
    glm::dquat orientation = glm::normalize(glm::quat_cast(glm::dmat3(right, correctedUp, -forward)));
    if (!finiteQuaternion(orientation)) {
        result.error = CameraPathError::InvalidPathState;
        result.detail = "root.keyframes[" + std::to_string(keyframeIndex) + "]";
        return false;
    }
    if (quaternionRequiresNegation(orientation)) {
        orientation = -orientation;
    }
    keyframe.orientation = orientation;
    return true;
}

[[nodiscard]] uint64_t hashPath(const CameraPath& path) {
    StableContentHashBuilder hash;
    hash.addString(kCameraPathContractKind);
    hash.addUint64(kCameraPathContractVersion);
    hash.addString(path.id);
    hash.addString(kCameraPathCoordinateSystem);
    hash.addString(kCameraPathInterpolation);
    hash.addUint64(path.keyframes.size());
    for (const CameraPathKeyframe& keyframe : path.keyframes) {
        hash.addDouble(keyframe.timeSeconds);
        hash.addDouble(keyframe.position.x);
        hash.addDouble(keyframe.position.y);
        hash.addDouble(keyframe.position.z);
        hash.addDouble(keyframe.lookAt.x);
        hash.addDouble(keyframe.lookAt.y);
        hash.addDouble(keyframe.lookAt.z);
        hash.addDouble(keyframe.up.x);
        hash.addDouble(keyframe.up.y);
        hash.addDouble(keyframe.up.z);
        hash.addDouble(keyframe.verticalFovDegrees);
    }
    return hash.value();
}

[[nodiscard]] CameraPathLoadResult parseCameraPathRoot(const Json& root) {
    CameraPathLoadResult result;
    if (!root.is_object()) {
        result.error = CameraPathError::InvalidRoot;
        result.detail = "root";
        return result;
    }
    result.error = validateFields(root, {"kind", "version", "id", "coordinate_system", "interpolation", "keyframes"},
                                  "root", result.detail);
    if (result.error != CameraPathError::None) {
        return result;
    }

    std::string kind;
    if (!readString(root, "kind", "root.kind", kind, result)) {
        return result;
    }
    if (kind != kCameraPathContractKind) {
        result.error = CameraPathError::InvalidKind;
        result.detail = "root.kind";
        return result;
    }

    uint32_t version = 0u;
    if (!readVersion(root, version, result)) {
        return result;
    }
    if (version != kCameraPathContractVersion) {
        result.error = CameraPathError::UnsupportedVersion;
        result.detail = "root.version";
        return result;
    }

    if (!readString(root, "id", "root.id", result.path.id, result)) {
        return result;
    }
    if (!validIdentifier(result.path.id)) {
        result.error = CameraPathError::InvalidIdentifier;
        result.detail = "root.id";
        return result;
    }

    std::string coordinateSystem;
    if (!readString(root, "coordinate_system", "root.coordinate_system", coordinateSystem, result)) {
        return result;
    }
    if (coordinateSystem != kCameraPathCoordinateSystem) {
        result.error = CameraPathError::InvalidCoordinateSystem;
        result.detail = "root.coordinate_system";
        return result;
    }

    std::string interpolation;
    if (!readString(root, "interpolation", "root.interpolation", interpolation, result)) {
        return result;
    }
    if (interpolation != kCameraPathInterpolation) {
        result.error = CameraPathError::InvalidInterpolation;
        result.detail = "root.interpolation";
        return result;
    }

    const Json& keyframes = *root.find("keyframes");
    if (!keyframes.is_array()) {
        result.error = CameraPathError::InvalidFieldType;
        result.detail = "root.keyframes";
        return result;
    }
    if (keyframes.size() < 2u || keyframes.size() > kMaximumKeyframeCount) {
        result.error = CameraPathError::InvalidKeyframeCount;
        result.detail = "root.keyframes";
        return result;
    }

    result.path.keyframes.reserve(keyframes.size());
    double previousTime = -1.0;
    for (size_t index = 0u; index < keyframes.size(); ++index) {
        const Json& keyframeJson = keyframes[index];
        const std::string keyframePath = "root.keyframes[" + std::to_string(index) + "]";
        if (!keyframeJson.is_object()) {
            result.error = CameraPathError::InvalidFieldType;
            result.detail = keyframePath;
            return result;
        }
        result.error =
            validateFields(keyframeJson, {"time_seconds", "position", "look_at", "up", "vertical_fov_degrees"},
                           keyframePath, result.detail);
        if (result.error != CameraPathError::None) {
            return result;
        }

        CameraPathKeyframe keyframe;
        if (!readFiniteNumber(*keyframeJson.find("time_seconds"), CameraPathError::InvalidKeyframeTime,
                              keyframePath + ".time_seconds", keyframe.timeSeconds, result)) {
            return result;
        }
        if ((index == 0u && keyframe.timeSeconds != 0.0) || (index != 0u && keyframe.timeSeconds <= previousTime)) {
            result.error = CameraPathError::InvalidKeyframeTime;
            result.detail = keyframePath + ".time_seconds";
            return result;
        }
        if (!readVector(*keyframeJson.find("position"), CameraPathError::InvalidPosition, keyframePath + ".position",
                        keyframe.position, result) ||
            !readVector(*keyframeJson.find("look_at"), CameraPathError::InvalidLookAt, keyframePath + ".look_at",
                        keyframe.lookAt, result) ||
            !readVector(*keyframeJson.find("up"), CameraPathError::InvalidUpVector, keyframePath + ".up", keyframe.up,
                        result) ||
            !readFiniteNumber(*keyframeJson.find("vertical_fov_degrees"), CameraPathError::InvalidFieldOfView,
                              keyframePath + ".vertical_fov_degrees", keyframe.verticalFovDegrees, result)) {
            return result;
        }
        if (!finiteVector(keyframe.position)) {
            result.error = CameraPathError::InvalidPosition;
            result.detail = keyframePath + ".position";
            return result;
        }
        if (keyframe.verticalFovDegrees <= 1.0 || keyframe.verticalFovDegrees >= 179.0) {
            result.error = CameraPathError::InvalidFieldOfView;
            result.detail = keyframePath + ".vertical_fov_degrees";
            return result;
        }
        if (!buildOrientation(keyframe, index, result)) {
            return result;
        }
        previousTime = keyframe.timeSeconds;
        result.path.keyframes.push_back(keyframe);
    }

    result.path.durationSeconds = result.path.keyframes.back().timeSeconds;
    if (result.path.durationSeconds <= 0.0) {
        result.error = CameraPathError::InvalidKeyframeTime;
        result.detail = "root.keyframes";
        return result;
    }
    result.path.contentHash = hashPath(result.path);
    return result;
}
} // namespace

bool CameraPath::isValid() const {
    return !id.empty() && keyframes.size() >= 2u && std::isfinite(durationSeconds) && durationSeconds > 0.0 &&
           keyframes.front().timeSeconds == 0.0 && keyframes.back().timeSeconds == durationSeconds;
}

bool CameraPathLoadResult::succeeded() const {
    return error == CameraPathError::None && path.isValid();
}

const char* cameraPathErrorStableId(const CameraPathError error) {
    switch (error) {
    case CameraPathError::None: return "None";
    case CameraPathError::FileOpenFailed: return "FileOpenFailed";
    case CameraPathError::FileReadFailed: return "FileReadFailed";
    case CameraPathError::InvalidJson: return "InvalidJson";
    case CameraPathError::InvalidRoot: return "InvalidRoot";
    case CameraPathError::MissingField: return "MissingField";
    case CameraPathError::UnexpectedField: return "UnexpectedField";
    case CameraPathError::InvalidFieldType: return "InvalidFieldType";
    case CameraPathError::InvalidKind: return "InvalidKind";
    case CameraPathError::UnsupportedVersion: return "UnsupportedVersion";
    case CameraPathError::InvalidIdentifier: return "InvalidIdentifier";
    case CameraPathError::InvalidCoordinateSystem: return "InvalidCoordinateSystem";
    case CameraPathError::InvalidInterpolation: return "InvalidInterpolation";
    case CameraPathError::InvalidKeyframeCount: return "InvalidKeyframeCount";
    case CameraPathError::InvalidKeyframeTime: return "InvalidKeyframeTime";
    case CameraPathError::InvalidPosition: return "InvalidPosition";
    case CameraPathError::InvalidLookAt: return "InvalidLookAt";
    case CameraPathError::InvalidUpVector: return "InvalidUpVector";
    case CameraPathError::InvalidFieldOfView: return "InvalidFieldOfView";
    case CameraPathError::InvalidPathState: return "InvalidPathState";
    case CameraPathError::SampleTimeNotFinite: return "SampleTimeNotFinite";
    case CameraPathError::SampleTimeOutOfRange: return "SampleTimeOutOfRange";
    }
    std::abort();
}

CameraPathLoadResult parseCameraPathJson(const std::string_view jsonText) {
    const Json root = Json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
    if (root.is_discarded()) {
        CameraPathLoadResult result;
        result.error = CameraPathError::InvalidJson;
        result.detail = "root";
        return result;
    }
    return parseCameraPathRoot(root);
}

CameraPathLoadResult loadCameraPath(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        CameraPathLoadResult result;
        result.error = CameraPathError::FileOpenFailed;
        result.detail = path.generic_u8string();
        return result;
    }
    const Json root = Json::parse(input, nullptr, false);
    if (input.bad()) {
        CameraPathLoadResult result;
        result.error = CameraPathError::FileReadFailed;
        result.detail = path.generic_u8string();
        return result;
    }
    if (root.is_discarded()) {
        CameraPathLoadResult result;
        result.error = CameraPathError::InvalidJson;
        result.detail = path.generic_u8string();
        return result;
    }
    return parseCameraPathRoot(root);
}

CameraPathError sampleCameraPath(const CameraPath& path, const double timeSeconds, CameraPathPose& pose) {
    if (!path.isValid()) {
        return CameraPathError::InvalidPathState;
    }
    if (!std::isfinite(timeSeconds)) {
        return CameraPathError::SampleTimeNotFinite;
    }
    if (timeSeconds < 0.0 || timeSeconds > path.durationSeconds) {
        return CameraPathError::SampleTimeOutOfRange;
    }

    if (timeSeconds == path.durationSeconds) {
        const CameraPathKeyframe& keyframe = path.keyframes.back();
        pose.position = keyframe.position;
        pose.forward = keyframe.orientation * glm::dvec3(0.0, 0.0, -1.0);
        pose.up = keyframe.orientation * glm::dvec3(0.0, 1.0, 0.0);
        pose.verticalFovDegrees = keyframe.verticalFovDegrees;
        return CameraPathError::None;
    }

    const auto upper = std::upper_bound(
        path.keyframes.begin(), path.keyframes.end(), timeSeconds,
        [](const double time, const CameraPathKeyframe& keyframe) { return time < keyframe.timeSeconds; });
    if (upper == path.keyframes.begin() || upper == path.keyframes.end()) {
        return CameraPathError::InvalidPathState;
    }
    const CameraPathKeyframe& next = *upper;
    const CameraPathKeyframe& previous = *(upper - 1);
    const double interval = next.timeSeconds - previous.timeSeconds;
    if (!std::isfinite(interval) || interval <= 0.0) {
        return CameraPathError::InvalidPathState;
    }
    const double factor = (timeSeconds - previous.timeSeconds) / interval;
    const glm::dquat orientation = glm::normalize(glm::slerp(previous.orientation, next.orientation, factor));
    if (!finiteQuaternion(orientation)) {
        return CameraPathError::InvalidPathState;
    }
    pose.position = previous.position + (next.position - previous.position) * factor;
    pose.forward = orientation * glm::dvec3(0.0, 0.0, -1.0);
    pose.up = orientation * glm::dvec3(0.0, 1.0, 0.0);
    pose.verticalFovDegrees =
        previous.verticalFovDegrees + (next.verticalFovDegrees - previous.verticalFovDegrees) * factor;
    return CameraPathError::None;
}

std::string cameraPathContentHashHex(const uint64_t contentHash) {
    return stableContentHashHex(contentHash);
}

} // namespace renderer::contracts
