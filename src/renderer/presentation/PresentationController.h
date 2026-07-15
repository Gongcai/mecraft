#ifndef MECRAFT_PRESENTATION_CONTROLLER_H
#define MECRAFT_PRESENTATION_CONTROLLER_H

#include "renderer/rhi/RhiTypes.h"

#include <cstdint>
#include <memory>

class RhiDevice;

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

    PresentationController(const PresentationController&) = delete;
    PresentationController& operator=(const PresentationController&) = delete;

    /// Resizes presentation resources when required and acquires one real frame.
    /// @param width Requested framebuffer width in physical pixels.
    /// @param height Requested framebuffer height in physical pixels.
    /// @return A renderable frame, a non-fatal skip, or an explicit failure.
    [[nodiscard]] PresentationFrame beginFrame(int width, int height);

    /// Presents and closes the matching frame returned by beginFrame().
    /// @param frame Live frame identity returned by this controller.
    /// @return Presentation completion state and backend status.
    [[nodiscard]] PresentationCompleteResult presentFrame(
        const PresentationFrame& frame);

    /// Returns the immutable cumulative presentation statistics.
    [[nodiscard]] const PresentationStatistics& statistics() const {
        return m_statistics;
    }

    /// Returns the active presentation technology.
    [[nodiscard]] PresentationMode mode() const {
        return m_statistics.mode;
    }

private:
    [[nodiscard]] PresentationFrame failBegin(
        PresentationFailure failure,
        RhiFrameStatus status = RhiFrameStatus::Error);
    [[nodiscard]] PresentationCompleteResult failPresent(
        PresentationFailure failure,
        RhiFrameStatus status = RhiFrameStatus::Error);
    void invalidateExtentForStatus(RhiFrameStatus status);

    PresentationBackend& m_backend;
    PresentationStatistics m_statistics;
    RhiFrameAcquireResult m_openFrame;
    uint64_t m_openRealFrameNumber = 0u;
    uint32_t m_width = 0u;
    uint32_t m_height = 0u;
    bool m_extentValid = false;
    bool m_frameOpen = false;
};

/// Returns a stable diagnostic message for one presentation failure.
/// @param failure Failure code reported by PresentationController.
/// @return English diagnostic text suitable for logs.
[[nodiscard]] const char* presentationFailureMessage(PresentationFailure failure);

#endif // MECRAFT_PRESENTATION_CONTROLLER_H
