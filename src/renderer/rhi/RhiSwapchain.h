#ifndef MECRAFT_RHI_SWAPCHAIN_H
#define MECRAFT_RHI_SWAPCHAIN_H

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

struct RhiSwapchainDesc {
    const char* debugName = nullptr;
    void* nativeWindow = nullptr;
    int width = 1;
    int height = 1;
    RhiTextureFormat colorFormat = RhiTextureFormat::Rgba8Unorm;
};

class RhiSwapchain {
public:
    virtual ~RhiSwapchain() = default;

    virtual bool create(const RhiSwapchainDesc& desc) = 0;
    virtual void destroy() = 0;
    virtual bool resize(int width, int height) = 0;
    [[nodiscard]] virtual RhiTextureHandle currentColorTexture() const = 0;
    [[nodiscard]] virtual RhiTextureFormat colorFormat() const = 0;
    [[nodiscard]] virtual int width() const = 0;
    [[nodiscard]] virtual int height() const = 0;
};

#endif // MECRAFT_RHI_SWAPCHAIN_H
