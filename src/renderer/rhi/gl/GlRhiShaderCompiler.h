#ifndef MECRAFT_GL_RHI_SHADER_COMPILER_H
#define MECRAFT_GL_RHI_SHADER_COMPILER_H

#include "renderer/rhi/RhiShaderCompiler.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace renderer::rhi::gl {

struct GlRhiShaderBindingRemap {
    uint32_t set = 0;
    uint32_t binding = 0;
    RhiBindingType type = RhiBindingType::UniformBuffer;
    uint32_t physicalBinding = 0;
};

// Cross-compiles a reflected SPIR-V stage to OpenGL 4.5 GLSL after applying pipeline-specific
// descriptor and push-constant physical binding assignments.
[[nodiscard]] std::optional<std::string> crossCompileShaderToOpenGl(
    const RhiCompiledShader& shader,
    const std::vector<GlRhiShaderBindingRemap>& remaps,
    const std::optional<uint32_t>& pushConstantBinding,
    std::string& errorMessage);

} // namespace renderer::rhi::gl

#endif // MECRAFT_GL_RHI_SHADER_COMPILER_H
