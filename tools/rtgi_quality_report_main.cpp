#include "app/validation/RtgiQualityReport.h"

#include <array>
#include <iostream>
#include <string_view>

namespace {

void printUsage() {
    std::cerr << "Usage: rtgi_quality_report_tool --profile-manifest <path> --profile <id> "
                 "--quality-dir <path> --reference-dir <path> --reference-output <path.exr> "
                 "--validation-report <path.json> --report-output <path.json>\n";
}

} // namespace

int main(const int argc, const char* const* argv) {
    app::validation::RtgiQualityReportRequest request;
    if (argc != 15) {
        printUsage();
        return 2;
    }
    for (int argument = 1; argument < argc; argument += 2) {
        const std::string_view option = argv[argument];
        const char* const value = argv[argument + 1];
        if (option == "--profile-manifest") {
            request.profileManifestPath = value;
        } else if (option == "--profile") {
            request.profileId = value;
        } else if (option == "--quality-dir") {
            request.qualitySequenceDirectory = value;
        } else if (option == "--reference-dir") {
            request.referenceSequenceDirectory = value;
        } else if (option == "--reference-output") {
            request.referenceOutputPath = value;
        } else if (option == "--validation-report") {
            request.validationCaptureReportPath = value;
        } else if (option == "--report-output") {
            request.reportOutputPath = value;
        } else {
            printUsage();
            return 2;
        }
    }

    app::validation::RtgiQualityReportSummary summary;
    std::string detail;
    const app::validation::RtgiQualityReportError error =
        app::validation::generateRtgiQualityReport(request, summary, detail);
    if (error != app::validation::RtgiQualityReportError::None) {
        std::cerr << app::validation::rtgiQualityReportErrorStableId(error) << ": " << detail << '\n';
        return 1;
    }
    std::cout << "RTGI quality report written: " << request.reportOutputPath << '\n'
              << "64 spp reference written: " << request.referenceOutputPath << '\n'
              << "Available metrics passed: " << (summary.availableMetricsPassed ? "true" : "false") << '\n'
              << "Complete static gate passed: " << (summary.completeStaticGatePassed ? "true" : "false") << '\n';
    return 0;
}
