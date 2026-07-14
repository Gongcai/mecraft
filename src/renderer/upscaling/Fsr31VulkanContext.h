#ifndef MECRAFT_FSR31_VULKAN_CONTEXT_H
#define MECRAFT_FSR31_VULKAN_CONTEXT_H

#include "renderer/contracts/TemporalFrameContract.h"
#include "renderer/upscaling/Fsr31VulkanResourceContract.h"

#include <cstddef>
#include <cstdint>
#include <memory>

class VkRhiDevice;
class RhiCommandList;

struct Fsr31VulkanContextDesc {
    TemporalExtent maxRenderExtent;
    TemporalExtent maxOutputExtent;
    bool dynamicResolution = false;
    bool debugChecking = false;
};

enum class Fsr31VulkanContextCreateStatus {
    Success,
    AlreadyInitialized,
    InvalidRenderExtent,
    InvalidOutputExtent,
    RenderExtentExceedsOutputExtent,
    MissingVulkanDevice,
    InvalidScratchMemorySize,
    ScratchMemoryAllocationError,
    BackendInterfaceError,
    ContextCreationError,
    SharedResourceDescriptionError,
    InvalidSharedResourceDescription,
    SharedResourceCreationError
};

struct Fsr31VulkanContextCreateResult {
    Fsr31VulkanContextCreateStatus status =
        Fsr31VulkanContextCreateStatus::MissingVulkanDevice;
    int32_t sdkError = 0;

    [[nodiscard]] bool succeeded() const {
        return status == Fsr31VulkanContextCreateStatus::Success;
    }
};

enum class Fsr31VulkanContextDestroyStatus {
    Success,
    NotInitialized,
    ContextDestroyError
};

struct Fsr31VulkanContextDestroyResult {
    Fsr31VulkanContextDestroyStatus status =
        Fsr31VulkanContextDestroyStatus::NotInitialized;
    int32_t sdkError = 0;

    [[nodiscard]] bool succeeded() const {
        return status == Fsr31VulkanContextDestroyStatus::Success;
    }
};

struct Fsr31VulkanDispatchDesc {
    bool enableSharpening = false;
    float sharpness = 0.0f;
    bool drawDebugView = false;
};

enum class Fsr31VulkanDispatchStatus {
    Success,
    NotInitialized,
    InvalidSettings,
    ContextExtentExceeded,
    MissingCommandBuffer,
    InvalidResources,
    SdkError
};

struct Fsr31VulkanDispatchResult {
    Fsr31VulkanDispatchStatus status =
        Fsr31VulkanDispatchStatus::NotInitialized;
    int32_t sdkError = 0;
    std::optional<TemporalFrameValidationError> temporalError;
    std::optional<Fsr31ResourceResolveStatus> resourceStatus;
    std::optional<Fsr31ResourceRole> resourceRole;
    std::optional<Fsr31ResourceValidationFailure> validationFailure;

    Fsr31VulkanDispatchResult() = default;
    explicit Fsr31VulkanDispatchResult(
        const Fsr31VulkanDispatchStatus resultStatus,
        const int32_t error = 0)
        : status(resultStatus), sdkError(error) {}

    [[nodiscard]] bool succeeded() const {
        return status == Fsr31VulkanDispatchStatus::Success;
    }
};

/// Owns one FidelityFX FSR 3.1 upscaler context and its Vulkan backend memory.
class Fsr31VulkanContext final {
public:
    Fsr31VulkanContext();
    ~Fsr31VulkanContext();
    Fsr31VulkanContext(const Fsr31VulkanContext&) = delete;
    Fsr31VulkanContext& operator=(const Fsr31VulkanContext&) = delete;

    /// Create the official Vulkan backend and FSR upscaler context.
    /// @param device Initialized Vulkan RHI device that owns the SDK shared resources.
    /// @param desc Maximum extents and immutable context feature flags.
    /// @return Explicit lifecycle status plus the SDK error code when applicable.
    [[nodiscard]] Fsr31VulkanContextCreateResult initialize(
        VkRhiDevice& device,
        const Fsr31VulkanContextDesc& desc);

    /// Destroy the FSR context before its owning Vulkan device is destroyed.
    /// @return Explicit lifecycle status plus the SDK error code when applicable.
    [[nodiscard]] Fsr31VulkanContextDestroyResult shutdown();

    /// Record one FSR 3.1 dispatch into an active Vulkan RHI command list.
    /// @param device Vulkan RHI device owning the context and frame resources.
    /// @param commandList Recording graphics or compute command list.
    /// @param frame Validated temporal inputs and output target for this frame.
    /// @param desc Immutable per-dispatch sharpening and debug settings.
    /// @return Explicit resource, command-buffer, or SDK dispatch status.
    [[nodiscard]] Fsr31VulkanDispatchResult dispatch(
        const VkRhiDevice& device,
        RhiCommandList& commandList,
        const TemporalFrameInput& frame,
        const Fsr31VulkanDispatchDesc& desc);

    [[nodiscard]] bool isInitialized() const;
    [[nodiscard]] TemporalExtent maxRenderExtent() const;
    [[nodiscard]] TemporalExtent maxOutputExtent() const;
    [[nodiscard]] size_t scratchMemorySize() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // MECRAFT_FSR31_VULKAN_CONTEXT_H
