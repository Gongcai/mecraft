#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::string readSource(const std::string& relativePath) {
    const std::string path = std::string(MECRAFT_TEST_SOURCE_DIR) + '/' + relativePath;
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool requireContains(const std::string& source, const std::string_view token,
                                   const char* const message) {
    if (source.find(token) != std::string::npos) {
        return true;
    }
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main() {
    const std::string renderScene = readSource("src/renderer/core/RenderScene.cpp");
    const std::string temporalUpscale = readSource("src/renderer/passes/TemporalUpscalePass.cpp");
    const std::string exposureShader = readSource("assets/shaders/fsr_exposure_normalize.frag");
    const std::string resourceContract =
        readSource("src/renderer/upscaling/Fsr31VulkanResourceContract.cpp");
    if (renderScene.empty() || temporalUpscale.empty() || exposureShader.empty() || resourceContract.empty()) {
        std::cerr << "FSR exposure contract sources must be readable\n";
        return 1;
    }

    const bool valid =
        requireContains(renderScene, "input.preExposure = m_currentContext.preExposure;",
                        "gameplay FSR input must use the scene pre-exposure") &&
        requireContains(renderScene, "input.previousPreExposure = m_currentContext.previousPreExposure;",
                        "gameplay FSR input must preserve the previous pre-exposure") &&
        requireContains(temporalUpscale, "textureDesc.format = RhiTextureFormat::R32Float;",
                        "FSR exposure normalization must allocate an R32F texture") &&
        requireContains(temporalUpscale, "fsrFrame.textures.exposure = m_fsrExposureTexture;",
                        "FSR dispatch must consume the normalized exposure texture") &&
        requireContains(temporalUpscale, "dispatch.dependsOn(normalizeExposure.handle())",
                        "FSR dispatch must wait for exposure normalization") &&
        requireContains(exposureShader, "FragExposure = sceneExposure / pPreExposure.x;",
                        "FSR exposure must remove the scene pre-exposure scale") &&
        requireContains(resourceContract, "VK_FORMAT_R32_SFLOAT",
                        "FSR exposure resource validation must require R32F");
    return valid ? 0 : 1;
}
