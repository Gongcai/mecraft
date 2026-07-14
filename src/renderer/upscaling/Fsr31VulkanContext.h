#ifndef MECRAFT_FSR31_VULKAN_CONTEXT_H
#define MECRAFT_FSR31_VULKAN_CONTEXT_H

#include "renderer/contracts/TemporalFrameContract.h"

#include <cstddef>
#include <cstdint>
#include <memory>

class VkRhiDevice;

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
    ContextCreationError
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

/// Owns one FidelityFX FSR 3.1 upscaler context and its Vulkan backend memory.
class Fsr31VulkanContext final {
public:
    Fsr31VulkanContext();
    ~Fsr31VulkanContext();
    Fsr31VulkanContext(const Fsr31VulkanContext&) = delete;
    Fsr31VulkanContext& operator=(const Fsr31VulkanContext&) = delete;

    /// Create the official Vulkan backend and FSR upscaler context.
    /// @param device Initialized Vulkan RHI device that owns all future resources.
    /// @param desc Maximum extents and immutable context feature flags.
    /// @return Explicit lifecycle status plus the SDK error code when applicable.
    [[nodiscard]] Fsr31VulkanContextCreateResult initialize(
        const VkRhiDevice& device,
        const Fsr31VulkanContextDesc& desc);

    /// Destroy the FSR context before its owning Vulkan device is destroyed.
    /// @return Explicit lifecycle status plus the SDK error code when applicable.
    [[nodiscard]] Fsr31VulkanContextDestroyResult shutdown();

    [[nodiscard]] bool isInitialized() const;
    [[nodiscard]] TemporalExtent maxRenderExtent() const;
    [[nodiscard]] TemporalExtent maxOutputExtent() const;
    [[nodiscard]] size_t scratchMemorySize() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // MECRAFT_FSR31_VULKAN_CONTEXT_H
