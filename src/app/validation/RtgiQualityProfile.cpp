#include "app/validation/RtgiQualityProfile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string_view>

namespace app::validation {
namespace {

using Json = nlohmann::json;

[[nodiscard]] bool validIdentifier(const std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](const char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
               character == '-' || character == '.';
    });
}

[[nodiscard]] bool readUint32(const Json& value, uint32_t& output) {
    if (!value.is_number_unsigned() || value.get<uint64_t>() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    output = static_cast<uint32_t>(value.get<uint64_t>());
    return true;
}

[[nodiscard]] bool readProfile(const Json& source, RtgiQualityProfile& profile) {
    if (!source.is_object() || source.size() != 8u || !source.contains("id") || !source.contains("version") ||
        !source.contains("scene_contract_id") || !source.contains("render_settings_id") ||
        !source.contains("camera_time_seconds") || !source.contains("capture_width") ||
        !source.contains("capture_height") || !source.contains("roi")) {
        return false;
    }
    if (!source.at("id").is_string() || !source.at("scene_contract_id").is_string() ||
        !source.at("render_settings_id").is_string() || !source.at("camera_time_seconds").is_number() ||
        !readUint32(source.at("version"), profile.version) ||
        !readUint32(source.at("capture_width"), profile.captureWidth) ||
        !readUint32(source.at("capture_height"), profile.captureHeight)) {
        return false;
    }
    profile.id = source.at("id").get<std::string>();
    profile.sceneContractId = source.at("scene_contract_id").get<std::string>();
    profile.renderSettingsId = source.at("render_settings_id").get<std::string>();
    profile.cameraTimeSeconds = source.at("camera_time_seconds").get<double>();
    const Json& roi = source.at("roi");
    if (!roi.is_object() || roi.size() != 4u || !roi.contains("x") || !roi.contains("y") || !roi.contains("width") ||
        !roi.contains("height") || !readUint32(roi.at("x"), profile.roi.x) || !readUint32(roi.at("y"), profile.roi.y) ||
        !readUint32(roi.at("width"), profile.roi.width) || !readUint32(roi.at("height"), profile.roi.height)) {
        return false;
    }
    return profile.version == kRtgiQualityProfileVersion && validIdentifier(profile.id) &&
           validIdentifier(profile.sceneContractId) && validIdentifier(profile.renderSettingsId) &&
           std::isfinite(profile.cameraTimeSeconds) && profile.cameraTimeSeconds >= 0.0 && profile.captureWidth != 0u &&
           profile.captureHeight != 0u && profile.roi.width != 0u && profile.roi.height != 0u &&
           profile.roi.x <= profile.captureWidth && profile.roi.y <= profile.captureHeight &&
           profile.roi.width <= profile.captureWidth - profile.roi.x &&
           profile.roi.height <= profile.captureHeight - profile.roi.y;
}

} // namespace

RtgiQualityProfileError loadRtgiQualityProfile(const std::filesystem::path& sourcePath, const std::string& profileId,
                                               RtgiQualityProfile& profile, std::string& detail) {
    profile = {};
    detail.clear();
    if (!validIdentifier(profileId)) {
        detail = "profile_id";
        return RtgiQualityProfileError::InvalidProfile;
    }
    std::ifstream input(sourcePath);
    if (!input.is_open()) {
        detail = sourcePath.generic_u8string();
        return RtgiQualityProfileError::FileOpenFailed;
    }
    const Json root = Json::parse(input, nullptr, false);
    if (root.is_discarded()) {
        detail = sourcePath.generic_u8string();
        return RtgiQualityProfileError::JsonParseFailed;
    }
    if (!root.is_object() || root.size() != 3u || !root.contains("kind") || !root.contains("version") ||
        !root.contains("profiles") || !root.at("kind").is_string() ||
        root.at("kind").get<std::string>() != kRtgiQualityProfileKind || !root.at("profiles").is_array()) {
        detail = "root";
        return RtgiQualityProfileError::InvalidRoot;
    }
    uint32_t version = 0u;
    if (!readUint32(root.at("version"), version) || version != kRtgiQualityProfileVersion) {
        detail = "root.version";
        return RtgiQualityProfileError::InvalidRoot;
    }
    for (const Json& candidate : root.at("profiles")) {
        if (!candidate.is_object() || !candidate.contains("id") || !candidate.at("id").is_string() ||
            candidate.at("id").get<std::string>() != profileId) {
            continue;
        }
        if (!readProfile(candidate, profile)) {
            detail = "profiles." + profileId;
            return RtgiQualityProfileError::InvalidProfile;
        }
        return RtgiQualityProfileError::None;
    }
    detail = profileId;
    return RtgiQualityProfileError::MissingProfile;
}

const char* rtgiQualityProfileErrorStableId(const RtgiQualityProfileError error) {
    switch (error) {
    case RtgiQualityProfileError::None: return "None";
    case RtgiQualityProfileError::FileOpenFailed: return "FileOpenFailed";
    case RtgiQualityProfileError::JsonParseFailed: return "JsonParseFailed";
    case RtgiQualityProfileError::InvalidRoot: return "InvalidRoot";
    case RtgiQualityProfileError::MissingProfile: return "MissingProfile";
    case RtgiQualityProfileError::InvalidProfile: return "InvalidProfile";
    }
    std::abort();
}

} // namespace app::validation
