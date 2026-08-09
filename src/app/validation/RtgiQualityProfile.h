#ifndef MECRAFT_RTGI_QUALITY_PROFILE_H
#define MECRAFT_RTGI_QUALITY_PROFILE_H

#include "app/AppLaunchOptions.h"
#include "renderer/contracts/RtgiQualityValidationContract.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace app::validation {

inline constexpr const char* kRtgiQualityProfileKind = "mecraft.rtgi_quality_profiles";
inline constexpr uint32_t kRtgiQualityProfileVersion = 1u;
inline constexpr uint32_t kRtgiQualitySequenceFrameCount = 32u;
inline constexpr uint32_t kRtgiQualityReferenceSpp = 64u;

/// Describes one versioned static RTGI quality measurement region.
struct RtgiQualityProfile final {
    std::string id;
    uint32_t version = 0u;
    std::string sceneContractId;
    std::string renderSettingsId;
    double cameraTimeSeconds = 0.0;
    uint32_t captureWidth = 0u;
    uint32_t captureHeight = 0u;
    renderer::contracts::RtgiValidationRoi roi;
};

/// Identifies a deterministic static-quality profile loading failure.
enum class RtgiQualityProfileError : uint8_t {
    None,
    FileOpenFailed,
    JsonParseFailed,
    InvalidRoot,
    MissingProfile,
    InvalidProfile
};

/// Returns a versioned static RTGI quality profile by stable identifier.
/// @param sourcePath JSON profile manifest located beside validation scene descriptors.
/// @param profileId Requested stable profile identifier.
/// @param profile Receives the parsed profile on success.
/// @param detail Receives a field-specific failure detail.
/// @return None on success, otherwise a deterministic loading error.
[[nodiscard]] RtgiQualityProfileError loadRtgiQualityProfile(const std::filesystem::path& sourcePath,
                                                             const std::string& profileId, RtgiQualityProfile& profile,
                                                             std::string& detail);

/// Returns the stable diagnostic identifier for a profile loading error.
[[nodiscard]] const char* rtgiQualityProfileErrorStableId(RtgiQualityProfileError error);

} // namespace app::validation

#endif // MECRAFT_RTGI_QUALITY_PROFILE_H
