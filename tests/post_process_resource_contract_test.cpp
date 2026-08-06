#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] std::string_view functionBody(const std::string& source, const char* signature,
                                            const char* nextSignature) {
    const size_t begin = source.find(signature);
    const size_t end = begin == std::string::npos ? std::string::npos : source.find(nextSignature, begin);
    if (begin == std::string::npos || end == std::string::npos || end <= begin) {
        return {};
    }
    return std::string_view(source).substr(begin, end - begin);
}

} // namespace

int main() {
    const std::string passPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/src/renderer/passes/PostProcessPass.cpp";
    std::ifstream passFile(passPath, std::ios::binary);
    if (!requireTrue(passFile.is_open(), "PostProcessPass source must be readable")) {
        return 1;
    }

    const std::string source{std::istreambuf_iterator<char>(passFile), std::istreambuf_iterator<char>()};
    const std::string_view setHdrInput =
        functionBody(source, "bool PostProcessPass::setHdrInput(", "bool PostProcessPass::prepareTextureOutput(");
    const std::string_view ensureExposure = functionBody(source, "bool PostProcessPass::ensureExposureState(",
                                                         "bool PostProcessPass::ensureProcessingTargets(");
    const std::string_view ensureProcessing = functionBody(source, "bool PostProcessPass::ensureProcessingTargets(",
                                                           "bool PostProcessPass::ensureCompositeTarget(");
    const std::string_view destroyExposure = functionBody(source, "void PostProcessPass::destroyExposureState(",
                                                          "void PostProcessPass::destroyProcessingTargets(");
    const std::string_view destroyProcessing = functionBody(source, "void PostProcessPass::destroyProcessingTargets(",
                                                            "void PostProcessPass::destroyRhiResources(");

    const bool inputContract = !setHdrInput.empty() &&
                               setHdrInput.find("ensureExposureState(*m_rhiDevice)") != std::string_view::npos &&
                               setHdrInput.find("ensureProcessingTargets") == std::string_view::npos;
    const bool allocationContract =
        !ensureExposure.empty() && !ensureProcessing.empty() &&
        ensureExposure.find("\"PostProcess.ExposureState\"") != std::string_view::npos &&
        ensureProcessing.find("!ensureExposureState(rhiDevice)") != std::string_view::npos &&
        ensureProcessing.find("\"PostProcess.ExposureState\"") == std::string_view::npos;
    const bool destructionContract =
        !destroyExposure.empty() && !destroyProcessing.empty() &&
        destroyExposure.find("m_exposureStateHandle") != std::string_view::npos &&
        destroyExposure.find("destroyExposureReadbackBuffers();") != std::string_view::npos &&
        destroyProcessing.find("m_exposureStateHandle") == std::string_view::npos &&
        destroyProcessing.find("destroyExposureReadbackBuffers();") == std::string_view::npos;

    return requireTrue(inputContract, "HDR input changes must preserve size-independent exposure history") &&
                   requireTrue(allocationContract,
                               "exposure state allocation must remain separate from sized processing targets") &&
                   requireTrue(destructionContract,
                               "sized target destruction must not destroy exposure history or readback state")
               ? 0
               : 1;
}
