#ifndef MECRAFT_PRESENTATION_CONTROLLER_H
#define MECRAFT_PRESENTATION_CONTROLLER_H

#include "renderer/rhi/RhiTypes.h"
#include "renderer/contracts/TemporalFrameContract.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class RhiDevice;
class RhiCommandList;
class Window;

enum class PresentationMode {
    Native,
    Fsr31FrameGeneration,
    DlssFrameGeneration
};

enum class PresentationResult {
    Ready,
    Presented,
    Skipped,
    Failed,
    ContractViolation
};

enum class PresentationFailure {
    None,
    ResizeRejected,
    AcquireRejected,
    PresentRejected,
    BackendFrameCountInvalid,
    VsyncRejected,
    FullscreenRejected,
    WindowStateUnavailable,
    FrameGenerationRejected,
    FrameGenerationInputsRejected,
    FrameCancellationRejected,
    FrameAlreadyOpen,
    FrameNotOpen,
    FrameIdentityMismatch
};

/// Describes one acquired real frame and its stable presentation identity.
struct PresentationFrame {
    PresentationResult result = PresentationResult::Failed;
    PresentationFailure failure = PresentationFailure::None;
    RhiFrameStatus rhiStatus = RhiFrameStatus::Error;
    RhiFrameAcquireResult acquired;
    uint64_t realFrameNumber = 0u;

    /// Reports whether rendering commands may target the acquired frame.
    /// @return True only when the controller owns a live acquired frame.
    [[nodiscard]] bool shouldRender() const {
        return result == PresentationResult::Ready;
    }

    /// Reports whether the application may continue its frame loop.
    /// @return True for a renderable frame or a non-fatal skipped frame.
    [[nodiscard]] bool shouldContinue() const {
        return result == PresentationResult::Ready ||
               result == PresentationResult::Skipped;
    }
};

/// Describes the result of closing one real frame through the active backend.
struct PresentationCompleteResult {
    PresentationResult result = PresentationResult::Failed;
    PresentationFailure failure = PresentationFailure::None;
    RhiFrameStatus rhiStatus = RhiFrameStatus::Error;

    /// Reports whether the application may continue its frame loop.
    /// @return True after a successful present or a non-fatal presentation skip.
    [[nodiscard]] bool shouldContinue() const {
        return result == PresentationResult::Presented ||
               result == PresentationResult::Skipped;
    }
};

/// Accumulates real-frame and displayed-frame counters for diagnostics.
struct PresentationStatistics {
    PresentationMode mode = PresentationMode::Native;
    uint64_t acquireAttempts = 0u;
    uint64_t realFramesAcquired = 0u;
    uint64_t realFramesPresented = 0u;
    uint64_t generatedFramesPresented = 0u;
    uint64_t displayedFrames = 0u;
    uint64_t skippedFrames = 0u;
    uint64_t failedOperations = 0u;
    uint64_t resizeOperations = 0u;
    uint64_t vsyncChanges = 0u;
    uint64_t fullscreenChanges = 0u;
    bool vsyncEnabled = false;
    bool fullscreenEnabled = false;
    RhiFrameStatus lastAcquireStatus = RhiFrameStatus::Error;
    RhiFrameStatus lastPresentStatus = RhiFrameStatus::Error;
    PresentationFailure lastFailure = PresentationFailure::None;
};

/// Reports how many display images one backend emitted for a real frame.
struct PresentationBackendPresentResult {
    RhiFrameStatus status = RhiFrameStatus::Error;
    uint32_t displayedFrameCount = 0u;
    uint32_t generatedFrameCount = 0u;
};

/// Identifies the independent output-resolution UI target for one real frame.
struct PresentationUiFrame {
    RhiTextureHandle colorTexture;
    RhiTextureViewHandle colorView;
    RhiTextureFormat colorFormat = RhiTextureFormat::Undefined;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t slotIndex = 0u;
    uint64_t resourceGeneration = 0u;
    uint64_t realFrameNumber = 0u;
    bool premultipliedAlpha = true;
};

/// Exposes the HUD-less scene and independent UI resources for presentation backends.
struct PresentationFrameResources {
    RhiTextureHandle hudlessColorTexture;
    RhiTextureViewHandle hudlessColorView;
    RhiTextureHandle uiColorTexture;
    RhiTextureViewHandle uiColorView;
    RhiTextureFormat colorFormat = RhiTextureFormat::Undefined;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint64_t realFrameNumber = 0u;
    bool uiPremultipliedAlpha = true;
};

