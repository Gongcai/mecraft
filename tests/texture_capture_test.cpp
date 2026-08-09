#include "renderer/capture/TextureCapture.h"

#include <cstdint>
#include <array>
#include <filesystem>
#include <iostream>
#include <fstream>
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

    constexpr uint32_t kHalfWidth = 2u;
    constexpr uint32_t kHalfHeight = 2u;
    constexpr uint32_t kHalfBytesPerRow = 20u;
    const std::vector<uint8_t> paddedRgba16f = {0x00u, 0x3cu, 0x00u, 0x40u, 0x00u, 0x42u, 0x00u, 0x44u, 0xffu, 0xffu,
                                                0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0u,    0u,    0u,    0u,
                                                0x00u, 0x46u, 0x00u, 0x48u, 0x00u, 0x4au, 0x00u, 0x4cu, 0xffu, 0xffu,
                                                0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0u,    0u,    0u,    0u};
    std::vector<uint16_t> rgba16f;
    std::vector<uint16_t> invalidHalf;
    if (!requireTrue(renderer::capture::normalizeTextureCaptureHalfPixels(
                         paddedRgba16f.data(), paddedRgba16f.size(), kHalfBytesPerRow, kHalfWidth, kHalfHeight,
                         RhiTextureFormat::Rgba16Float, renderer::capture::TextureCaptureOrigin::BottomLeft,
                         rgba16f) == renderer::capture::TextureCaptureError::None,
                     "valid padded RGBA16F pixels must normalize") ||
        !requireTrue(rgba16f == std::vector<uint16_t>{0x4600u, 0x4800u, 0x4a00u, 0x4c00u, 0xffffu, 0xffffu, 0xffffu,
                                                      0xffffu, 0x3c00u, 0x4000u, 0x4200u, 0x4400u, 0xffffu, 0xffffu,
                                                      0xffffu, 0xffffu},
                     "half-float normalization must flip rows without changing channel bit patterns") ||
        !requireTrue(renderer::capture::normalizeTextureCaptureHalfPixels(
                         paddedRgba16f.data(), paddedRgba16f.size(), kHalfBytesPerRow, kHalfWidth, kHalfHeight,
                         RhiTextureFormat::Rgba8Unorm, renderer::capture::TextureCaptureOrigin::TopLeft,
                         invalidHalf) == renderer::capture::TextureCaptureError::UnsupportedFormat,
                     "non-RGBA16F formats must be rejected by the half-float path")) {
        return 1;
    }

    const std::filesystem::path exrPath = std::filesystem::temp_directory_path() / "mecraft_texture_capture_test.exr";
    const renderer::capture::TextureCaptureResult exrResult =
        renderer::capture::writeLinearExr(exrPath, kHalfWidth, kHalfHeight, rgba16f);
    renderer::capture::LinearExrImage exrImage;
    const renderer::capture::TextureCaptureResult exrReadResult = renderer::capture::readLinearExr(exrPath, exrImage);
    std::ifstream exrInput(exrPath, std::ios::binary);
    std::array<uint8_t, 8u> exrPrefix{};
    exrInput.read(reinterpret_cast<char*>(exrPrefix.data()), static_cast<std::streamsize>(exrPrefix.size()));
    const bool exrValid =
        exrResult.succeeded() && exrInput.gcount() == static_cast<std::streamsize>(exrPrefix.size()) &&
        exrPrefix[0] == 0x76u && exrPrefix[1] == 0x2fu && exrPrefix[2] == 0x31u && exrPrefix[3] == 0x01u &&
        exrPrefix[4] == 0x02u && exrPrefix[5] == 0x00u && exrPrefix[6] == 0x00u && exrPrefix[7] == 0x00u;
    if (!exrReadResult.succeeded()) {
        std::cerr << "[texture_capture_test] EXR read detail: " << exrReadResult.detail << '\n';
    }
    std::error_code removeError;
    std::filesystem::remove(exrPath, removeError);
    if (!requireTrue(
            exrValid && exrReadResult.succeeded() && exrImage.width == kHalfWidth && exrImage.height == kHalfHeight &&
                exrImage.rgb16f == std::vector<uint16_t>{0x4600u, 0x4800u, 0x4a00u, 0xffffu, 0xffffu, 0xffffu, 0x3c00u,
                                                         0x4000u, 0x4200u, 0xffffu, 0xffffu, 0xffffu} &&
                !removeError,
            "linear EXR writer and reader must round-trip RGB half-float scanlines")) {
        return 1;
    }

    std::cout << "[texture_capture_test] PASS\n";
    return 0;
}
