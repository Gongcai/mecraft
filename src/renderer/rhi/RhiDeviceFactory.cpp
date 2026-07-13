#include "renderer/rhi/RhiDeviceFactory.h"

#include "renderer/rhi/RhiDevice.h"

#ifdef MECRAFT_RHI_BACKEND_OPENGL
#include "renderer/rhi/gl/GlRhiDevice.h"
#endif
#ifdef MECRAFT_RHI_BACKEND_VULKAN
#include "renderer/rhi/vulkan/VkRhiDevice.h"
#endif

namespace renderer::rhi {

RhiBackend defaultRhiBackend() {
#if defined(MECRAFT_DEFAULT_RHI_OPENGL)
    return RhiBackend::OpenGL;
#elif defined(MECRAFT_DEFAULT_RHI_VULKAN)
    return RhiBackend::Vulkan;
#else
#error "A valid default RHI backend compile definition is required"
#endif
}

std::unique_ptr<RhiDevice> createRhiDevice(const RhiBackend backend) {
    switch (backend) {
        case RhiBackend::OpenGL:
#ifdef MECRAFT_RHI_BACKEND_OPENGL
            return std::make_unique<GlRhiDevice>();
#else
            return nullptr;
#endif
        case RhiBackend::Vulkan:
#ifdef MECRAFT_RHI_BACKEND_VULKAN
            return std::make_unique<VkRhiDevice>();
#else
            return nullptr;
#endif
    }
    return nullptr;
}

std::unique_ptr<RhiDevice> createDefaultRhiDevice() {
    return createRhiDevice(defaultRhiBackend());
}

} // namespace renderer::rhi
