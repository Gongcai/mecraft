#ifndef MECRAFT_RHI_SHADER_COMPILER_H
#define MECRAFT_RHI_SHADER_COMPILER_H

#include "renderer/rhi/RhiDescriptor.h"
#include "renderer/rhi/RhiPipeline.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace renderer::rhi {

struct RhiShaderBindingInfo {
    uint32_t set = 0;
    uint32_t binding = 0;
    RhiBindingType type = RhiBindingType::UniformBuffer;
    uint32_t arrayCount = 1;
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

// Compiles one canonical Vulkan GLSL stage to SPIR-V and reflects its resource contract.
// Invalid or backend-specific source is rejected instead of being rewritten or compiled through another path.
[[nodiscard]] std::optional<RhiCompiledShader> compileShaderToSpirv(
    const RhiShaderDesc& desc,
    std::string& errorMessage);

} // namespace renderer::rhi

#endif // MECRAFT_RHI_SHADER_COMPILER_H
