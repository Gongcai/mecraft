#ifndef MECRAFT_RTGI_QUALITY_REPORT_H
#define MECRAFT_RTGI_QUALITY_REPORT_H

#include "renderer/contracts/RtgiQualityValidationContract.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace app::validation {

inline constexpr const char* kRtgiQualityReportKind = "mecraft.rtgi_quality_report";
inline constexpr uint32_t kRtgiQualityReportVersion = 3u;
inline constexpr double kRtgiVarianceReductionThresholdPercent = 70.0;
inline constexpr double kRtgiLuminanceSsimThreshold = 0.95;
inline constexpr double kRtgiRelativeLuminanceErrorP95Threshold = 0.10;

/// Describes the two captured frame sequences and deterministic report outputs.
struct RtgiQualityReportRequest final {
    std::filesystem::path profileManifestPath;
    std::string profileId;
    std::filesystem::path qualitySequenceDirectory;
    std::filesystem::path referenceSequenceDirectory;
    std::filesystem::path referenceOutputPath;
    std::filesystem::path reportOutputPath;
};

/// Identifies a deterministic RTGI quality report generation failure.
enum class RtgiQualityReportError : uint8_t {
    None,
    InvalidRequest,
    ProfileLoadFailed,
    SequenceDirectoryReadFailed,
    MissingSequenceFrame,
    UnexpectedSequenceFrame,
    ExrReadFailed,
    ImageExtentMismatch,
    NonFiniteRadiance,
    NegativeRadiance,
    AveragedRadianceOutOfRange,
    ReferenceWriteFailed,
    MetricEvaluationFailed,
    ReportWriteFailed
};

/// Contains the measured gates and evidence state written to the JSON report.
struct RtgiQualityReportSummary final {
    renderer::contracts::RtgiTemporalVarianceMetrics variance;
    renderer::contracts::RtgiReferenceComparisonMetrics comparison;
    renderer::contracts::RtgiReferenceComparisonMetrics referenceConvergence;
    double rawMeanLuminance = 0.0;
    double denoisedMeanLuminance = 0.0;
    double referenceMeanLuminance = 0.0;
    bool varianceReductionPassed = false;
    bool luminanceSsimPassed = false;
    bool relativeLuminanceErrorPassed = false;
    bool referenceConvergencePassed = false;
    bool radianceValidationPassed = false;
    bool availableMetricsPassed = false;
    bool completeStaticGatePassed = false;
};

/// Generates one averaged 64 spp EXR and a structured quality report.
/// @param request Exact profile, input sequence, and output paths.
/// @param summary Receives evaluated metric and gate values on success.
/// @param detail Receives a field-specific diagnostic on failure.
/// @return None only after both output files have been written successfully.
[[nodiscard]] RtgiQualityReportError generateRtgiQualityReport(const RtgiQualityReportRequest& request,
                                                               RtgiQualityReportSummary& summary, std::string& detail);

/// Returns the stable diagnostic identifier for a report generation error.
[[nodiscard]] const char* rtgiQualityReportErrorStableId(RtgiQualityReportError error);

} // namespace app::validation

#endif // MECRAFT_RTGI_QUALITY_REPORT_H
