#include "renderer/upscaling/Fsr31VulkanResourceContract.h"

#include "renderer/rhi/vulkan/VkRhiDevice.h"

#include <array>

namespace {

struct ResourceRequirement {
    Fsr31ResourceRole role;
    const VkRhiTextureInteropInfo* resource;
    VkFormat format;
    TemporalExtent extent;
    VkImageAspectFlags aspectMask;
    bool storage;
};

[[nodiscard]] std::optional<Fsr31ResourceValidationFailure> validateResource(const ResourceRequirement& requirement) {
    const VkRhiTextureInteropInfo& resource = *requirement.resource;
    if (resource.image == VK_NULL_HANDLE || resource.view == VK_NULL_HANDLE) {
        return Fsr31ResourceValidationFailure{requirement.role, Fsr31ResourceValidationError::MissingNativeObject};
    }
    if (resource.imageType != VK_IMAGE_TYPE_2D) {
        return Fsr31ResourceValidationFailure{requirement.role, Fsr31ResourceValidationError::InvalidImageType};
    }
    if (resource.viewType != VK_IMAGE_VIEW_TYPE_2D) {
        return Fsr31ResourceValidationFailure{requirement.role, Fsr31ResourceValidationError::InvalidViewType};
    }
    if (resource.format != requirement.format) {
        return Fsr31ResourceValidationFailure{requirement.role, Fsr31ResourceValidationError::InvalidFormat};
    }
    if (resource.extent.width != requirement.extent.width || resource.extent.height != requirement.extent.height ||
        resource.extent.depth != 1u) {
        return Fsr31ResourceValidationFailure{requirement.role, Fsr31ResourceValidationError::InvalidExtent};
    }
    if (resource.aspectMask != requirement.aspectMask) {
        return Fsr31ResourceValidationFailure{requirement.role, Fsr31ResourceValidationError::InvalidAspectMask};
    }
    if (resource.baseMip != 0u || resource.mipCount != 1u || resource.baseLayer != 0u || resource.layerCount != 1u) {
        return Fsr31ResourceValidationFailure{requirement.role, Fsr31ResourceValidationError::InvalidSubresourceRange};
    }
    if ((resource.usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0u) {
        return Fsr31ResourceValidationFailure{requirement.role, Fsr31ResourceValidationError::MissingSampledUsage};
    }
    if (requirement.storage && (resource.usage & VK_IMAGE_USAGE_STORAGE_BIT) == 0u) {
        return Fsr31ResourceValidationFailure{requirement.role, Fsr31ResourceValidationError::MissingStorageUsage};
    }
    return std::nullopt;
}

} // namespace

std::optional<Fsr31ResourceValidationFailure> validateFsr31VulkanResourceSet(const TemporalExtent resourceExtent,
                                                                             const TemporalExtent outputExtent,
                                                                             const Fsr31VulkanResourceSet& resources) {
    const std::array<ResourceRequirement, 7u> requirements{
        {{Fsr31ResourceRole::HdrColor, &resources.hdrColor, VK_FORMAT_R16G16B16A16_SFLOAT, resourceExtent,
          VK_IMAGE_ASPECT_COLOR_BIT, false},
         {Fsr31ResourceRole::Depth, &resources.depth, VK_FORMAT_D32_SFLOAT, resourceExtent, VK_IMAGE_ASPECT_DEPTH_BIT,
          false},
         {Fsr31ResourceRole::Velocity, &resources.velocity, VK_FORMAT_R16G16_SFLOAT, resourceExtent,
          VK_IMAGE_ASPECT_COLOR_BIT, false},
         {Fsr31ResourceRole::Exposure,
          &resources.exposure,
          VK_FORMAT_R16G16B16A16_SFLOAT,
          {1u, 1u},
          VK_IMAGE_ASPECT_COLOR_BIT,
          false},
         {Fsr31ResourceRole::ReactiveMask, &resources.reactiveMask, VK_FORMAT_R8_UNORM, resourceExtent,
          VK_IMAGE_ASPECT_COLOR_BIT, false},
         {Fsr31ResourceRole::TransparencyMask, &resources.transparencyMask, VK_FORMAT_R8_UNORM, resourceExtent,
          VK_IMAGE_ASPECT_COLOR_BIT, false},
         {Fsr31ResourceRole::OutputHdrColor, &resources.outputHdrColor, VK_FORMAT_R16G16B16A16_SFLOAT, outputExtent,
          VK_IMAGE_ASPECT_COLOR_BIT, true}}};
    for (const ResourceRequirement& requirement : requirements) {
        const auto failure = validateResource(requirement);
        if (failure.has_value()) {
            return failure;
        }
    }
    return std::nullopt;
}

Fsr31ResourceResolveResult resolveFsr31VulkanResourceSet(const VkRhiDevice& device, const TemporalFrameInput& frame) {
    const auto temporalError = validateTemporalFrame(frame);
    if (temporalError.has_value()) {
        Fsr31ResourceResolveResult result;
        result.status = Fsr31ResourceResolveStatus::InvalidTemporalFrame;
        result.temporalError = temporalError;
        return result;
    }

    Fsr31VulkanResourceSet resources;
    struct ResourceLookup {
        Fsr31ResourceRole role;
        RhiTextureHandle texture;
        RhiTextureViewHandle view;
        VkRhiTextureInteropInfo* output;
    };
    const ResourceLookup lookups[] = {
        {Fsr31ResourceRole::HdrColor, frame.textures.hdrColor, frame.textures.hdrColorView, &resources.hdrColor},
        {Fsr31ResourceRole::Depth, frame.textures.depth, frame.textures.depthView, &resources.depth},
        {Fsr31ResourceRole::Velocity, frame.textures.velocity, frame.textures.velocityView, &resources.velocity},
        {Fsr31ResourceRole::Exposure, frame.textures.exposure, frame.textures.exposureView, &resources.exposure},
        {Fsr31ResourceRole::ReactiveMask, frame.textures.reactiveMask, frame.textures.reactiveMaskView,
         &resources.reactiveMask},
        {Fsr31ResourceRole::TransparencyMask, frame.textures.transparencyMask, frame.textures.transparencyMaskView,
         &resources.transparencyMask},
        {Fsr31ResourceRole::OutputHdrColor, frame.textures.outputHdrColor, frame.textures.outputHdrColorView,
         &resources.outputHdrColor}};
    for (const ResourceLookup& lookup : lookups) {
        const auto resource = VkRhiInterop::textureInfo(device, lookup.texture, lookup.view);
        if (!resource.has_value()) {
            Fsr31ResourceResolveResult result;
            result.status = Fsr31ResourceResolveStatus::MissingNativeResource;
            result.missingRole = lookup.role;
            return result;
        }
        *lookup.output = *resource;
    }

    const auto failure =
        validateFsr31VulkanResourceSet(frame.extents.resourceExtent, frame.extents.outputExtent, resources);
    if (failure.has_value()) {
        Fsr31ResourceResolveResult result;
        result.status = Fsr31ResourceResolveStatus::InvalidResourceContract;
        result.validationFailure = failure;
        return result;
    }
    Fsr31ResourceResolveResult result;
    result.status = Fsr31ResourceResolveStatus::Success;
    result.resources = resources;
    return result;
}
