#include "TextureResampler.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void failTextureResampler(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

void validateResampleInput(const unsigned char* sourcePixels,
                           const int sourceWidth,
                           const int sourceHeight,
                           const int sourceRowStridePixels,
                           const int targetWidth,
                           const int targetHeight) {
    if (sourcePixels == nullptr) {
        failTextureResampler("Texture resampler source pixels are null");
    }
    if (sourceWidth <= 0 || sourceHeight <= 0 || sourceRowStridePixels < sourceWidth) {
        failTextureResampler("Texture resampler source dimensions are invalid");
    }
    if (targetWidth <= 0 || targetHeight <= 0) {
        failTextureResampler("Texture resampler target dimensions are invalid");
    }
}

std::vector<unsigned char> copyRgba8(const unsigned char* sourcePixels,
                                     const int sourceWidth,
                                     const int sourceHeight,
                                     const int sourceRowStridePixels) {
    std::vector<unsigned char> output(static_cast<size_t>(sourceWidth) *
                                      static_cast<size_t>(sourceHeight) * 4U);
    for (int y = 0; y < sourceHeight; ++y) {
        const unsigned char* sourceRow =
            sourcePixels + static_cast<size_t>(y * sourceRowStridePixels) * 4U;
        unsigned char* targetRow = output.data() + static_cast<size_t>(y * sourceWidth) * 4U;
        std::copy(sourceRow, sourceRow + static_cast<size_t>(sourceWidth) * 4U, targetRow);
    }
    return output;
}

std::vector<unsigned char> resampleNearest(const unsigned char* sourcePixels,
                                           const int sourceWidth,
                                           const int sourceHeight,
                                           const int sourceRowStridePixels,
                                           const int targetWidth,
                                           const int targetHeight) {
    std::vector<unsigned char> output(static_cast<size_t>(targetWidth) *
                                      static_cast<size_t>(targetHeight) * 4U);
    for (int y = 0; y < targetHeight; ++y) {
        const int sourceY = std::min((y * sourceHeight) / targetHeight, sourceHeight - 1);
        for (int x = 0; x < targetWidth; ++x) {
            const int sourceX = std::min((x * sourceWidth) / targetWidth, sourceWidth - 1);
            const size_t sourceIndex =
                (static_cast<size_t>(sourceY * sourceRowStridePixels + sourceX)) * 4U;
            const size_t targetIndex = (static_cast<size_t>(y * targetWidth + x)) * 4U;
            output[targetIndex + 0U] = sourcePixels[sourceIndex + 0U];
            output[targetIndex + 1U] = sourcePixels[sourceIndex + 1U];
            output[targetIndex + 2U] = sourcePixels[sourceIndex + 2U];
            output[targetIndex + 3U] = sourcePixels[sourceIndex + 3U];
        }
    }
    return output;
}

unsigned char blendChannel(const unsigned char c00,
                           const unsigned char c10,
                           const unsigned char c01,
                           const unsigned char c11,
                           const float tx,
                           const float ty) {
    const float top = static_cast<float>(c00) + (static_cast<float>(c10) - static_cast<float>(c00)) * tx;
    const float bottom = static_cast<float>(c01) + (static_cast<float>(c11) - static_cast<float>(c01)) * tx;
    const float value = top + (bottom - top) * ty;
    return static_cast<unsigned char>(std::round(std::clamp(value, 0.0f, 255.0f)));
}

std::vector<unsigned char> resampleLinear(const unsigned char* sourcePixels,
                                          const int sourceWidth,
                                          const int sourceHeight,
                                          const int sourceRowStridePixels,
                                          const int targetWidth,
                                          const int targetHeight) {
    std::vector<unsigned char> output(static_cast<size_t>(targetWidth) *
                                      static_cast<size_t>(targetHeight) * 4U);
    const float scaleX = static_cast<float>(sourceWidth) / static_cast<float>(targetWidth);
    const float scaleY = static_cast<float>(sourceHeight) / static_cast<float>(targetHeight);
    for (int y = 0; y < targetHeight; ++y) {
        const float sourceY = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(sourceY)), 0, sourceHeight - 1);
        const int y1 = std::min(y0 + 1, sourceHeight - 1);
        const float ty = std::clamp(sourceY - static_cast<float>(y0), 0.0f, 1.0f);
        for (int x = 0; x < targetWidth; ++x) {
            const float sourceX = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(sourceX)), 0, sourceWidth - 1);
            const int x1 = std::min(x0 + 1, sourceWidth - 1);
            const float tx = std::clamp(sourceX - static_cast<float>(x0), 0.0f, 1.0f);

            const size_t i00 = (static_cast<size_t>(y0 * sourceRowStridePixels + x0)) * 4U;
            const size_t i10 = (static_cast<size_t>(y0 * sourceRowStridePixels + x1)) * 4U;
            const size_t i01 = (static_cast<size_t>(y1 * sourceRowStridePixels + x0)) * 4U;
            const size_t i11 = (static_cast<size_t>(y1 * sourceRowStridePixels + x1)) * 4U;
            const size_t targetIndex = (static_cast<size_t>(y * targetWidth + x)) * 4U;

            for (size_t channel = 0U; channel < 4U; ++channel) {
                output[targetIndex + channel] = blendChannel(sourcePixels[i00 + channel],
                                                             sourcePixels[i10 + channel],
                                                             sourcePixels[i01 + channel],
                                                             sourcePixels[i11 + channel],
                                                             tx,
                                                             ty);
            }
        }
    }
    return output;
}

} // namespace

namespace resource {

TextureResampleFilter selectTextureTileResampleFilter(const int sourceWidth,
                                                      const int sourceHeight,
                                                      const int targetSize) {
    if (sourceWidth <= 0 || sourceHeight <= 0 || targetSize <= 0) {
        failTextureResampler("Texture resampler filter selection dimensions are invalid");
    }
    if (sourceWidth > targetSize || sourceHeight > targetSize) {
        return TextureResampleFilter::Linear;
    }
    return TextureResampleFilter::Nearest;
}

std::vector<unsigned char> resampleRgba8(const unsigned char* sourcePixels,
                                         const int sourceWidth,
                                         const int sourceHeight,
                                         const int sourceRowStridePixels,
                                         const int targetWidth,
                                         const int targetHeight,
                                         const TextureResampleFilter filter) {
    validateResampleInput(sourcePixels,
                          sourceWidth,
                          sourceHeight,
                          sourceRowStridePixels,
                          targetWidth,
                          targetHeight);
    if (sourceWidth == targetWidth && sourceHeight == targetHeight &&
        sourceRowStridePixels == sourceWidth) {
        return copyRgba8(sourcePixels, sourceWidth, sourceHeight, sourceRowStridePixels);
    }
    if (filter == TextureResampleFilter::Nearest) {
        return resampleNearest(sourcePixels,
                               sourceWidth,
                               sourceHeight,
                               sourceRowStridePixels,
                               targetWidth,
                               targetHeight);
    }
    return resampleLinear(sourcePixels,
                          sourceWidth,
                          sourceHeight,
                          sourceRowStridePixels,
                          targetWidth,
                          targetHeight);
}

} // namespace resource
