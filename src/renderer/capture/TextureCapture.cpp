#include "renderer/capture/TextureCapture.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiCommandListPool.h"
#include "renderer/rhi/RhiDevice.h"

#include <stb/stb_image_write.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>

namespace renderer::capture {
namespace {

constexpr uint32_t kBytesPerPixel = 4u;

[[nodiscard]] bool supportedFormat(const RhiTextureFormat format) {
    return format == RhiTextureFormat::Rgba8Unorm ||
           format == RhiTextureFormat::Rgba8Srgb ||
           format == RhiTextureFormat::Bgra8Unorm ||
           format == RhiTextureFormat::Bgra8Srgb;
}

[[nodiscard]] bool bgraFormat(const RhiTextureFormat format) {
    return format == RhiTextureFormat::Bgra8Unorm ||
           format == RhiTextureFormat::Bgra8Srgb;
}

[[nodiscard]] TextureCaptureResult failure(
    const TextureCaptureError error,
    std::string detail) {
    TextureCaptureResult result;
    result.error = error;
    result.detail = std::move(detail);
    return result;
}

} // namespace

bool TextureCaptureResult::succeeded() const {
    return error == TextureCaptureError::None;
}

TextureCaptureError normalizeTextureCapturePixels(
    const uint8_t* source,
    const size_t sourceSize,
    const uint32_t bytesPerRow,
    const uint32_t width,
    const uint32_t height,
    const RhiTextureFormat format,
    const TextureCaptureOrigin origin,
    std::vector<uint8_t>& rgba) {
    rgba.clear();
    if (source == nullptr || width == 0u || height == 0u ||
        !supportedFormat(format)) {
        return !supportedFormat(format)
            ? TextureCaptureError::UnsupportedFormat
            : TextureCaptureError::InvalidRequest;
    }
    const uint64_t tightRowBytes =
        static_cast<uint64_t>(width) * kBytesPerPixel;
    if (tightRowBytes > bytesPerRow) {
        return TextureCaptureError::InvalidRequest;
    }
    const uint64_t requiredSourceSize =
        static_cast<uint64_t>(height - 1u) * bytesPerRow + tightRowBytes;
    const uint64_t outputSize = tightRowBytes * height;
    if (requiredSourceSize > sourceSize ||
        outputSize > std::numeric_limits<size_t>::max()) {
        return TextureCaptureError::SizeOverflow;
    }

    rgba.resize(static_cast<size_t>(outputSize));
    const bool swizzleBgra = bgraFormat(format);
    for (uint32_t destinationY = 0u; destinationY < height;
         ++destinationY) {
        const uint32_t sourceY = origin == TextureCaptureOrigin::TopLeft
            ? destinationY
            : height - destinationY - 1u;
        const uint8_t* sourceRow = source +
            static_cast<uint64_t>(sourceY) * bytesPerRow;
        uint8_t* destinationRow = rgba.data() +
            static_cast<uint64_t>(destinationY) * tightRowBytes;
        for (uint32_t x = 0u; x < width; ++x) {
            const uint8_t* sourcePixel = sourceRow + x * kBytesPerPixel;
            uint8_t* destinationPixel =
                destinationRow + x * kBytesPerPixel;
            destinationPixel[0] = sourcePixel[swizzleBgra ? 2u : 0u];
            destinationPixel[1] = sourcePixel[1];
            destinationPixel[2] = sourcePixel[swizzleBgra ? 0u : 2u];
            destinationPixel[3] = sourcePixel[3];
        }
    }
    return TextureCaptureError::None;
}

TextureCaptureResult captureTextureToPng(
    RhiDevice& rhiDevice,
    RhiCommandListPool& commandListPool,
    const TextureCaptureRequest& request) {
    if (!request.sourceTexture.isValid() || request.width == 0u ||
        request.height == 0u ||
        request.sourceState == RhiResourceState::Undefined ||
        request.outputPath.empty() || request.outputPath.extension() != ".png") {
        return failure(TextureCaptureError::InvalidRequest,
                       "source texture, state, extent, and lowercase .png path are required");
    }
    if (!supportedFormat(request.sourceFormat)) {
        return failure(TextureCaptureError::UnsupportedFormat,
                       "only 8-bit RGBA and BGRA textures can be captured");
    }

    const uint64_t tightRowBytes =
        static_cast<uint64_t>(request.width) * kBytesPerPixel;
    if (request.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        request.height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        tightRowBytes > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return failure(TextureCaptureError::SizeOverflow,
                       "PNG dimensions or stride exceed the writer contract");
    }
    const uint64_t alignment = std::max<uint64_t>(
        1u, rhiDevice.capabilities().textureBufferCopyRowPitchAlignment);
    if (tightRowBytes > std::numeric_limits<uint64_t>::max() -
                            (alignment - 1u)) {
        return failure(TextureCaptureError::SizeOverflow,
                       "aligned capture row size overflowed");
    }
    const uint64_t bytesPerRow64 =
        ((tightRowBytes + alignment - 1u) / alignment) * alignment;
    if (bytesPerRow64 > std::numeric_limits<uint32_t>::max() ||
        request.height >
            std::numeric_limits<uint64_t>::max() / bytesPerRow64) {
        return failure(TextureCaptureError::SizeOverflow,
                       "capture readback size overflowed");
    }
    const uint32_t bytesPerRow = static_cast<uint32_t>(bytesPerRow64);
    const uint64_t readbackSize = bytesPerRow64 * request.height;
    if (readbackSize > std::numeric_limits<size_t>::max()) {
        return failure(TextureCaptureError::SizeOverflow,
                       "capture readback exceeds the host address space");
    }

    RhiBufferDesc readbackDesc;
    readbackDesc.debugName = "ValidationCapture.Readback";
    readbackDesc.size = readbackSize;
    readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                         rhiFlag(RhiBufferUsage::MapRead);
    readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
    readbackDesc.initialState = RhiResourceState::TransferDst;
    readbackDesc.memoryCategory = RhiMemoryCategory::Readback;
    const RhiBufferHandle readback =
        rhiDevice.createBuffer(readbackDesc, nullptr, 0u);
    if (!readback.isValid()) {
        return failure(TextureCaptureError::ReadbackAllocationFailed,
                       "failed to create the GPU-to-CPU readback buffer");
    }

    RhiCommandList* commandList =
        commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::CommandListAcquireFailed,
                       "failed to acquire a graphics command list");
    }
    if (!commandList->begin(
            {"ValidationCapture.Commands", RhiCommandListType::Graphics})) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::CommandListBeginFailed,
                       "failed to begin the capture command list");
    }

    commandList->textureBarrier({
        request.sourceTexture,
        request.sourceState,
        RhiResourceState::TransferSrc});
    RhiTextureBufferCopy copy;
    copy.srcTexture = request.sourceTexture;
    copy.dstBuffer = readback;
    copy.bytesPerRow = bytesPerRow;
    copy.rowsPerImage = request.height;
    copy.width = request.width;
    copy.height = request.height;
    commandList->copyTextureToBuffer(copy);
    commandList->bufferBarrier({
        readback,
        RhiResourceState::TransferDst,
        RhiResourceState::HostRead});
    commandList->textureBarrier({
        request.sourceTexture,
        RhiResourceState::TransferSrc,
        request.sourceState});
    if (!commandList->end()) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::CommandListEndFailed,
                       "capture command validation failed");
    }

    RhiCommandList* submitted[] = {commandList};
    RhiSubmissionToken token;
    if (!rhiDevice.submit(
            {"ValidationCapture.Submit", submitted, 1u,
             RhiQueueType::Graphics},
            &token)) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::SubmissionFailed,
                       "capture copy submission was rejected");
    }
    if (!rhiDevice.waitForSubmission(token)) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::SubmissionWaitFailed,
                       "capture copy submission did not complete");
    }

    const auto* mapped = static_cast<const uint8_t*>(
        rhiDevice.mapBuffer(readback, 0u, readbackSize));
    if (mapped == nullptr) {
        rhiDevice.destroyBuffer(readback);
        return failure(TextureCaptureError::BufferMapFailed,
                       "capture readback buffer mapping failed");
    }
    std::vector<uint8_t> rgba;
    const TextureCaptureError normalizationError =
        normalizeTextureCapturePixels(
            mapped, static_cast<size_t>(readbackSize), bytesPerRow,
            request.width, request.height, request.sourceFormat,
            request.origin, rgba);
    rhiDevice.unmapBuffer(readback);
    rhiDevice.destroyBuffer(readback);
    if (normalizationError != TextureCaptureError::None) {
        return failure(TextureCaptureError::PixelNormalizationFailed,
                       textureCaptureErrorStableId(normalizationError));
    }

    const std::filesystem::path parent = request.outputPath.parent_path();
    if (!parent.empty()) {
        std::error_code directoryError;
        std::filesystem::create_directories(parent, directoryError);
        if (directoryError) {
            return failure(TextureCaptureError::OutputDirectoryFailed,
                           directoryError.message());
        }
    }
    const std::string outputPath = request.outputPath.generic_u8string();
    const int writeResult = stbi_write_png(
        outputPath.c_str(), static_cast<int>(request.width),
        static_cast<int>(request.height), 4, rgba.data(),
        static_cast<int>(request.width * kBytesPerPixel));
    if (writeResult == 0) {
        return failure(TextureCaptureError::PngWriteFailed, outputPath);
    }
    return {};
}

