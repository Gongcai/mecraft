#include "renderer/rhi/RhiDeviceFactory.h"

#include "renderer/rhi/RhiDevice.h"

#ifdef MECRAFT_RHI_BACKEND_OPENGL
#include "renderer/rhi/gl/GlRhiDevice.h"
#endif

#include <cstring>

namespace renderer::rhi {

std::unique_ptr<RhiDevice> createRhiDevice(const RhiBackend backend) {
    switch (backend) {
        case RhiBackend::OpenGL:
#ifdef MECRAFT_RHI_BACKEND_OPENGL
            return std::make_unique<GlRhiDevice>();
#else
            return nullptr;
#endif
        case RhiBackend::Vulkan:
            return nullptr;
    }
    return nullptr;
}

std::unique_ptr<RhiDevice> createDefaultRhiDevice() {
    if (std::strcmp(MECRAFT_DEFAULT_RHI_BACKEND, "OpenGL") == 0) {
        return createRhiDevice(RhiBackend::OpenGL);
    }
    if (std::strcmp(MECRAFT_DEFAULT_RHI_BACKEND, "Vulkan") == 0) {
        return createRhiDevice(RhiBackend::Vulkan);
    }
    return nullptr;
}

} // namespace renderer::rhi
