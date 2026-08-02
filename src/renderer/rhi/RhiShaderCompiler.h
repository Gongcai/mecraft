#ifndef MECRAFT_RHI_SHADER_COMPILER_H
#define MECRAFT_RHI_SHADER_COMPILER_H

#include "renderer/rhi/RhiDescriptor.h"
#include "renderer/rhi/RhiPipeline.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace renderer::rhi {

enum class RhiShaderBackend { Vulkan, OpenGl };

struct RhiShaderBindingInfo {
    uint32_t set = 0;
    uint32_t binding = 0;
    RhiBindingType type = RhiBindingType::UniformBuffer;
    uint32_t arrayCount = 1;
    bool runtimeArray = false;
    RhiShaderStageFlags stages = 0;
    std::string name;
};

struct RhiPushConstantInfo {
    uint32_t size = 0;
    RhiShaderStageFlags stages = 0;
    std::string name;
};

struct RhiShaderReflection {
    std::vector<RhiShaderBindingInfo> bindings;
    std::optional<RhiPushConstantInfo> pushConstant;
};

struct RhiCompiledShader {
    RhiShaderStage stage = RhiShaderStage::Vertex;
    std::string entryPoint = "main";
    std::vector<uint32_t> spirv;
    RhiShaderReflection reflection;
};

// Compiles canonical GLSL or validates provided SPIR-V and reflects its resource contract.
// The selected backend macro is injected only when compiling canonical source.
[[nodiscard]] std::optional<RhiCompiledShader> compileShaderToSpirv(const RhiShaderDesc& desc, RhiShaderBackend backend,
                                                                    std::string& errorMessage);

} // namespace renderer::rhi

#endif // MECRAFT_RHI_SHADER_COMPILER_H
