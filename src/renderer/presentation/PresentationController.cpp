#include "PresentationController.h"

#include "renderer/rhi/RhiDevice.h"

#include <cstdlib>

namespace {

[[nodiscard]] bool isRenderableAcquireStatus(const RhiFrameStatus status) {
    return status == RhiFrameStatus::Success ||
           status == RhiFrameStatus::Suboptimal;
}

class NativePresentationBackend final : public PresentationBackend {
public:
    explicit NativePresentationBackend(RhiDevice& rhiDevice)
        : m_rhiDevice(rhiDevice) {
    }

    [[nodiscard]] PresentationMode mode() const override {
        return PresentationMode::Native;
    }

    bool resize(const uint32_t width, const uint32_t height) override {
        return m_rhiDevice.resizeSwapchain(width, height);
    }

    [[nodiscard]] RhiFrameAcquireResult acquireFrame() override {
        return m_rhiDevice.acquireFrame();
    }

    PresentationBackendPresentResult presentFrame(
        const RhiPresentInfo& info) override {
        const RhiFrameStatus status = m_rhiDevice.presentFrame(info);
        const uint32_t displayedFrameCount = isRenderableAcquireStatus(status)
            ? 1u
            : 0u;
        return {status, displayedFrameCount, 0u};
    }

private:
    RhiDevice& m_rhiDevice;
};

[[nodiscard]] bool isNonFatalPresentationStatus(const RhiFrameStatus status) {
    return status == RhiFrameStatus::OutOfDate ||
           status == RhiFrameStatus::Minimized ||
           status == RhiFrameStatus::SurfaceLost;
}

} // namespace

std::unique_ptr<PresentationBackend> createNativePresentationBackend(
    RhiDevice& rhiDevice) {
    return std::make_unique<NativePresentationBackend>(rhiDevice);
}

PresentationController::PresentationController(
    PresentationBackend& backend)
    : m_backend(backend) {
    m_statistics.mode = m_backend.mode();
}

PresentationFrame PresentationController::beginFrame(const int width,
                                                       const int height) {
    if (m_frameOpen) {
        return failBegin(PresentationFailure::FrameAlreadyOpen);
    }
    if (width <= 0 || height <= 0) {
        ++m_statistics.skippedFrames;
        m_statistics.lastAcquireStatus = RhiFrameStatus::Minimized;
        m_statistics.lastFailure = PresentationFailure::None;
        return {
            PresentationResult::Skipped,
            PresentationFailure::None,
            RhiFrameStatus::Minimized,
            {},
            0u
        };
    }

    const uint32_t requestedWidth = static_cast<uint32_t>(width);
    const uint32_t requestedHeight = static_cast<uint32_t>(height);
    if (!m_extentValid || requestedWidth != m_width || requestedHeight != m_height) {
        if (!m_backend.resize(requestedWidth, requestedHeight)) {
            return failBegin(PresentationFailure::ResizeRejected);
        }
        m_width = requestedWidth;
        m_height = requestedHeight;
        m_extentValid = true;
        ++m_statistics.resizeOperations;
    }

    ++m_statistics.acquireAttempts;
    RhiFrameAcquireResult acquired = m_backend.acquireFrame();
    m_statistics.lastAcquireStatus = acquired.status;
    if (isRenderableAcquireStatus(acquired.status)) {
        m_frameOpen = true;
        m_openFrame = acquired;
        m_openRealFrameNumber = ++m_statistics.realFramesAcquired;
        m_statistics.lastFailure = PresentationFailure::None;
        return {
            PresentationResult::Ready,
            PresentationFailure::None,
            acquired.status,
            acquired,
            m_openRealFrameNumber
        };
    }
    if (isNonFatalPresentationStatus(acquired.status)) {
        invalidateExtentForStatus(acquired.status);
        ++m_statistics.skippedFrames;
        m_statistics.lastFailure = PresentationFailure::None;
        return {
            PresentationResult::Skipped,
            PresentationFailure::None,
            acquired.status,
            acquired,
            0u
        };
    }
    return failBegin(PresentationFailure::AcquireRejected, acquired.status);
}

