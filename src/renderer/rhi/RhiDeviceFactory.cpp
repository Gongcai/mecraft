#include "renderer/rhi/RhiDeviceFactory.h"

#include "renderer/rhi/RhiDevice.h"

#include <cstdlib>

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

const char* rhiBackendDisplayName(const RhiBackend backend) {
    switch (backend) {
    case RhiBackend::OpenGL: return "OpenGL";
    case RhiBackend::Vulkan: return "Vulkan";
    }
    std::abort();
}

const char* rhiBackendConfigName(const RhiBackend backend) {
    switch (backend) {
    case RhiBackend::OpenGL: return "opengl";
    case RhiBackend::Vulkan: return "vulkan";
    }
    std::abort();
}

std::optional<RhiBackend> parseRhiBackend(const std::string_view name) {
    if (name == "opengl") {
        return RhiBackend::OpenGL;
    }
    if (name == "vulkan") {
        return RhiBackend::Vulkan;
    }
    return std::nullopt;
}

bool isRhiBackendAvailable(const RhiBackend backend) {
    switch (backend) {
    case RhiBackend::OpenGL:
#ifdef MECRAFT_RHI_BACKEND_OPENGL
        return true;
#else
        return false;
#endif
    case RhiBackend::Vulkan:
#ifdef MECRAFT_RHI_BACKEND_VULKAN
        return true;
#else
        return false;
#endif
    }
    return false;
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
