#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

bool requireTrue(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

bool testVersion() {
    const FfxVersionNumber expected = FFX_SDK_MAKE_VERSION(3u, 1u, 4u);
    return requireTrue(ffxFsr3UpscalerGetEffectVersion() == expected,
                       "FidelityFX FSR upscaler version must be 3.1.4");
}

bool testRenderExtentQueries() {
    uint32_t width = 0u;
    uint32_t height = 0u;
    if (!requireTrue(ffxFsr3UpscalerGetRenderResolutionFromQualityMode(
                         &width, &height, 1920u, 1080u,
                         FFX_FSR3UPSCALER_QUALITY_MODE_QUALITY) == FFX_OK &&
                         width == 1280u && height == 720u,
                     "FSR Quality extent query must return 1280x720 for 1080p")) {
        return false;
    }
    if (!requireTrue(ffxFsr3UpscalerGetRenderResolutionFromQualityMode(
                         &width, &height, 1920u, 1080u,
                         FFX_FSR3UPSCALER_QUALITY_MODE_BALANCED) == FFX_OK &&
                         width == 1129u && height == 635u,
                     "FSR Balanced extent query must use the SDK ratio")) {
        return false;
    }
    if (!requireTrue(ffxFsr3UpscalerGetRenderResolutionFromQualityMode(
                         &width, &height, 1920u, 1080u,
                         FFX_FSR3UPSCALER_QUALITY_MODE_PERFORMANCE) == FFX_OK &&
                         width == 960u && height == 540u,
                     "FSR Performance extent query must return 960x540 for 1080p")) {
        return false;
    }
    return requireTrue(ffxFsr3UpscalerGetRenderResolutionFromQualityMode(
                           &width, &height, 1920u, 1080u,
                           static_cast<FfxFsr3UpscalerQualityMode>(99)) ==
                           static_cast<FfxErrorCode>(FFX_ERROR_INVALID_ENUM),
                       "FSR extent query must reject invalid quality modes");
}

bool testJitterQueries() {
    const int32_t phaseCount = ffxFsr3UpscalerGetJitterPhaseCount(1280, 1920);
    float jitterX = 0.0f;
    float jitterY = 0.0f;
    if (!requireTrue(phaseCount == 18,
                     "FSR Quality jitter phase count must be 18") ||
        !requireTrue(ffxFsr3UpscalerGetJitterOffset(
                         &jitterX, &jitterY, 0, phaseCount) == FFX_OK,
                     "FSR jitter query must accept a valid phase")) {
        return false;
    }
    return requireTrue(std::abs(jitterX) < 0.000001f &&
                           std::abs(jitterY + 1.0f / 6.0f) < 0.000001f,
                       "FSR jitter query must return the official Halton sequence");
}

} // namespace

int main() {
    if (!testVersion()) return 1;
    if (!testRenderExtentQueries()) return 1;
    if (!testJitterQueries()) return 1;
    return 0;
}
