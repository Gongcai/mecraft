#ifndef MECRAFT_FSR31_VULKAN_RESOURCE_CONTRACT_H
#define MECRAFT_FSR31_VULKAN_RESOURCE_CONTRACT_H

#include "renderer/contracts/TemporalFrameContract.h"
#include "renderer/rhi/vulkan/VkRhiInterop.h"

#include <optional>

class VkRhiDevice;

/// Identifies one Vulkan resource consumed by the FSR 3.1 upscaler.
enum class Fsr31ResourceRole {
    HdrColor,
    Depth,
    Velocity,
    Exposure,
    ReactiveMask,
    TransparencyMask,
    OutputHdrColor
};

/// Describes why a native Vulkan resource cannot be registered with FSR 3.1.
enum class Fsr31ResourceValidationError {
    MissingNativeObject,
    InvalidImageType,
    InvalidViewType,
    InvalidFormat,
    InvalidExtent,
    InvalidAspectMask,
    InvalidSubresourceRange,
    MissingSampledUsage,
    MissingStorageUsage
};

struct Fsr31ResourceValidationFailure {
    Fsr31ResourceRole role = Fsr31ResourceRole::HdrColor;
    Fsr31ResourceValidationError error =
        Fsr31ResourceValidationError::MissingNativeObject;
};

/// Native Vulkan resources registered for one FSR 3.1 dispatch.
struct Fsr31VulkanResourceSet {
    VkRhiTextureInteropInfo hdrColor;
    VkRhiTextureInteropInfo depth;
    VkRhiTextureInteropInfo velocity;
    VkRhiTextureInteropInfo exposure;
    VkRhiTextureInteropInfo reactiveMask;
    VkRhiTextureInteropInfo transparencyMask;
    VkRhiTextureInteropInfo outputHdrColor;
};

enum class Fsr31ResourceResolveStatus {
    Success,
    InvalidTemporalFrame,
    MissingNativeResource,
    InvalidResourceContract
};

struct Fsr31ResourceResolveResult {
    Fsr31ResourceResolveStatus status =
        Fsr31ResourceResolveStatus::InvalidTemporalFrame;
    std::optional<TemporalFrameValidationError> temporalError;
    std::optional<Fsr31ResourceRole> missingRole;
    std::optional<Fsr31ResourceValidationFailure> validationFailure;
    Fsr31VulkanResourceSet resources;

    [[nodiscard]] bool succeeded() const {
        return status == Fsr31ResourceResolveStatus::Success;
    }
};

/// Validate native image metadata required by an FSR 3.1 Vulkan dispatch.
/// @param renderExtent Resolution shared by scene inputs except exposure.
/// @param outputExtent Resolution of the storage output image.
/// @param resources Native Vulkan resource metadata resolved from RHI handles.
/// @return Empty on success, otherwise the first invalid resource and reason.
[[nodiscard]] std::optional<Fsr31ResourceValidationFailure>
validateFsr31VulkanResourceSet(TemporalExtent renderExtent,
                              TemporalExtent outputExtent,
                              const Fsr31VulkanResourceSet& resources);

/// Resolve and validate every temporal texture through the Vulkan interop boundary.
/// @param device Vulkan RHI device owning all supplied handles.
/// @param frame Complete backend-independent temporal frame contract.
/// @return Explicit resolution status plus native resources on success.
[[nodiscard]] Fsr31ResourceResolveResult resolveFsr31VulkanResourceSet(
    const VkRhiDevice& device,
    const TemporalFrameInput& frame);

#endif // MECRAFT_FSR31_VULKAN_RESOURCE_CONTRACT_H
