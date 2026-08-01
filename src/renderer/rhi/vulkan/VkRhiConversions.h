#ifndef MECRAFT_VK_RHI_CONVERSIONS_H
#define MECRAFT_VK_RHI_CONVERSIONS_H

#include "renderer/rhi/RhiDescriptor.h"
#include "renderer/rhi/RhiResources.h"

#include <vulkan/vulkan.h>

namespace renderer::rhi::vulkan {

struct VkResourceStateMapping {
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

[[nodiscard]] inline VkFormat toVkFormat(const RhiTextureFormat format) {
    switch (format) {
    case RhiTextureFormat::R8Unorm: return VK_FORMAT_R8_UNORM;
    case RhiTextureFormat::Rg8Unorm: return VK_FORMAT_R8G8_UNORM;
    case RhiTextureFormat::Rgba8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case RhiTextureFormat::Rgba8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case RhiTextureFormat::Bgra8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case RhiTextureFormat::Bgra8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case RhiTextureFormat::Rgb10A2Unorm: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case RhiTextureFormat::Rg16Float: return VK_FORMAT_R16G16_SFLOAT;
    case RhiTextureFormat::Rgba16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case RhiTextureFormat::Rgba32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RhiTextureFormat::R16Float: return VK_FORMAT_R16_SFLOAT;
    case RhiTextureFormat::R32Float: return VK_FORMAT_R32_SFLOAT;
    case RhiTextureFormat::R32Uint: return VK_FORMAT_R32_UINT;
    case RhiTextureFormat::Rg32Uint: return VK_FORMAT_R32G32_UINT;
    case RhiTextureFormat::Depth16: return VK_FORMAT_D16_UNORM;
    case RhiTextureFormat::Depth24: return VK_FORMAT_X8_D24_UNORM_PACK32;
    case RhiTextureFormat::Depth24Stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
    case RhiTextureFormat::Depth32Float: return VK_FORMAT_D32_SFLOAT;
    case RhiTextureFormat::Undefined: break;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] inline RhiTextureFormat fromVkFormat(const VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM: return RhiTextureFormat::R8Unorm;
    case VK_FORMAT_R8G8_UNORM: return RhiTextureFormat::Rg8Unorm;
    case VK_FORMAT_R8G8B8A8_UNORM: return RhiTextureFormat::Rgba8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB: return RhiTextureFormat::Rgba8Srgb;
    case VK_FORMAT_B8G8R8A8_UNORM: return RhiTextureFormat::Bgra8Unorm;
    case VK_FORMAT_B8G8R8A8_SRGB: return RhiTextureFormat::Bgra8Srgb;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return RhiTextureFormat::Rgb10A2Unorm;
    case VK_FORMAT_R16G16_SFLOAT: return RhiTextureFormat::Rg16Float;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return RhiTextureFormat::Rgba16Float;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return RhiTextureFormat::Rgba32Float;
    case VK_FORMAT_R16_SFLOAT: return RhiTextureFormat::R16Float;
    case VK_FORMAT_R32_SFLOAT: return RhiTextureFormat::R32Float;
    case VK_FORMAT_R32_UINT: return RhiTextureFormat::R32Uint;
    case VK_FORMAT_R32G32_UINT: return RhiTextureFormat::Rg32Uint;
    case VK_FORMAT_D16_UNORM: return RhiTextureFormat::Depth16;
    case VK_FORMAT_X8_D24_UNORM_PACK32: return RhiTextureFormat::Depth24;
    case VK_FORMAT_D24_UNORM_S8_UINT: return RhiTextureFormat::Depth24Stencil8;
    case VK_FORMAT_D32_SFLOAT: return RhiTextureFormat::Depth32Float;
    default: break;
    }
    return RhiTextureFormat::Undefined;
}

[[nodiscard]] inline VkImageAspectFlags defaultAspectForFormat(const RhiTextureFormat format) {
    switch (format) {
    case RhiTextureFormat::Depth16:
    case RhiTextureFormat::Depth24:
    case RhiTextureFormat::Depth32Float: return VK_IMAGE_ASPECT_DEPTH_BIT;
    case RhiTextureFormat::Depth24Stencil8: return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default: return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

[[nodiscard]] inline VkImageAspectFlags toVkImageAspectFlags(const RhiTextureAspectFlags aspects,
                                                             const RhiTextureFormat format) {
    if (aspects == 0u) {
        return defaultAspectForFormat(format);
    }
    VkImageAspectFlags result = 0u;
    if ((aspects & rhiFlag(RhiTextureAspect::Color)) != 0u)
        result |= VK_IMAGE_ASPECT_COLOR_BIT;
    if ((aspects & rhiFlag(RhiTextureAspect::Depth)) != 0u)
        result |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if ((aspects & rhiFlag(RhiTextureAspect::Stencil)) != 0u)
        result |= VK_IMAGE_ASPECT_STENCIL_BIT;
    return result;
}

[[nodiscard]] inline VkResourceStateMapping toVkResourceState(const RhiResourceState state) {
    switch (state) {
    case RhiResourceState::Undefined: return {};
    case RhiResourceState::Present:
        return {VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    case RhiResourceState::RenderTarget:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case RhiResourceState::DepthWrite:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    case RhiResourceState::DepthRead:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    case RhiResourceState::ShaderRead:
        return {VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case RhiResourceState::ShaderWrite:
        return {VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case RhiResourceState::TransferSrc:
        return {VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case RhiResourceState::TransferDst:
        return {VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case RhiResourceState::VertexBuffer:
        return {VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    case RhiResourceState::IndexBuffer:
        return {VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    case RhiResourceState::IndirectArgument:
        return {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED};
    case RhiResourceState::UniformBuffer:
        return {VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    case RhiResourceState::StorageBuffer:
        return {VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    case RhiResourceState::AccelerationStructureBuildInput:
        return {VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED};
    case RhiResourceState::AccelerationStructureBuildScratch:
        return {VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                VK_IMAGE_LAYOUT_UNDEFINED};
    case RhiResourceState::AccelerationStructureBuildWrite:
        return {VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                    VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR,
                VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                VK_IMAGE_LAYOUT_UNDEFINED};
    case RhiResourceState::AccelerationStructureRead:
        return {VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                    VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR,
                VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR, VK_IMAGE_LAYOUT_UNDEFINED};
    case RhiResourceState::HostRead:
        return {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    case RhiResourceState::HostWrite:
        return {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    }
    return {};
}

[[nodiscard]] inline VkShaderStageFlags toVkShaderStageFlags(const RhiShaderStageFlags stages) {
    VkShaderStageFlags result = 0u;
    if ((stages & rhiFlag(RhiShaderStage::Vertex)) != 0u)
        result |= VK_SHADER_STAGE_VERTEX_BIT;
    if ((stages & rhiFlag(RhiShaderStage::Fragment)) != 0u)
        result |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if ((stages & rhiFlag(RhiShaderStage::Compute)) != 0u)
        result |= VK_SHADER_STAGE_COMPUTE_BIT;
    return result;
}

[[nodiscard]] inline VkDescriptorType toVkDescriptorType(const RhiBindingType type) {
    switch (type) {
    case RhiBindingType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case RhiBindingType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case RhiBindingType::SampledTexture: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case RhiBindingType::StorageTexture: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case RhiBindingType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
    case RhiBindingType::CombinedTextureSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case RhiBindingType::AccelerationStructure: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

[[nodiscard]] inline VkAccelerationStructureTypeKHR
toVkAccelerationStructureType(const RhiAccelerationStructureType type) {
    switch (type) {
    case RhiAccelerationStructureType::BottomLevel: return VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    case RhiAccelerationStructureType::TopLevel: return VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    }
    return VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR;
}

[[nodiscard]] inline VkBuildAccelerationStructureFlagsKHR
toVkAccelerationStructureBuildFlags(const RhiAccelerationStructureBuildFlags flags) {
    VkBuildAccelerationStructureFlagsKHR result = 0u;
    if ((flags & rhiFlag(RhiAccelerationStructureBuildFlag::AllowUpdate)) != 0u)
        result |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    if ((flags & rhiFlag(RhiAccelerationStructureBuildFlag::AllowCompaction)) != 0u)
        result |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    if ((flags & rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace)) != 0u)
        result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if ((flags & rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastBuild)) != 0u)
        result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    return result;
}

[[nodiscard]] inline VkGeometryFlagsKHR
toVkAccelerationStructureGeometryFlags(const RhiAccelerationStructureGeometryFlags flags) {
    VkGeometryFlagsKHR result = 0u;
    if ((flags & rhiFlag(RhiAccelerationStructureGeometryFlag::Opaque)) != 0u)
        result |= VK_GEOMETRY_OPAQUE_BIT_KHR;
    if ((flags & rhiFlag(RhiAccelerationStructureGeometryFlag::NoDuplicateAnyHitInvocation)) != 0u)
        result |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
    return result;
}

[[nodiscard]] inline VkIndexType toVkAccelerationStructureIndexType(const RhiAccelerationStructureIndexFormat format) {
    switch (format) {
    case RhiAccelerationStructureIndexFormat::None: return VK_INDEX_TYPE_NONE_KHR;
    case RhiAccelerationStructureIndexFormat::Uint16: return VK_INDEX_TYPE_UINT16;
    case RhiAccelerationStructureIndexFormat::Uint32: return VK_INDEX_TYPE_UINT32;
    }
    return VK_INDEX_TYPE_MAX_ENUM;
}

[[nodiscard]] inline VkCopyAccelerationStructureModeKHR
toVkAccelerationStructureCopyMode(const RhiAccelerationStructureCopyMode mode) {
    switch (mode) {
    case RhiAccelerationStructureCopyMode::Clone: return VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
    case RhiAccelerationStructureCopyMode::Compact: return VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
    }
    return VK_COPY_ACCELERATION_STRUCTURE_MODE_MAX_ENUM_KHR;
}

[[nodiscard]] inline VkDescriptorBindingFlags toVkDescriptorBindingFlags(const RhiBindingFlags flags) {
    VkDescriptorBindingFlags result = 0u;
    if (rhiHasBindingFlag(flags, RhiBindingFlag::PartiallyBound))
        result |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    if (rhiHasBindingFlag(flags, RhiBindingFlag::UpdateAfterBind))
        result |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    if (rhiHasBindingFlag(flags, RhiBindingFlag::UpdateUnusedWhilePending))
        result |= VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    if (rhiHasBindingFlag(flags, RhiBindingFlag::VariableDescriptorCount))
        result |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    return result;
}

[[nodiscard]] inline VkCompareOp toVkCompareOp(const RhiCompareOp op) {
    switch (op) {
    case RhiCompareOp::Never: return VK_COMPARE_OP_NEVER;
    case RhiCompareOp::Less: return VK_COMPARE_OP_LESS;
    case RhiCompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case RhiCompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case RhiCompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case RhiCompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case RhiCompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case RhiCompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_NEVER;
}

[[nodiscard]] inline VkBlendFactor toVkBlendFactor(const RhiBlendFactor factor) {
    switch (factor) {
    case RhiBlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
    case RhiBlendFactor::One: return VK_BLEND_FACTOR_ONE;
    case RhiBlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
    case RhiBlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case RhiBlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
    case RhiBlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case RhiBlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
    case RhiBlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case RhiBlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
    case RhiBlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    }
    return VK_BLEND_FACTOR_ZERO;
}

[[nodiscard]] inline VkBlendOp toVkBlendOp(const RhiBlendOp op) {
    switch (op) {
    case RhiBlendOp::Add: return VK_BLEND_OP_ADD;
    case RhiBlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
    case RhiBlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case RhiBlendOp::Min: return VK_BLEND_OP_MIN;
    case RhiBlendOp::Max: return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

[[nodiscard]] inline VkAttachmentLoadOp toVkLoadOp(const RhiLoadOp op) {
    switch (op) {
    case RhiLoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
    case RhiLoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case RhiLoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

[[nodiscard]] inline VkAttachmentStoreOp toVkStoreOp(const RhiStoreOp op) {
    switch (op) {
    case RhiStoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
    case RhiStoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

[[nodiscard]] inline VkPrimitiveTopology toVkPrimitiveTopology(const RhiPrimitiveTopology topology) {
    switch (topology) {
    case RhiPrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case RhiPrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case RhiPrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case RhiPrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

[[nodiscard]] inline VkFormat toVkVertexFormat(const RhiVertexFormat format) {
    switch (format) {
    case RhiVertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
    case RhiVertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case RhiVertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case RhiVertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RhiVertexFormat::Uint: return VK_FORMAT_R32_UINT;
    case RhiVertexFormat::Uint2: return VK_FORMAT_R32G32_UINT;
    case RhiVertexFormat::Uint3: return VK_FORMAT_R32G32B32_UINT;
    case RhiVertexFormat::Uint4: return VK_FORMAT_R32G32B32A32_UINT;
    case RhiVertexFormat::Sint8: return VK_FORMAT_R8_SINT;
    case RhiVertexFormat::Unorm8: return VK_FORMAT_R8_UNORM;
    case RhiVertexFormat::Uint8: return VK_FORMAT_R8_UINT;
    case RhiVertexFormat::Uint16: return VK_FORMAT_R16_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

} // namespace renderer::rhi::vulkan

#endif // MECRAFT_VK_RHI_CONVERSIONS_H
