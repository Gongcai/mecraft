#ifndef MECRAFT_TEMPORAL_UPSCALE_PASS_H
#define MECRAFT_TEMPORAL_UPSCALE_PASS_H

#include "renderer/contracts/TemporalFrameContract.h"
#include "renderer/core/RenderSettings.h"
#include "renderer/rhi/RhiRenderGraph.h"
#include "renderer/rhi/RhiTypes.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

class RhiDevice;
class RhiCommandListPool;
#if defined(MECRAFT_ENABLE_FSR31)
class Fsr31VulkanContext;
#endif
#if defined(MECRAFT_ENABLE_STREAMLINE)
class DlssVulkanContext;
#endif

enum class TemporalUpscaleStatus {
    Success,
    InvalidFrame,
    NativeExtentMismatch,
    Fsr31Unavailable,
    Fsr31InvalidResources,
    Fsr31CommandError,
    Fsr31DispatchError,
    Fsr31SubmitError,
    DlssUnavailable,
    DlssInvalidResources,
    DlssCommandError,
    DlssDispatchError,
    DlssSubmitError
};

struct TemporalUpscaleResult {
    TemporalUpscaleStatus status = TemporalUpscaleStatus::InvalidFrame;
    std::optional<TemporalFrameValidationError> validationError;
    RhiTextureHandle outputHdrColor;
    RhiTextureViewHandle outputHdrColorView;
    TemporalExtent outputExtent;
    int32_t sdkError = 0;

    [[nodiscard]] bool succeeded() const { return status == TemporalUpscaleStatus::Success; }
};

/// Dispatches the selected HDR temporal reconstruction implementation.
class TemporalUpscalePass {
public:
    TemporalUpscalePass();
    ~TemporalUpscalePass();
    TemporalUpscalePass(const TemporalUpscalePass&) = delete;
    TemporalUpscalePass& operator=(const TemporalUpscalePass&) = delete;

    void init(RhiDevice& device, RhiCommandListPool& commandListPool);
    void shutdown();

    /// Create the output-resolution HDR storage target required by SDK upscalers.
    /// @param settings Selected implementation and immutable SDK context flags.
    /// @param renderExtent Maximum scene-rendering extent for the SDK context.
    /// @param outputExtent Required display-resolution output extent.
    /// @return True when Native needs no target or the SDK target is ready.
    [[nodiscard]] bool prepareOutputTarget(const UpscaleSettings& settings, TemporalExtent renderExtent,
                                           TemporalExtent outputExtent);

    [[nodiscard]] RhiTextureHandle outputTextureHandle() const { return m_outputTexture; }
    [[nodiscard]] RhiTextureViewHandle outputTextureViewHandle() const { return m_outputView; }

    /// Execute one temporal reconstruction frame.
    /// @param settings Selected implementation and per-dispatch SDK settings.
    /// @param frame Fully populated backend-independent frame contract.
    /// @return Status and HDR output consumed by display post-processing.
    [[nodiscard]] TemporalUpscaleResult execute(const UpscaleSettings& settings, const TemporalFrameInput& frame);

    [[nodiscard]] static const char* statusText(TemporalUpscaleStatus status);

private:
    void destroyOutputTarget();
#if defined(MECRAFT_ENABLE_FSR31)
    [[nodiscard]] bool releaseFsr31Context();
    /// Create the R32F exposure target and graphics state used by FSR.
    /// @return True when the normalization target and pipeline are valid.
    [[nodiscard]] bool ensureFsrExposureResources();
    /// Cache a bind group for one post-process exposure-state view.
    /// @param sourceView RGBA16F scene exposure state sampled by normalization.
    /// @return True when a matching bind group is ready.
    [[nodiscard]] bool ensureFsrExposureBindGroup(RhiTextureViewHandle sourceView);
    /// Release every resource owned by the FSR exposure normalization stage.
    void destroyFsrExposureResources();
#endif
#if defined(MECRAFT_ENABLE_STREAMLINE)
    [[nodiscard]] bool releaseDlssContext();
#endif

    RhiDevice* m_device = nullptr;
    RhiCommandListPool* m_commandListPool = nullptr;
    RhiTextureHandle m_outputTexture;
    RhiTextureViewHandle m_outputView;
    TemporalExtent m_outputExtent;
    bool m_outputInitialized = false;
    RenderGraph m_renderGraph;
#if defined(MECRAFT_ENABLE_FSR31)
    std::unique_ptr<Fsr31VulkanContext> m_fsr31Context;
    bool m_fsr31DynamicResolution = false;
    bool m_fsr31DebugChecking = false;
    RhiTextureHandle m_fsrExposureTexture;
    RhiTextureViewHandle m_fsrExposureView;
    RhiSamplerHandle m_fsrExposureSampler;
    RhiBindGroupLayoutHandle m_fsrExposureBindGroupLayout;
    RhiPipelineLayoutHandle m_fsrExposurePipelineLayout;
    RhiShaderHandle m_fsrExposureVertexShader;
    RhiShaderHandle m_fsrExposureFragmentShader;
    RhiPipelineHandle m_fsrExposurePipeline;
    std::array<RhiBindGroupHandle, 2u> m_fsrExposureBindGroups{};
    std::array<RhiTextureViewHandle, 2u> m_fsrExposureSourceViews{};
    bool m_fsrExposureInitialized = false;
#endif
#if defined(MECRAFT_ENABLE_STREAMLINE)
    std::unique_ptr<DlssVulkanContext> m_dlssContext;
#endif
};

#endif // MECRAFT_TEMPORAL_UPSCALE_PASS_H
