#include "renderer/capture/TextureCapture.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[texture_capture_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    constexpr uint32_t kWidth = 2u;
    constexpr uint32_t kHeight = 2u;
    constexpr uint32_t kBytesPerRow = 12u;
    const std::vector<uint8_t> bottomLeftBgra = {30u, 20u, 10u, 255u, 60u,  50u,  40u,  128u, 0u, 0u, 0u, 0u,
                                                 90u, 80u, 70u, 255u, 120u, 110u, 100u, 64u,  0u, 0u, 0u, 0u};
    std::vector<uint8_t> rgba;
    const renderer::capture::TextureCaptureError error = renderer::capture::normalizeTextureCapturePixels(
        bottomLeftBgra.data(), bottomLeftBgra.size(), kBytesPerRow, kWidth, kHeight, RhiTextureFormat::Bgra8Unorm,
        renderer::capture::TextureCaptureOrigin::BottomLeft, rgba);
    const std::vector<uint8_t> expected = {70u, 80u, 90u, 255u, 100u, 110u, 120u, 64u,
                                           10u, 20u, 30u, 255u, 40u,  50u,  60u,  128u};
    if (!requireTrue(error == renderer::capture::TextureCaptureError::None,
                     "valid padded BGRA pixels must normalize") ||
        !requireTrue(rgba == expected, "normalization must flip rows and swizzle BGRA to RGBA")) {
        return 1;
    }

    if (!requireTrue(renderer::capture::normalizeTextureCapturePixels(
                         bottomLeftBgra.data(), bottomLeftBgra.size(), 4u, kWidth, kHeight,
                         RhiTextureFormat::Bgra8Unorm, renderer::capture::TextureCaptureOrigin::TopLeft,
                         rgba) == renderer::capture::TextureCaptureError::InvalidRequest,
                     "a row stride smaller than one complete row must be rejected") ||
        !requireTrue(renderer::capture::normalizeTextureCapturePixels(
                         bottomLeftBgra.data(), bottomLeftBgra.size(), kBytesPerRow, kWidth, kHeight,
                         RhiTextureFormat::Rgba16Float, renderer::capture::TextureCaptureOrigin::TopLeft,
                         rgba) == renderer::capture::TextureCaptureError::UnsupportedFormat,
                     "non-eight-bit texture formats must be rejected explicitly")) {
        return 1;
    }

    std::cout << "[texture_capture_test] PASS\n";
    return 0;
}
