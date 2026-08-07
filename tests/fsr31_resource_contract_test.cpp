#include "renderer/upscaling/Fsr31VulkanResourceContract.h"

#include <cstdint>
#include <iostream>
#include <type_traits>

namespace {

bool requireTrue(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

template <typename Handle> Handle fakeHandle(const uintptr_t value) {
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(value);
    } else {
        return static_cast<Handle>(value);
    }
}

VkRhiTextureInteropInfo sampledTexture(const VkFormat format, const TemporalExtent extent, const uintptr_t identity) {
    VkRhiTextureInteropInfo resource;
    resource.image = fakeHandle<VkImage>(identity * 2u);
    resource.view = fakeHandle<VkImageView>(identity * 2u + 1u);
    resource.format = format;
    resource.extent = {extent.width, extent.height, 1u};
    resource.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    resource.imageType = VK_IMAGE_TYPE_2D;
    resource.viewType = VK_IMAGE_VIEW_TYPE_2D;
    resource.mipLevels = 1u;
    resource.arrayLayers = 1u;
    resource.baseMip = 0u;
    resource.mipCount = 1u;
    resource.baseLayer = 0u;
    resource.layerCount = 1u;
    resource.aspectMask = format == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    return resource;
}

Fsr31VulkanResourceSet completeResources() {
    constexpr TemporalExtent kRenderExtent{1280u, 720u};
    constexpr TemporalExtent kOutputExtent{1920u, 1080u};
    Fsr31VulkanResourceSet resources;
    resources.hdrColor = sampledTexture(VK_FORMAT_R16G16B16A16_SFLOAT, kRenderExtent, 1u);
    resources.depth = sampledTexture(VK_FORMAT_D32_SFLOAT, kRenderExtent, 2u);
    resources.velocity = sampledTexture(VK_FORMAT_R16G16_SFLOAT, kRenderExtent, 3u);
    resources.exposure = sampledTexture(VK_FORMAT_R32_SFLOAT, {1u, 1u}, 4u);
    resources.reactiveMask = sampledTexture(VK_FORMAT_R8_UNORM, kRenderExtent, 5u);
    resources.transparencyMask = sampledTexture(VK_FORMAT_R8_UNORM, kRenderExtent, 6u);
    resources.outputHdrColor = sampledTexture(VK_FORMAT_R16G16B16A16_SFLOAT, kOutputExtent, 7u);
    resources.outputHdrColor.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    return resources;
}

bool testCompleteContract() {
    const auto failure = validateFsr31VulkanResourceSet({1280u, 720u}, {1920u, 1080u}, completeResources());
    return requireTrue(!failure.has_value(), "complete FSR 3.1 Vulkan resources must validate");
}

bool testFormatValidation() {
    Fsr31VulkanResourceSet resources = completeResources();
    resources.velocity.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    const auto failure = validateFsr31VulkanResourceSet({1280u, 720u}, {1920u, 1080u}, resources);
    return requireTrue(failure.has_value() && failure->role == Fsr31ResourceRole::Velocity &&
                           failure->error == Fsr31ResourceValidationError::InvalidFormat,
                       "velocity format mismatch must identify the velocity resource");
}

bool testExtentValidation() {
    Fsr31VulkanResourceSet resources = completeResources();
    resources.outputHdrColor.extent.width = 1280u;
    const auto failure = validateFsr31VulkanResourceSet({1280u, 720u}, {1920u, 1080u}, resources);
    return requireTrue(failure.has_value() && failure->role == Fsr31ResourceRole::OutputHdrColor &&
                           failure->error == Fsr31ResourceValidationError::InvalidExtent,
                       "FSR output must use the complete output extent");
}

bool testUsageValidation() {
    Fsr31VulkanResourceSet resources = completeResources();
    resources.outputHdrColor.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    const auto outputFailure = validateFsr31VulkanResourceSet({1280u, 720u}, {1920u, 1080u}, resources);
    if (!requireTrue(outputFailure.has_value() && outputFailure->role == Fsr31ResourceRole::OutputHdrColor &&
                         outputFailure->error == Fsr31ResourceValidationError::MissingStorageUsage,
                     "FSR output must expose Vulkan storage usage")) {
        return false;
    }

    resources = completeResources();
    resources.reactiveMask.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const auto inputFailure = validateFsr31VulkanResourceSet({1280u, 720u}, {1920u, 1080u}, resources);
    return requireTrue(inputFailure.has_value() && inputFailure->role == Fsr31ResourceRole::ReactiveMask &&
                           inputFailure->error == Fsr31ResourceValidationError::MissingSampledUsage,
                       "FSR inputs must expose Vulkan sampled usage");
}

bool testNativeObjectAndSubresourceValidation() {
    Fsr31VulkanResourceSet resources = completeResources();
    resources.depth.view = VK_NULL_HANDLE;
    const auto nativeFailure = validateFsr31VulkanResourceSet({1280u, 720u}, {1920u, 1080u}, resources);
    if (!requireTrue(nativeFailure.has_value() && nativeFailure->role == Fsr31ResourceRole::Depth &&
                         nativeFailure->error == Fsr31ResourceValidationError::MissingNativeObject,
                     "missing depth image view must be reported explicitly")) {
        return false;
    }

    resources = completeResources();
    resources.exposure.baseMip = 1u;
    const auto rangeFailure = validateFsr31VulkanResourceSet({1280u, 720u}, {1920u, 1080u}, resources);
    return requireTrue(rangeFailure.has_value() && rangeFailure->role == Fsr31ResourceRole::Exposure &&
                           rangeFailure->error == Fsr31ResourceValidationError::InvalidSubresourceRange,
                       "FSR resources must use one complete base subresource");
}

bool testAspectValidation() {
    Fsr31VulkanResourceSet resources = completeResources();
    resources.depth.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    const auto failure = validateFsr31VulkanResourceSet({1280u, 720u}, {1920u, 1080u}, resources);
    return requireTrue(failure.has_value() && failure->role == Fsr31ResourceRole::Depth &&
                           failure->error == Fsr31ResourceValidationError::InvalidAspectMask,
                       "FSR depth input must expose the depth aspect");
}

} // namespace

int main() {
    if (!testCompleteContract())
        return 1;
    if (!testFormatValidation())
        return 1;
    if (!testExtentValidation())
        return 1;
    if (!testUsageValidation())
        return 1;
    if (!testNativeObjectAndSubresourceValidation())
        return 1;
    if (!testAspectValidation())
        return 1;
    return 0;
}
