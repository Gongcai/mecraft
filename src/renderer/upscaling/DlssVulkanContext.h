#ifndef MECRAFT_DLSS_VULKAN_CONTEXT_H
#define MECRAFT_DLSS_VULKAN_CONTEXT_H

#include "renderer/contracts/TemporalFrameContract.h"
#include "renderer/core/RenderSettings.h"

#include <cstdint>

class RhiCommandList;
class VkRhiDevice;

enum class DlssVulkanStatus {
    Success,
    RuntimeUnavailable,
    InvalidQuality,
    InvalidExtent,
    InvalidResources,
    MissingCommandBuffer,
    SdkError
};

struct DlssRenderExtentResult {
    DlssVulkanStatus status = DlssVulkanStatus::InvalidExtent;
    TemporalExtent extent;

    [[nodiscard]] bool succeeded() const { return status == DlssVulkanStatus::Success; }
};

struct DlssJitterResult {
    DlssVulkanStatus status = DlssVulkanStatus::InvalidExtent;
    TemporalJitter jitter;
    uint32_t phaseCount = 0u;
    uint32_t phaseIndex = 0u;

    [[nodiscard]] bool succeeded() const { return status == DlssVulkanStatus::Success; }
};

struct DlssVulkanDispatchResult {
    DlssVulkanStatus status = DlssVulkanStatus::SdkError;

    [[nodiscard]] bool succeeded() const { return status == DlssVulkanStatus::Success; }
};

/// Query the render extent selected by the active DLSS model.
/// @param quality Project quality mode mapped to the Streamline DLSS mode.
/// @param outputExtent Display-resolution target requested from DLSS.
/// @return Explicit status and the model-selected render extent.
[[nodiscard]] DlssRenderExtentResult queryDlssRenderExtent(TemporalUpscaleQuality quality, TemporalExtent outputExtent);

/// Generate one deterministic DLSS temporal jitter sample.
/// @param frameIndex Monotonically increasing real-frame index.
/// @param renderExtent Scene rendering extent.
/// @param outputExtent DLSS output extent.
/// @return Pixel/projection jitter and sequence metadata.
[[nodiscard]] DlssJitterResult queryDlssJitter(uint64_t frameIndex, TemporalExtent renderExtent,
                                               TemporalExtent outputExtent);

/// Owns the Streamline DLSS viewport configuration and Vulkan resource bridge.
class DlssVulkanContext final {
public:
    DlssVulkanContext() = default;
    ~DlssVulkanContext();
    DlssVulkanContext(const DlssVulkanContext&) = delete;
    DlssVulkanContext& operator=(const DlssVulkanContext&) = delete;

    /// Configure the fixed Streamline viewport used by the renderer.
    /// @param quality DLSS quality mode.
    /// @param outputExtent Display-resolution output extent.
    /// @return True when Streamline accepted the DLSS options.
    bool initialize(TemporalUpscaleQuality quality, TemporalExtent renderExtent, TemporalExtent outputExtent);

    /// Release Streamline resources associated with the DLSS viewport.
    /// @return True when no DLSS viewport resources remain owned by Streamline.
    bool shutdown();

    /// Evaluate one DLSS frame on an active Vulkan command buffer.
    /// @param device Vulkan RHI device used to resolve native resources.
    /// @param commandList Recording graphics command list used for evaluation.
    /// @param frame Complete backend-independent temporal frame contract.
    /// @return Explicit resource, command-buffer, or SDK status.
    [[nodiscard]] DlssVulkanDispatchResult dispatch(const VkRhiDevice& device, const RhiCommandList& commandList,
                                                    const TemporalFrameInput& frame);

    [[nodiscard]] bool isInitialized() const { return m_initialized; }

    [[nodiscard]] TemporalExtent outputExtent() const { return m_outputExtent; }

    [[nodiscard]] TemporalExtent renderExtent() const { return m_renderExtent; }

    [[nodiscard]] TemporalUpscaleQuality quality() const { return m_quality; }

private:
    static constexpr uint32_t kViewport = 0u;

    TemporalUpscaleQuality m_quality = TemporalUpscaleQuality::Native;
    TemporalExtent m_renderExtent;
    TemporalExtent m_outputExtent;
    bool m_initialized = false;
};

#endif // MECRAFT_DLSS_VULKAN_CONTEXT_H
