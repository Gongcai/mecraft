#ifndef MECRAFT_TEXTURE_CAPTURE_H
#define MECRAFT_TEXTURE_CAPTURE_H

#include "renderer/rhi/RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class RhiCommandListPool;
class RhiDevice;

namespace renderer::capture {

/// Identifies the row origin of texels returned by a graphics backend.
enum class TextureCaptureOrigin : uint8_t { TopLeft, BottomLeft };

/// Identifies every deterministic texture readback or PNG writing failure.
enum class TextureCaptureError : uint8_t {
    None,
    InvalidRequest,
    UnsupportedFormat,
    SizeOverflow,
    ReadbackAllocationFailed,
    CommandListAcquireFailed,
    CommandListBeginFailed,
    CommandListEndFailed,
    SubmissionFailed,
    SubmissionWaitFailed,
    BufferMapFailed,
    PixelNormalizationFailed,
    RadianceScalingFailed,
    OutputDirectoryFailed,
    PngWriteFailed,
    ExrWriteFailed,
    ExrReadFailed
};

/// Describes one synchronous 8-bit RGBA or BGRA texture capture.
struct TextureCaptureRequest {
    RhiTextureHandle sourceTexture;
    RhiResourceState sourceState = RhiResourceState::Undefined;
    RhiTextureFormat sourceFormat = RhiTextureFormat::Undefined;
    uint32_t width = 0u;
    uint32_t height = 0u;
    TextureCaptureOrigin origin = TextureCaptureOrigin::TopLeft;
    float linearRgbScale = 1.0f;
    std::filesystem::path outputPath;
};

/// Returns a stable capture status and field-specific failure detail.
struct TextureCaptureResult {
    TextureCaptureError error = TextureCaptureError::None;
    std::string detail;

    /// Reports whether texture readback and PNG writing both succeeded.
    /// @return True only when error is None.
    [[nodiscard]] bool succeeded() const;
};

/// Stores one tightly packed top-left linear RGB half-float OpenEXR image.
struct LinearExrImage final {
    uint32_t width = 0u;
    uint32_t height = 0u;
    std::vector<uint16_t> rgb16f;
};

/// Converts padded backend rows into tightly packed top-left RGBA pixels.
/// @param source Raw mapped readback bytes.
/// @param sourceSize Number of readable bytes at source.
/// @param bytesPerRow Backend-aligned byte stride between source rows.
/// @param width Image width in texels.
/// @param height Image height in texels.
/// @param format Source channel ordering; only 8-bit RGBA/BGRA is accepted.
/// @param origin Row origin used by the source backend.
/// @param rgba Receives width * height * 4 tightly packed bytes.
/// @return Stable normalization error, or None on success.
[[nodiscard]] TextureCaptureError normalizeTextureCapturePixels(const uint8_t* source, size_t sourceSize,
                                                                uint32_t bytesPerRow, uint32_t width, uint32_t height,
                                                                RhiTextureFormat format, TextureCaptureOrigin origin,
                                                                std::vector<uint8_t>& rgba);

/// Converts padded RGBA16F backend rows into tightly packed top-left half-float pixels.
/// @param source Raw mapped readback bytes.
/// @param sourceSize Number of readable bytes at source.
/// @param bytesPerRow Backend-aligned byte stride between source rows.
/// @param width Image width in texels.
/// @param height Image height in texels.
/// @param format Source texture format; only RGBA16F is accepted.
/// @param origin Row origin used by the source backend.
/// @param rgba16f Receives width * height * 4 IEEE-754 half-float channel bit patterns.
/// @return Stable normalization error, or None on success.
[[nodiscard]] TextureCaptureError normalizeTextureCaptureHalfPixels(const uint8_t* source, size_t sourceSize,
                                                                    uint32_t bytesPerRow, uint32_t width,
                                                                    uint32_t height, RhiTextureFormat format,
                                                                    TextureCaptureOrigin origin,
                                                                    std::vector<uint16_t>& rgba16f);

/// Applies a finite positive scale to RGB channels in tightly packed RGBA16F pixels.
/// @param rgba16f In-place IEEE-754 half-float pixels; alpha channels are preserved.
/// @param scale Scene-linear RGB multiplier required by the output-domain contract.
/// @return None when every scaled RGB channel remains finite, non-negative, and representable by FP16.
[[nodiscard]] TextureCaptureError scaleTextureCaptureHalfPixels(std::vector<uint16_t>& rgba16f, float scale);

/// Writes a linear RGB OpenEXR scanline image from tightly packed RGBA16F pixels.
/// @param outputPath Lowercase .exr output path.
/// @param width Image width in texels.
/// @param height Image height in texels.
/// @param rgba16f Top-left RGBA16F pixels represented by IEEE-754 half bit patterns.
/// @return Stable capture result describing EXR directory or write failure.
[[nodiscard]] TextureCaptureResult writeLinearExr(const std::filesystem::path& outputPath, uint32_t width,
                                                  uint32_t height, const std::vector<uint16_t>& rgba16f);

/// Reads the strict uncompressed RGB Half scanline OpenEXR layout emitted by writeLinearExr.
/// @param inputPath Lowercase .exr source path.
/// @param image Receives tightly packed top-left RGB half-float channel bit patterns.
/// @return Stable capture result describing file or format validation failure.
[[nodiscard]] TextureCaptureResult readLinearExr(const std::filesystem::path& inputPath, LinearExrImage& image);

/// Reads one texture synchronously and writes a deterministic PNG file.
/// @param rhiDevice Device that owns the source texture and readback buffer.
/// @param commandListPool Graphics command-list pool for copy submission.
/// @param request Complete source-state, format, extent, and output contract.
/// @return Stable capture result with operation-specific detail.
[[nodiscard]] TextureCaptureResult captureTextureToPng(RhiDevice& rhiDevice, RhiCommandListPool& commandListPool,
                                                       const TextureCaptureRequest& request);

/// Reads one RGBA16F texture synchronously and writes a linear RGB OpenEXR file.
/// @param rhiDevice Device that owns the source texture and readback buffer.
/// @param commandListPool Graphics command-list pool for copy submission.
/// @param request Complete source-state, RGBA16F format, extent, and output contract.
/// @return Stable capture result with operation-specific detail.
[[nodiscard]] TextureCaptureResult captureTextureToExr(RhiDevice& rhiDevice, RhiCommandListPool& commandListPool,
                                                       const TextureCaptureRequest& request);

/// Returns the stable identifier used by validation logs and reports.
/// @param error Texture capture error to identify.
/// @return Process-lifetime string containing the stable error identifier.
[[nodiscard]] const char* textureCaptureErrorStableId(TextureCaptureError error);

} // namespace renderer::capture

#endif // MECRAFT_TEXTURE_CAPTURE_H
