#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiDeviceFactory.h"

#include <iostream>
#include <string_view>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    const RhiBackend expectedDefault =
#if defined(MECRAFT_TEST_DEFAULT_RHI_OPENGL)
        RhiBackend::OpenGL;
#elif defined(MECRAFT_TEST_DEFAULT_RHI_VULKAN)
        RhiBackend::Vulkan;
#else
#error "A valid default RHI backend test definition is required"
#endif

    if (!requireTrue(renderer::rhi::defaultRhiBackend() == expectedDefault,
                     "default RHI backend must match the configured backend")) {
        return 1;
    }
    if (!requireTrue(renderer::rhi::createDefaultRhiDevice() != nullptr,
                     "default RHI device factory must create the configured backend")) {
        return 1;
    }
    if (!requireTrue(renderer::rhi::parseRhiBackend("opengl") == RhiBackend::OpenGL,
                     "OpenGL config name must parse to the OpenGL backend")) {
        return 1;
    }
    if (!requireTrue(renderer::rhi::parseRhiBackend("vulkan") == RhiBackend::Vulkan,
                     "Vulkan config name must parse to the Vulkan backend")) {
        return 1;
    }
    if (!requireTrue(!renderer::rhi::parseRhiBackend("invalid"), "invalid backend config names must be rejected")) {
        return 1;
    }
    if (!requireTrue(std::string_view(renderer::rhi::rhiBackendConfigName(RhiBackend::OpenGL)) == "opengl",
                     "OpenGL backend must serialize to its config name")) {
        return 1;
    }
    if (!requireTrue(std::string_view(renderer::rhi::rhiBackendConfigName(RhiBackend::Vulkan)) == "vulkan",
                     "Vulkan backend must serialize to its config name")) {
        return 1;
    }

#if defined(MECRAFT_TEST_RHI_OPENGL)
    if (!requireTrue(renderer::rhi::isRhiBackendAvailable(RhiBackend::OpenGL),
                     "OpenGL backend must report availability when enabled")) {
        return 1;
    }
    if (!requireTrue(renderer::rhi::createRhiDevice(RhiBackend::OpenGL) != nullptr,
                     "OpenGL RHI factory branch must create a device when enabled")) {
        return 1;
    }
#else
    if (!requireTrue(!renderer::rhi::isRhiBackendAvailable(RhiBackend::OpenGL),
                     "OpenGL backend must report unavailability when disabled")) {
        return 1;
    }
    if (!requireTrue(renderer::rhi::createRhiDevice(RhiBackend::OpenGL) == nullptr,
                     "OpenGL RHI factory branch must reject a disabled backend")) {
        return 1;
    }
#endif

#if defined(MECRAFT_TEST_RHI_VULKAN)
    if (!requireTrue(renderer::rhi::isRhiBackendAvailable(RhiBackend::Vulkan),
                     "Vulkan backend must report availability when enabled")) {
        return 1;
    }
    if (!requireTrue(renderer::rhi::createRhiDevice(RhiBackend::Vulkan) != nullptr,
                     "Vulkan RHI factory branch must create a device when enabled")) {
        return 1;
    }
#else
    if (!requireTrue(!renderer::rhi::isRhiBackendAvailable(RhiBackend::Vulkan),
                     "Vulkan backend must report unavailability when disabled")) {
        return 1;
    }
    if (!requireTrue(renderer::rhi::createRhiDevice(RhiBackend::Vulkan) == nullptr,
                     "Vulkan RHI factory branch must reject a disabled backend")) {
        return 1;
    }
#endif

    return 0;
}