/// Backend boundary used by native and SDK-controlled presentation paths.
class PresentationBackend {
public:
    virtual ~PresentationBackend() = default;

    /// Returns the presentation technology implemented by this backend.
    [[nodiscard]] virtual PresentationMode mode() const = 0;
    /// Resizes backend-owned presentation resources to the requested extent.
    virtual bool resize(uint32_t width, uint32_t height) = 0;
    /// Acquires one real frame for rendering.
    [[nodiscard]] virtual RhiFrameAcquireResult acquireFrame() = 0;
    /// Presents the matching acquired real frame.
    virtual PresentationBackendPresentResult presentFrame(
        const RhiPresentInfo& info) = 0;
    /// Cancels the matching acquired frame without displaying it.
    virtual bool cancelFrame(const RhiPresentInfo& info) = 0;
    /// Reports the currently active vertical synchronization state.
    [[nodiscard]] virtual bool vsyncEnabled() const = 0;
    /// Reports whether the backend can change vertical synchronization at runtime.
    [[nodiscard]] virtual bool supportsVsyncControl() const = 0;
    /// Applies a vertical synchronization state before frame acquisition.
    virtual bool setVsyncEnabled(bool enabled) = 0;
    /// Reports whether this backend can create DLSS frame-generation swapchains.
    [[nodiscard]] virtual bool supportsFrameGeneration() const = 0;
    /// Reports whether the user-selected frame-generation swapchain is active.
    [[nodiscard]] virtual bool frameGenerationEnabled() const = 0;
    /// Reports whether interpolation is enabled for the current gameplay state.
    [[nodiscard]] virtual bool frameGenerationActive() const = 0;
    /// Recreates the swapchain after changing the user-selected DLSS-G state.
    virtual bool setFrameGenerationEnabled(bool enabled) = 0;
    /// Enables or suspends interpolation without unloading retained resources.
    virtual bool setRenderingGameFrames(bool renderingGameFrames) = 0;
    /// Reports whether the current frame must preserve and tag DLSS-G inputs.
    [[nodiscard]] virtual bool requiresFrameGenerationInputs() const = 0;
    /// Tags complete output-resolution and temporal resources for the next present.
    virtual bool prepareFrameGeneration(
        const PresentationFrameResources& resources,
        const TemporalFrameInput& temporalFrame) = 0;
};

/// Creates the native RHI swapchain presentation backend.
/// @param rhiDevice Device whose swapchain lifecycle is controlled by the backend.
/// @return A backend bound to the supplied device.
[[nodiscard]] std::unique_ptr<PresentationBackend> createNativePresentationBackend(
    RhiDevice& rhiDevice);

/// Owns frame acquisition, presentation state, and presentation statistics.
class PresentationController {
public:
    /// Creates a controller around one explicit presentation backend.
    /// @param backend Backend whose lifetime must exceed the controller lifetime.
    explicit PresentationController(PresentationBackend& backend);
    ~PresentationController();

    PresentationController(const PresentationController&) = delete;
    PresentationController& operator=(const PresentationController&) = delete;

    /// Resizes presentation resources when required and acquires one real frame.
    /// @param width Requested framebuffer width in physical pixels.
    /// @param height Requested framebuffer height in physical pixels.
    /// @return A renderable frame, a non-fatal skip, or an explicit failure.
    [[nodiscard]] PresentationFrame beginFrame(int width, int height);

    /// Applies queued display-state changes and acquires using the live window extent.
    /// @param window Window bound through initWindowState().
    /// @return A renderable frame, a non-fatal skip, or an explicit failure.
    [[nodiscard]] PresentationFrame beginFrame(Window& window);

    /// Presents and closes the matching frame returned by beginFrame().
    /// @param frame Live frame identity returned by this controller.
    /// @return Presentation completion state and backend status.
    [[nodiscard]] PresentationCompleteResult presentFrame(
        const PresentationFrame& frame);

    /// Cancels and closes a matching acquired frame without displaying it.
    /// @param frame Live frame identity returned by beginFrame().
    /// @return True when interpolation suspension and backend frame release both succeed.
    [[nodiscard]] bool cancelFrame(const PresentationFrame& frame);

    /// Creates presentation-owned UI targets and the UI composition pipeline.
    /// @param rhiDevice Device used for target, pipeline, and submission resources.
    /// @return True when the independent UI presentation contract is ready.
    [[nodiscard]] bool initUiComposition(RhiDevice& rhiDevice);

