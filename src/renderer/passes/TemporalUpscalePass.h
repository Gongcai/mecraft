#ifndef MECRAFT_TEMPORAL_UPSCALE_PASS_H
#define MECRAFT_TEMPORAL_UPSCALE_PASS_H

#include "renderer/contracts/TemporalFrameContract.h"
#include "renderer/core/RenderSettings.h"

#include <optional>

class RhiDevice;

enum class TemporalUpscaleStatus {
    Success,
    InvalidFrame,
    NativeExtentMismatch,
    Fsr31Unavailable,
    DlssUnavailable
};

struct TemporalUpscaleResult {
    TemporalUpscaleStatus status = TemporalUpscaleStatus::InvalidFrame;
    std::optional<TemporalFrameValidationError> validationError;
    RhiTextureHandle outputHdrColor;
    RhiTextureViewHandle outputHdrColorView;
    TemporalExtent outputExtent;

    [[nodiscard]] bool succeeded() const {
        return status == TemporalUpscaleStatus::Success;
    }
};

/// Dispatches the selected HDR temporal reconstruction implementation.
class TemporalUpscalePass {
public:
    TemporalUpscalePass() = default;
    ~TemporalUpscalePass();
    TemporalUpscalePass(const TemporalUpscalePass&) = delete;
    TemporalUpscalePass& operator=(const TemporalUpscalePass&) = delete;

    void init(RhiDevice& device);
    void shutdown();

    /// Create the output-resolution HDR storage target required by SDK upscalers.
    /// @param type Selected temporal reconstruction implementation.
    /// @param outputExtent Required display-resolution output extent.
    /// @return True when Native needs no target or the SDK target is ready.
    [[nodiscard]] bool prepareOutputTarget(TemporalUpscalerType type,
                                           TemporalExtent outputExtent);

    [[nodiscard]] RhiTextureHandle outputTextureHandle() const {
        return m_outputTexture;
    }
    [[nodiscard]] RhiTextureViewHandle outputTextureViewHandle() const {
        return m_outputView;
    }

    /// Execute one temporal reconstruction frame.
    /// @param type Explicitly selected temporal reconstruction technology.
    /// @param frame Fully populated backend-independent frame contract.
    /// @return Status and HDR output consumed by display post-processing.
    [[nodiscard]] TemporalUpscaleResult execute(
        TemporalUpscalerType type,
        const TemporalFrameInput& frame) const;

    [[nodiscard]] static const char* statusText(TemporalUpscaleStatus status);

private:
    void destroyOutputTarget();

    RhiDevice* m_device = nullptr;
    RhiTextureHandle m_outputTexture;
    RhiTextureViewHandle m_outputView;
    TemporalExtent m_outputExtent;
};

#endif // MECRAFT_TEMPORAL_UPSCALE_PASS_H
