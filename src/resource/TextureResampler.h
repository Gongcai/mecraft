#ifndef MECRAFT_TEXTURE_RESAMPLER_H
#define MECRAFT_TEXTURE_RESAMPLER_H

#include <vector>

namespace resource {

enum class TextureResampleFilter {
    Nearest,
    Linear,
};

[[nodiscard]] TextureResampleFilter selectTextureTileResampleFilter(int sourceWidth, int sourceHeight, int targetSize);

[[nodiscard]] std::vector<unsigned char> resampleRgba8(const unsigned char* sourcePixels, int sourceWidth,
                                                       int sourceHeight, int sourceRowStridePixels, int targetWidth,
                                                       int targetHeight, TextureResampleFilter filter);

} // namespace resource

#endif // MECRAFT_TEXTURE_RESAMPLER_H