    /// Binds the native window whose fullscreen state is controlled at frame boundaries.
    /// @param window Window used by the active presentation backend.
    /// @return True when the window binding is valid for this controller.
    [[nodiscard]] bool initWindowState(Window& window);

    /// Queues a vertical synchronization change for the next frame boundary.
    /// @param enabled Requested synchronization state.
    /// @return True when runtime VSync control is available and the request was accepted.
    [[nodiscard]] bool requestVsyncEnabled(bool enabled);

    /// Queues a fullscreen change for the next frame boundary.
    /// @param enabled Requested fullscreen state.
    /// @return True when a native window is bound and the request was accepted.
    [[nodiscard]] bool requestFullscreenEnabled(bool enabled);

    /// Queues a DLSS Frame Generation user-setting change for the next frame boundary.
    /// @param enabled True to create a DLSS-G swapchain, false to create a native one.
    /// @return True when the active backend supports the requested technology.
    [[nodiscard]] bool requestFrameGenerationEnabled(bool enabled);

    /// Queues gameplay interpolation activity for the next frame boundary.
    /// @param renderingGameFrames True while gameplay produces valid temporal frames.
    /// @return True when the request is valid for the active backend state.
    [[nodiscard]] bool requestRenderingGameFrames(bool renderingGameFrames);

    /// Returns the requested VSync state, including a queued frame-boundary change.
    [[nodiscard]] bool vsyncEnabled() const;

    /// Returns whether runtime VSync changes are supported by the active backend.
    [[nodiscard]] bool vsyncControlAvailable() const;

    /// Returns the requested fullscreen state, including a queued change.
    [[nodiscard]] bool fullscreenEnabled() const;

    /// Returns whether the bound native window supports runtime fullscreen changes.
    [[nodiscard]] bool fullscreenControlAvailable() const;

    /// Reports whether DLSS Frame Generation is supported by the active backend.
    [[nodiscard]] bool frameGenerationAvailable() const;

    /// Returns the requested DLSS Frame Generation user-setting state.
    [[nodiscard]] bool frameGenerationEnabled() const;

    /// Returns whether the backend has applied the DLSS-G swapchain state.
    [[nodiscard]] bool frameGenerationSwapchainEnabled() const;

    /// Returns whether interpolation is active for the current gameplay frame.
    [[nodiscard]] bool frameGenerationActive() const;

    /// Releases UI targets and composition resources after GPU work is complete.
    void shutdownUiComposition();

    /// Acquires one reusable output-resolution UI target for a real frame.
    /// @param frame Real frame currently owned by the controller.
    /// @return A UI target identity, or no value when the contract is invalid.
    [[nodiscard]] std::optional<PresentationUiFrame> acquireUiFrame(
        const PresentationFrame& frame);

    /// Resolves the HUD-less scene and UI resources for one real frame.
    /// @param frame Matching real frame returned by beginFrame().
    /// @param uiFrame Matching UI target returned by acquireUiFrame().
    /// @return Validated presentation resources, or no value on contract mismatch.
    [[nodiscard]] std::optional<PresentationFrameResources> frameResources(
        const PresentationFrame& frame,
        const PresentationUiFrame& uiFrame) const;

    /// Begins rendering into an acquired transparent UI target.
    /// @param commandList Command list that records the UI pass.
    /// @param uiFrame Target identity returned by acquireUiFrame().
    /// @return True when barriers and the rendering scope were recorded.
    [[nodiscard]] bool beginUiRendering(RhiCommandList& commandList,
                                         const PresentationUiFrame& uiFrame);

    /// Ends UI rendering and records premultiplied-alpha composition to the swapchain.
    /// @param commandList Command list containing the UI pass.
    /// @param frame Matching real frame returned by beginFrame().
    /// @param uiFrame Matching UI target returned by acquireUiFrame().
    /// @return True when composition and final resource barriers were recorded.
    [[nodiscard]] bool endUiRenderingAndComposite(
        RhiCommandList& commandList,
        const PresentationFrame& frame,
        const PresentationUiFrame& uiFrame);

    /// Closes and submits the command list containing UI and composition work.
    /// @param commandList Executable UI command list to submit.
    /// @param uiFrame UI target whose reuse is tracked by the submission token.
    /// @return True when the submission was accepted.
    [[nodiscard]] bool submitUiFrame(RhiCommandList& commandList,
                                      const PresentationUiFrame& uiFrame);

