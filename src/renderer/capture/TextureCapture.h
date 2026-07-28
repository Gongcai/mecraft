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
enum class TextureCaptureOrigin : uint8_t {
    TopLeft,
    BottomLeft
};

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
    OutputDirectoryFailed,
    PngWriteFailed
};

/// Describes one synchronous 8-bit RGBA or BGRA texture capture.
struct TextureCaptureRequest {
    RhiTextureHandle sourceTexture;
    RhiResourceState sourceState = RhiResourceState::Undefined;
    RhiTextureFormat sourceFormat = RhiTextureFormat::Undefined;
    uint32_t width = 0u;
    uint32_t height = 0u;
    TextureCaptureOrigin origin = TextureCaptureOrigin::TopLeft;
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
[[nodiscard]] TextureCaptureError normalizeTextureCapturePixels(
    const uint8_t* source,
    size_t sourceSize,
    uint32_t bytesPerRow,
    uint32_t width,
    uint32_t height,
    RhiTextureFormat format,
    TextureCaptureOrigin origin,
    std::vector<uint8_t>& rgba);

/// Reads one texture synchronously and writes a deterministic PNG file.
/// @param rhiDevice Device that owns the source texture and readback buffer.
/// @param commandListPool Graphics command-list pool for copy submission.
/// @param request Complete source-state, format, extent, and output contract.
/// @return Stable capture result with operation-specific detail.
[[nodiscard]] TextureCaptureResult captureTextureToPng(
    RhiDevice& rhiDevice,
    RhiCommandListPool& commandListPool,
    const TextureCaptureRequest& request);

/// Returns the stable identifier used by validation logs and reports.
/// @param error Texture capture error to identify.
/// @return Process-lifetime string containing the stable error identifier.
[[nodiscard]] const char* textureCaptureErrorStableId(
    TextureCaptureError error);

} // namespace renderer::capture

#endif // MECRAFT_TEXTURE_CAPTURE_H