PresentationCompleteResult PresentationController::presentFrame(
    const PresentationFrame& frame) {
    if (!m_frameOpen) {
        return failPresent(PresentationFailure::FrameNotOpen);
    }
    if (!frame.shouldRender() ||
        frame.realFrameNumber != m_openRealFrameNumber ||
        frame.acquired.frameIndex != m_openFrame.frameIndex ||
        frame.acquired.imageIndex != m_openFrame.imageIndex) {
        return failPresent(PresentationFailure::FrameIdentityMismatch);
    }

    const PresentationBackendPresentResult backendResult = m_backend.presentFrame({
        m_openFrame.frameIndex,
        m_openFrame.imageIndex
    });
    const RhiFrameStatus status = backendResult.status;
    m_statistics.lastPresentStatus = status;
    m_frameOpen = false;
    m_openFrame = {};
    m_openRealFrameNumber = 0u;

    if (isRenderableAcquireStatus(status)) {
        if (backendResult.displayedFrameCount == 0u ||
            backendResult.generatedFrameCount >= backendResult.displayedFrameCount) {
            return failPresent(
                PresentationFailure::BackendFrameCountInvalid,
                status);
        }
        ++m_statistics.realFramesPresented;
        m_statistics.generatedFramesPresented += backendResult.generatedFrameCount;
        m_statistics.displayedFrames += backendResult.displayedFrameCount;
        m_statistics.lastFailure = PresentationFailure::None;
        return {
            PresentationResult::Presented,
            PresentationFailure::None,
            status
        };
    }
    if (isNonFatalPresentationStatus(status)) {
        invalidateExtentForStatus(status);
        ++m_statistics.skippedFrames;
        m_statistics.lastFailure = PresentationFailure::None;
        return {
            PresentationResult::Skipped,
            PresentationFailure::None,
            status
        };
    }
    return failPresent(PresentationFailure::PresentRejected, status);
}

PresentationFrame PresentationController::failBegin(
    const PresentationFailure failure,
    const RhiFrameStatus status) {
    ++m_statistics.failedOperations;
    m_statistics.lastFailure = failure;
    return {
        failure == PresentationFailure::FrameAlreadyOpen
            ? PresentationResult::ContractViolation
            : PresentationResult::Failed,
        failure,
        status,
        {},
        0u
    };
}

PresentationCompleteResult PresentationController::failPresent(
    const PresentationFailure failure,
    const RhiFrameStatus status) {
    ++m_statistics.failedOperations;
    m_statistics.lastFailure = failure;
    return {
        failure == PresentationFailure::FrameNotOpen ||
        failure == PresentationFailure::FrameIdentityMismatch
            ? PresentationResult::ContractViolation
            : PresentationResult::Failed,
        failure,
        status
    };
}

void PresentationController::invalidateExtentForStatus(
    const RhiFrameStatus status) {
    if (status == RhiFrameStatus::OutOfDate ||
        status == RhiFrameStatus::SurfaceLost) {
        m_extentValid = false;
    }
}

const char* presentationFailureMessage(const PresentationFailure failure) {
    switch (failure) {
    case PresentationFailure::None:
        return "no presentation failure";
    case PresentationFailure::ResizeRejected:
        return "presentation backend rejected the requested framebuffer extent";
    case PresentationFailure::AcquireRejected:
        return "presentation backend failed to acquire a real frame";
    case PresentationFailure::PresentRejected:
        return "presentation backend failed to present the acquired real frame";
    case PresentationFailure::BackendFrameCountInvalid:
        return "presentation backend reported invalid displayed-frame counters";
    case PresentationFailure::FrameAlreadyOpen:
        return "presentation controller already owns an acquired real frame";
    case PresentationFailure::FrameNotOpen:
        return "presentation controller has no acquired real frame to present";
    case PresentationFailure::FrameIdentityMismatch:
        return "presented frame identity does not match the acquired real frame";
    }
    std::abort();
}