const char* textureCaptureErrorStableId(const TextureCaptureError error) {
    switch (error) {
        case TextureCaptureError::None: return "None";
        case TextureCaptureError::InvalidRequest: return "InvalidRequest";
        case TextureCaptureError::UnsupportedFormat:
            return "UnsupportedFormat";
        case TextureCaptureError::SizeOverflow: return "SizeOverflow";
        case TextureCaptureError::ReadbackAllocationFailed:
            return "ReadbackAllocationFailed";
        case TextureCaptureError::CommandListAcquireFailed:
            return "CommandListAcquireFailed";
        case TextureCaptureError::CommandListBeginFailed:
            return "CommandListBeginFailed";
        case TextureCaptureError::CommandListEndFailed:
            return "CommandListEndFailed";
        case TextureCaptureError::SubmissionFailed: return "SubmissionFailed";
        case TextureCaptureError::SubmissionWaitFailed:
            return "SubmissionWaitFailed";
        case TextureCaptureError::BufferMapFailed: return "BufferMapFailed";
        case TextureCaptureError::PixelNormalizationFailed:
            return "PixelNormalizationFailed";
        case TextureCaptureError::OutputDirectoryFailed:
            return "OutputDirectoryFailed";
        case TextureCaptureError::PngWriteFailed: return "PngWriteFailed";
    }
    std::abort();
}

} // namespace renderer::capture
