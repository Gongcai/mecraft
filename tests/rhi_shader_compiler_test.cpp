#include "renderer/rhi/RhiShaderCompiler.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"

#include <iostream>
#include <string>

namespace {
[[nodiscard]] bool compileForBackend(const std::string& source,
                                     const renderer::rhi::RhiShaderBackend backend,
                                     const char* backendName) {
    RhiShaderDesc desc;
    desc.debugName = "RhiScreenCoordinates.Test";
    desc.stage = RhiShaderStage::Fragment;
    desc.source = source.c_str();
    desc.sourceSize = source.size();

    std::string errorMessage;
    const auto compiled = renderer::rhi::compileShaderToSpirv(desc, backend, errorMessage);
    if (!compiled.has_value()) {
        std::cerr << backendName << " shader compilation failed: " << errorMessage << '\n';
        return false;
    }
    if (compiled->spirv.empty()) {
        std::cerr << backendName << " shader compilation produced empty SPIR-V\n";
        return false;
    }
    return true;
}
} // namespace

int main() {
    const auto source = renderer::rhi::loadShaderSource(
        "tests/shaders/rhi_screen_coordinates_test.frag");
    if (!source.has_value()) {
        std::cerr << "RHI screen-coordinate test shader source failed to load\n";
        return 1;
    }
    if (!compileForBackend(*source, renderer::rhi::RhiShaderBackend::Vulkan, "Vulkan") ||
        !compileForBackend(*source, renderer::rhi::RhiShaderBackend::OpenGl, "OpenGL")) {
        return 1;
    }
    return 0;
}
