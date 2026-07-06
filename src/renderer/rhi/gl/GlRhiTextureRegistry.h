#ifndef MECRAFT_GL_RHI_TEXTURE_REGISTRY_H
#define MECRAFT_GL_RHI_TEXTURE_REGISTRY_H

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

#include <cstdint>

namespace renderer::rhi::gl {

struct GlRhiTextureRegistration {
    uint32_t textureId = 0;
    RhiTextureDimension dimension = RhiTextureDimension::Texture2D;
    RhiTextureFormat format = RhiTextureFormat::Undefined;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depthOrLayers = 1;
    uint32_t mipLevels = 1;
    uint32_t sampleCount = 1;
    RhiTextureUsageFlags usage = 0;
    bool textureView = false;
};

[[nodiscard]] RhiTextureHandle registerTexture(const GlRhiTextureRegistration& registration);
void unregisterTexture(RhiTextureHandle handle);
void unregisterTextureAndReset(RhiTextureHandle& handle);

[[nodiscard]] bool isTextureRegistered(RhiTextureHandle handle);
[[nodiscard]] uint32_t textureId(RhiTextureHandle handle);
[[nodiscard]] bool textureRegistration(RhiTextureHandle handle,
                                       GlRhiTextureRegistration& outRegistration);

} // namespace renderer::rhi::gl

#endif // MECRAFT_GL_RHI_TEXTURE_REGISTRY_H