    /// Validates and tags presentation and temporal inputs for DLSS-G.
    /// @param frame Matching real frame returned by beginFrame().
    /// @param uiFrame Matching UI target returned by acquireUiFrame().
    /// @param temporalFrame Complete temporal resources and common camera constants.
    /// @return True when DLSS-G inputs are ready or interpolation is inactive.
    [[nodiscard]] bool prepareFrameGeneration(
        const PresentationFrame& frame,
        const PresentationUiFrame& uiFrame,
        const TemporalFrameInput& temporalFrame);

    /// Returns the immutable cumulative presentation statistics.
    [[nodiscard]] const PresentationStatistics& statistics() const {
        return m_statistics;
    }

    /// Returns the active presentation technology.
    [[nodiscard]] PresentationMode mode() const {
        return m_statistics.mode;
    }

private:
    struct UiSlot {
        RhiTextureHandle hudlessColorTexture;
        RhiTextureViewHandle hudlessColorView;
        RhiTextureHandle colorTexture;
        RhiTextureViewHandle colorView;
        RhiTextureHandle depthTexture;
        RhiTextureViewHandle depthView;
        RhiResourceState hudlessColorState = RhiResourceState::Undefined;
        RhiResourceState colorState = RhiResourceState::Undefined;
        RhiResourceState depthState = RhiResourceState::Undefined;
        RhiSubmissionToken completionToken;
    };

    [[nodiscard]] PresentationFrame failBegin(
        PresentationFailure failure,
        RhiFrameStatus status = RhiFrameStatus::Error);
    [[nodiscard]] PresentationCompleteResult failPresent(
        PresentationFailure failure,
        RhiFrameStatus status = RhiFrameStatus::Error);
    void invalidateExtentForStatus(RhiFrameStatus status);
    [[nodiscard]] bool validateUiFrame(const PresentationUiFrame& uiFrame) const;
    [[nodiscard]] bool ensureUiTargets(uint32_t width, uint32_t height);
    [[nodiscard]] bool createUiCompositionPipeline();
    void destroyUiTargets();
    void destroyUiCompositionPipeline();
    [[nodiscard]] bool waitForUiSlot(UiSlot& slot);
    [[nodiscard]] std::optional<PresentationFailure> applyPendingDisplayState();
    [[nodiscard]] PresentationFrame beginFrameExtent(int width, int height);
    [[nodiscard]] bool suspendFrameGenerationForSwapchainChange();
    [[nodiscard]] bool resumeFrameGenerationAfterAcquire();

    PresentationBackend& m_backend;
    PresentationStatistics m_statistics;
    RhiFrameAcquireResult m_openFrame;
    uint64_t m_openRealFrameNumber = 0u;
    uint32_t m_width = 0u;
    uint32_t m_height = 0u;
    bool m_extentValid = false;
    bool m_frameOpen = false;
    Window* m_window = nullptr;
    std::optional<bool> m_requestedVsyncEnabled;
    std::optional<bool> m_requestedFullscreenEnabled;
    std::optional<bool> m_requestedFrameGenerationEnabled;
    std::optional<bool> m_requestedRenderingGameFrames;
    bool m_renderingGameFrames = false;
    bool m_resumeFrameGeneration = false;
    bool m_frameGenerationPrepared = false;
    RhiDevice* m_uiDevice = nullptr;
    RhiTextureFormat m_uiColorFormat = RhiTextureFormat::Undefined;
    RhiTextureFormat m_uiDepthFormat = RhiTextureFormat::Undefined;
    uint32_t m_uiWidth = 0u;
    uint32_t m_uiHeight = 0u;
    uint32_t m_nextUiSlot = 0u;
    uint64_t m_uiResourceGeneration = 0u;
    std::vector<UiSlot> m_uiSlots;
    RhiSamplerHandle m_uiSampler;
    RhiShaderHandle m_uiCompositeVertexShader;
    RhiShaderHandle m_uiCompositeFragmentShader;
    RhiBindGroupLayoutHandle m_uiCompositeBindGroupLayout;
    RhiPipelineLayoutHandle m_uiCompositePipelineLayout;
    RhiPipelineHandle m_uiCompositePipeline;
    std::vector<RhiBindGroupHandle> m_uiCompositeBindGroups;
};

/// Returns a stable diagnostic message for one presentation failure.
/// @param failure Failure code reported by PresentationController.
/// @return English diagnostic text suitable for logs.
[[nodiscard]] const char* presentationFailureMessage(PresentationFailure failure);

#endif // MECRAFT_PRESENTATION_CONTROLLER_H
