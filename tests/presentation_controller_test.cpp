#include "renderer/presentation/PresentationController.h"
#include "engine/platform/Window.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "[presentation_controller_test] FAIL: %s\n", message);
    std::abort();
}

void require(const bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

RhiFrameAcquireResult acquiredFrame(const RhiFrameStatus status, const uint64_t frameIndex, const uint32_t imageIndex) {
    RhiFrameAcquireResult result;
    result.status = status;
    result.frameIndex = frameIndex;
    result.imageIndex = imageIndex;
    result.width = 1920u;
    result.height = 1080u;
    return result;
}

class FakePresentationBackend final : public PresentationBackend {
public:
    explicit FakePresentationBackend(const PresentationMode presentationMode = PresentationMode::Native)
        : m_mode(presentationMode) {}

    [[nodiscard]] PresentationMode mode() const override { return m_mode; }

    bool resize(const uint32_t width, const uint32_t height) override {
        ++resizeCalls;
        lastWidth = width;
        lastHeight = height;
        return resizeAccepted;
    }

    [[nodiscard]] RhiFrameAcquireResult acquireFrame() override {
        ++acquireCalls;
        if (nextAcquireResult >= acquireResults.size()) {
            fail("fake backend exhausted its acquire results");
        }
        return acquireResults[nextAcquireResult++];
    }

    PresentationBackendPresentResult presentFrame(const RhiPresentInfo& info) override {
        ++presentCalls;
        lastPresentInfo = info;
        if (nextPresentResult >= presentResults.size()) {
            fail("fake backend exhausted its present results");
        }
        return presentResults[nextPresentResult++];
    }

    bool cancelFrame(const RhiPresentInfo& info) override {
        ++cancelCalls;
        lastCancelInfo = info;
        return cancelAccepted;
    }

    [[nodiscard]] bool vsyncEnabled() const override { return currentVsyncEnabled; }

    [[nodiscard]] bool supportsVsyncControl() const override { return vsyncSupported; }

    bool setVsyncEnabled(const bool enabled) override {
        ++setVsyncCalls;
        if (!setVsyncAccepted) {
            return false;
        }
        currentVsyncEnabled = enabled;
        return true;
    }

    [[nodiscard]] bool supportsFrameGeneration() const override { return frameGenerationSupported; }

    [[nodiscard]] bool frameGenerationEnabled() const override { return frameGenerationUserEnabled; }

    [[nodiscard]] bool frameGenerationActive() const override {
        return frameGenerationUserEnabled && renderingGameFrames;
    }

    bool setFrameGenerationEnabled(const bool enabled) override {
        ++setFrameGenerationCalls;
        if (!frameGenerationSupported || !setFrameGenerationAccepted) {
            return false;
        }
        frameGenerationUserEnabled = enabled;
        if (!enabled) {
            renderingGameFrames = false;
        }
        m_mode = enabled ? PresentationMode::DlssFrameGeneration : PresentationMode::Native;
        return true;
    }

    bool setRenderingGameFrames(const bool enabled) override {
        ++setRenderingGameFramesCalls;
        renderingGameFrameRequests.push_back(enabled);
        if ((enabled && !resumeFrameGenerationAccepted) || (!enabled && !suspendFrameGenerationAccepted)) {
            return false;
        }
        if (frameGenerationUserEnabled) {
            renderingGameFrames = enabled;
        }
        return true;
    }

    [[nodiscard]] bool requiresFrameGenerationInputs() const override {
        return requireFrameGenerationInputs && frameGenerationActive();
    }

    bool prepareFrameGeneration(const PresentationFrameResources&, const TemporalFrameInput&) override {
        ++prepareFrameGenerationCalls;
        return prepareFrameGenerationAccepted;
    }

    PresentationMode m_mode = PresentationMode::Native;
    bool resizeAccepted = true;
    bool vsyncSupported = true;
    bool setVsyncAccepted = true;
    bool currentVsyncEnabled = false;
    bool frameGenerationSupported = false;
    bool frameGenerationUserEnabled = false;
    bool renderingGameFrames = false;
    bool setFrameGenerationAccepted = true;
    bool requireFrameGenerationInputs = false;
    bool prepareFrameGenerationAccepted = true;
    bool resumeFrameGenerationAccepted = true;
    bool suspendFrameGenerationAccepted = true;
    bool cancelAccepted = true;
    uint32_t resizeCalls = 0u;
    uint32_t acquireCalls = 0u;
    uint32_t presentCalls = 0u;
    uint32_t cancelCalls = 0u;
    uint32_t setVsyncCalls = 0u;
    uint32_t setFrameGenerationCalls = 0u;
    uint32_t setRenderingGameFramesCalls = 0u;
    uint32_t prepareFrameGenerationCalls = 0u;
    uint32_t lastWidth = 0u;
    uint32_t lastHeight = 0u;
    RhiPresentInfo lastPresentInfo;
    RhiPresentInfo lastCancelInfo;
    std::vector<RhiFrameAcquireResult> acquireResults;
    std::vector<PresentationBackendPresentResult> presentResults;
    std::vector<bool> renderingGameFrameRequests;
    size_t nextAcquireResult = 0u;
    size_t nextPresentResult = 0u;
};

void testQueuedVsyncChanges() {
    FakePresentationBackend backend;
    backend.acquireResults = {acquiredFrame(RhiFrameStatus::Success, 51u, 0u),
                              acquiredFrame(RhiFrameStatus::Success, 52u, 1u)};
    backend.presentResults = {{RhiFrameStatus::Success, 1u, 0u}, {RhiFrameStatus::Success, 1u, 0u}};
    PresentationController controller(backend);

    require(controller.requestVsyncEnabled(true),
            "supported VSync changes must be accepted for the next frame boundary");
    require(controller.vsyncEnabled() && !backend.currentVsyncEnabled,
            "the controller must expose the queued VSync state before backend mutation");
    const PresentationFrame firstFrame = controller.beginFrame(1600, 900);
    require(firstFrame.shouldRender() && backend.currentVsyncEnabled && backend.setVsyncCalls == 1u,
            "queued VSync must apply before frame acquisition");

    require(controller.requestVsyncEnabled(false), "VSync changes requested during an open frame must remain queued");
    require(backend.currentVsyncEnabled, "an open frame must retain its active VSync state until presentation");
    require(controller.presentFrame(firstFrame).result == PresentationResult::Presented,
            "the frame preceding a queued VSync change must present normally");

    const PresentationFrame secondFrame = controller.beginFrame(1600, 900);
    require(secondFrame.shouldRender() && !backend.currentVsyncEnabled && backend.setVsyncCalls == 2u,
            "the second queued VSync state must apply at the next frame boundary");
    require(controller.presentFrame(secondFrame).result == PresentationResult::Presented,
            "the frame following a VSync change must present normally");
    require(controller.statistics().vsyncChanges == 2u, "presentation statistics must count applied VSync changes");

    FakePresentationBackend unsupportedBackend;
    unsupportedBackend.vsyncSupported = false;
    PresentationController unsupportedController(unsupportedBackend);
    require(!unsupportedController.requestVsyncEnabled(true),
            "unsupported runtime VSync changes must be rejected explicitly");
}

void testQueuedFullscreenChanges() {
    Window window;
    require(window.initializePlatform(), "fullscreen presentation testing requires an initialized window platform");
    require(window.create(640, 360, "presentation_controller_test"),
            "fullscreen presentation testing requires a native window");

    FakePresentationBackend backend;
    backend.acquireResults = {acquiredFrame(RhiFrameStatus::Success, 61u, 0u),
                              acquiredFrame(RhiFrameStatus::Success, 62u, 1u)};
    backend.presentResults = {{RhiFrameStatus::Success, 1u, 0u}, {RhiFrameStatus::Success, 1u, 0u}};
    PresentationController controller(backend);
    require(controller.initWindowState(window), "the controller must bind its native presentation window");
    require(controller.requestFullscreenEnabled(true),
            "fullscreen changes must be accepted for the next frame boundary");
    require(controller.fullscreenEnabled() && !window.isFullscreen(),
            "the controller must expose queued fullscreen state before window mutation");

    const PresentationFrame fullscreenFrame = controller.beginFrame(window);
    require(fullscreenFrame.shouldRender() && window.isFullscreen(),
            "queued fullscreen mode must apply before frame acquisition");
    require(controller.requestFullscreenEnabled(false),
            "windowed mode requested during an open frame must remain queued");
    require(window.isFullscreen(), "an open frame must retain its current fullscreen state until presentation");
    require(controller.presentFrame(fullscreenFrame).result == PresentationResult::Presented,
            "the fullscreen frame must present before restoring windowed mode");

    const PresentationFrame windowedFrame = controller.beginFrame(window);
    require(windowedFrame.shouldRender() && !window.isFullscreen(),
            "queued windowed mode must apply at the next frame boundary");
    require(controller.presentFrame(windowedFrame).result == PresentationResult::Presented,
            "the restored windowed frame must present normally");
    require(controller.statistics().fullscreenChanges == 2u,
            "presentation statistics must count applied fullscreen changes");
    window.destroy();
}

void testNativeLifecycleAndIdentityContract() {
    FakePresentationBackend backend;
    backend.acquireResults.push_back(acquiredFrame(RhiFrameStatus::Success, 17u, 2u));
    backend.presentResults.push_back({RhiFrameStatus::Success, 1u, 0u});
    PresentationController controller(backend);

    const PresentationFrame minimized = controller.beginFrame(0, 1080);
    require(minimized.result == PresentationResult::Skipped, "zero framebuffer extent must skip the frame");
    require(backend.resizeCalls == 0u && backend.acquireCalls == 0u,
            "minimized frames must not touch the presentation backend");

    const PresentationFrame frame = controller.beginFrame(1920, 1080);
    require(frame.shouldRender(), "successful acquisition must produce a renderable frame");
    require(frame.realFrameNumber == 1u, "the first acquired real frame must receive number one");
    require(backend.resizeCalls == 1u && backend.acquireCalls == 1u,
            "the first frame must resize and acquire exactly once");
    require(!controller.acquireUiFrame(frame).has_value(),
            "UI target acquisition must require explicit composition initialization");

    const PresentationFrame duplicateAcquire = controller.beginFrame(1920, 1080);
    require(duplicateAcquire.failure == PresentationFailure::FrameAlreadyOpen,
            "a second acquisition must be rejected while a frame is open");

    PresentationFrame wrongIdentity = frame;
    ++wrongIdentity.acquired.imageIndex;
    const PresentationCompleteResult wrongPresent = controller.presentFrame(wrongIdentity);
    require(wrongPresent.failure == PresentationFailure::FrameIdentityMismatch,
            "presentation must reject a foreign frame identity");
    require(backend.presentCalls == 0u, "identity rejection must occur before backend presentation");

    const PresentationCompleteResult presented = controller.presentFrame(frame);
    require(presented.result == PresentationResult::Presented, "the matching acquired frame must present successfully");
    require(backend.presentCalls == 1u && backend.lastPresentInfo.frameIndex == 17u &&
                backend.lastPresentInfo.imageIndex == 2u,
            "the controller must preserve the backend frame identity");

    const PresentationCompleteResult duplicatePresent = controller.presentFrame(frame);
    require(duplicatePresent.failure == PresentationFailure::FrameNotOpen, "a frame must close after one presentation");

    const PresentationStatistics& stats = controller.statistics();
    require(stats.realFramesAcquired == 1u && stats.realFramesPresented == 1u && stats.displayedFrames == 1u,
            "native presentation counters must distinguish acquired and displayed frames");
    require(stats.skippedFrames == 1u && stats.failedOperations == 3u,
            "skipped frames and contract violations must be counted independently");
}

void testResizeInvalidationAndGeneratedFrameStatistics() {
    FakePresentationBackend backend(PresentationMode::Fsr31FrameGeneration);
    backend.acquireResults = {acquiredFrame(RhiFrameStatus::OutOfDate, 0u, 0u),
                              acquiredFrame(RhiFrameStatus::Success, 21u, 0u),
                              acquiredFrame(RhiFrameStatus::Success, 22u, 1u)};
    backend.presentResults = {{RhiFrameStatus::OutOfDate, 0u, 0u}, {RhiFrameStatus::Success, 2u, 1u}};
    PresentationController controller(backend);

    require(controller.beginFrame(1920, 1080).result == PresentationResult::Skipped,
            "out-of-date acquisition must skip without becoming fatal");
    const PresentationFrame firstRenderable = controller.beginFrame(1920, 1080);
    require(firstRenderable.shouldRender(), "the frame after swapchain invalidation must acquire successfully");
    require(controller.presentFrame(firstRenderable).result == PresentationResult::Skipped,
            "out-of-date presentation must close and skip the frame");

    const PresentationFrame generatedFrame = controller.beginFrame(1920, 1080);
    require(generatedFrame.realFrameNumber == 2u,
            "only successful real-frame acquisitions must advance real frame numbers");
    require(controller.presentFrame(generatedFrame).result == PresentationResult::Presented,
            "frame-generation backend result must present successfully");

    const PresentationStatistics& stats = controller.statistics();
    require(stats.mode == PresentationMode::Fsr31FrameGeneration, "statistics must retain the explicit backend mode");
    require(stats.resizeOperations == 3u && backend.resizeCalls == 3u,
            "out-of-date acquire and present results must invalidate the tracked extent");
    require(stats.realFramesAcquired == 2u && stats.realFramesPresented == 1u,
            "real-frame counters must exclude skipped acquisitions and presentations");
    require(stats.generatedFramesPresented == 1u && stats.displayedFrames == 2u,
            "generated and total displayed frames must be tracked separately");
    require(stats.skippedFrames == 2u, "acquire and present skips must both be recorded");
}

void testExplicitBackendFailures() {
    FakePresentationBackend resizeBackend;
    resizeBackend.resizeAccepted = false;
    PresentationController resizeController(resizeBackend);
    const PresentationFrame resizeFailure = resizeController.beginFrame(1280, 720);
    require(resizeFailure.failure == PresentationFailure::ResizeRejected,
            "resize rejection must be reported explicitly");

    FakePresentationBackend acquireBackend;
    acquireBackend.acquireResults.push_back(acquiredFrame(RhiFrameStatus::DeviceLost, 0u, 0u));
    PresentationController acquireController(acquireBackend);
    const PresentationFrame acquireFailure = acquireController.beginFrame(1280, 720);
    require(acquireFailure.failure == PresentationFailure::AcquireRejected &&
                acquireFailure.rhiStatus == RhiFrameStatus::DeviceLost,
            "device loss during acquisition must retain the backend status");

    FakePresentationBackend presentBackend;
    presentBackend.acquireResults.push_back(acquiredFrame(RhiFrameStatus::Success, 31u, 0u));
    presentBackend.presentResults.push_back({RhiFrameStatus::DeviceLost, 0u, 0u});
    PresentationController presentController(presentBackend);
    const PresentationFrame presentFrame = presentController.beginFrame(1280, 720);
    const PresentationCompleteResult presentFailure = presentController.presentFrame(presentFrame);
    require(presentFailure.failure == PresentationFailure::PresentRejected &&
                presentFailure.rhiStatus == RhiFrameStatus::DeviceLost,
            "device loss during presentation must retain the backend status");

    FakePresentationBackend countBackend;
    countBackend.acquireResults.push_back(acquiredFrame(RhiFrameStatus::Success, 41u, 0u));
    countBackend.presentResults.push_back({RhiFrameStatus::Success, 1u, 1u});
    PresentationController countController(countBackend);
    const PresentationFrame countFrame = countController.beginFrame(1280, 720);
    const PresentationCompleteResult countFailure = countController.presentFrame(countFrame);
    require(countFailure.failure == PresentationFailure::BackendFrameCountInvalid,
            "generated-frame counts must be smaller than total displayed-frame counts");
}

void testFrameGenerationLifecycle() {
    FakePresentationBackend backend;
    backend.frameGenerationSupported = true;
    backend.acquireResults = {
        acquiredFrame(RhiFrameStatus::Success, 71u, 0u), acquiredFrame(RhiFrameStatus::Success, 72u, 1u),
        acquiredFrame(RhiFrameStatus::Success, 73u, 0u), acquiredFrame(RhiFrameStatus::Success, 74u, 1u),
        acquiredFrame(RhiFrameStatus::Success, 75u, 0u)};
    backend.presentResults = {{RhiFrameStatus::Success, 1u, 0u},
                              {RhiFrameStatus::Success, 2u, 1u},
                              {RhiFrameStatus::Success, 1u, 0u},
                              {RhiFrameStatus::Success, 2u, 1u},
                              {RhiFrameStatus::Success, 1u, 0u}};
    PresentationController controller(backend);

    const PresentationFrame nativeFrame = controller.beginFrame(1920, 1080);
    require(nativeFrame.shouldRender() && controller.presentFrame(nativeFrame).result == PresentationResult::Presented,
            "the native frame preceding DLSS-G activation must present normally");

    require(controller.requestFrameGenerationEnabled(true) && controller.requestRenderingGameFrames(true),
            "DLSS-G enable and gameplay activity requests must be accepted");
    require(!controller.requestVsyncEnabled(true) && !controller.vsyncControlAvailable(),
            "a queued DLSS-G swapchain must reject application-controlled VSync changes");
    const PresentationFrame enabledFrame = controller.beginFrame(1920, 1080);
    require(enabledFrame.shouldRender() && backend.frameGenerationUserEnabled && backend.renderingGameFrames &&
                backend.resizeCalls == 1u,
            "DLSS-G activation must not invalidate an unchanged framebuffer extent");
    require(controller.presentFrame(enabledFrame).result == PresentationResult::Presented,
            "an active DLSS-G frame must present two display images");

    require(controller.requestRenderingGameFrames(false), "gameplay pause must queue interpolation suspension");
    const PresentationFrame pausedFrame = controller.beginFrame(1920, 1080);
    require(pausedFrame.shouldRender() && !backend.renderingGameFrames &&
                controller.presentFrame(pausedFrame).result == PresentationResult::Presented,
            "a paused gameplay frame must retain the DLSS-G swapchain without interpolation");

    require(controller.requestRenderingGameFrames(true), "gameplay resume must queue interpolation activation");
    const PresentationFrame resizedFrame = controller.beginFrame(1600, 900);
    require(resizedFrame.shouldRender() && backend.renderingGameFrames && backend.resizeCalls == 2u,
            "framebuffer resize must suspend DLSS-G, resize once, and resume after acquire");
    require(backend.renderingGameFrameRequests.size() >= 5u &&
                backend.renderingGameFrameRequests[backend.renderingGameFrameRequests.size() - 3u] &&
                !backend.renderingGameFrameRequests[backend.renderingGameFrameRequests.size() - 2u] &&
                backend.renderingGameFrameRequests.back(),
            "resize processing must issue the expected active, suspended, active sequence");
    require(controller.presentFrame(resizedFrame).result == PresentationResult::Presented,
            "DLSS-G must remain active after a successful resize");

    require(controller.requestRenderingGameFrames(false) && controller.requestFrameGenerationEnabled(false),
            "DLSS-G disable must be accepted at a frame boundary");
    const PresentationFrame disabledFrame = controller.beginFrame(1600, 900);
    require(disabledFrame.shouldRender() && !backend.frameGenerationUserEnabled &&
                controller.mode() == PresentationMode::Native && controller.vsyncControlAvailable(),
            "disabling DLSS-G must restore native presentation mode");
    require(controller.presentFrame(disabledFrame).result == PresentationResult::Presented,
            "the first native frame after DLSS-G shutdown must present normally");

    const PresentationStatistics& stats = controller.statistics();
    require(stats.realFramesPresented == 5u && stats.generatedFramesPresented == 2u && stats.displayedFrames == 7u,
            "DLSS-G lifecycle statistics must separate real and interpolated images");
}

void testFrameGenerationInputRequirement() {
    FakePresentationBackend backend(PresentationMode::DlssFrameGeneration);
    backend.frameGenerationSupported = true;
    backend.frameGenerationUserEnabled = true;
    backend.renderingGameFrames = true;
    backend.requireFrameGenerationInputs = true;
    backend.acquireResults.push_back(acquiredFrame(RhiFrameStatus::Success, 81u, 0u));
    PresentationController controller(backend);
    require(controller.requestRenderingGameFrames(true),
            "the DLSS-G input-contract test must request active gameplay interpolation");

    const PresentationFrame frame = controller.beginFrame(1280, 720);
    require(frame.shouldRender(), "the DLSS-G input-contract test requires an acquired frame");
    const PresentationCompleteResult result = controller.presentFrame(frame);
    require(result.failure == PresentationFailure::FrameGenerationInputsRejected && backend.presentCalls == 0u &&
                backend.cancelCalls == 1u,
            "active DLSS-G must reject presentation before the backend sees incomplete inputs");
}

void testFrameGenerationSuspendsWhileMinimized() {
    FakePresentationBackend backend(PresentationMode::DlssFrameGeneration);
    backend.frameGenerationSupported = true;
    backend.frameGenerationUserEnabled = true;
    backend.renderingGameFrames = true;
    backend.acquireResults.push_back(acquiredFrame(RhiFrameStatus::Success, 91u, 0u));
    backend.presentResults.push_back({RhiFrameStatus::Success, 2u, 1u});
    PresentationController controller(backend);
    require(controller.requestRenderingGameFrames(true),
            "the minimized-window test must start with active interpolation");

    const PresentationFrame minimizedFrame = controller.beginFrame(0, 0);
    require(minimizedFrame.result == PresentationResult::Skipped &&
                minimizedFrame.rhiStatus == RhiFrameStatus::Minimized && !backend.renderingGameFrames &&
                backend.acquireCalls == 0u,
            "minimization must suspend DLSS-G before skipping acquisition");

    const PresentationFrame restoredFrame = controller.beginFrame(1280, 720);
    require(restoredFrame.shouldRender() && backend.renderingGameFrames && backend.acquireCalls == 1u,
            "restoring the window must resume DLSS-G only after acquisition");
    require(controller.presentFrame(restoredFrame).result == PresentationResult::Presented,
            "the first restored DLSS-G frame must present normally");
}

void testFrameGenerationResumeFailureCancelsAcquiredFrame() {
    FakePresentationBackend backend(PresentationMode::DlssFrameGeneration);
    backend.frameGenerationSupported = true;
    backend.frameGenerationUserEnabled = true;
    backend.renderingGameFrames = true;
    backend.resumeFrameGenerationAccepted = false;
    backend.acquireResults.push_back(acquiredFrame(RhiFrameStatus::Success, 101u, 1u));
    PresentationController controller(backend);
    require(controller.requestRenderingGameFrames(true),
            "the resume-failure test must start with active interpolation");

    const PresentationFrame frame = controller.beginFrame(1280, 720);
    require(frame.failure == PresentationFailure::FrameGenerationRejected && backend.cancelCalls == 1u &&
                backend.lastCancelInfo.frameIndex == 101u && backend.lastCancelInfo.imageIndex == 1u,
            "a failed DLSS-G resume must release the image acquired after resize");
}

void testFrameGenerationInputCancellationFailureIsReported() {
    FakePresentationBackend backend(PresentationMode::DlssFrameGeneration);
    backend.frameGenerationSupported = true;
    backend.frameGenerationUserEnabled = true;
    backend.renderingGameFrames = true;
    backend.requireFrameGenerationInputs = true;
    backend.cancelAccepted = false;
    backend.acquireResults.push_back(acquiredFrame(RhiFrameStatus::Success, 111u, 0u));
    PresentationController controller(backend);
    require(controller.requestRenderingGameFrames(true),
            "the cancellation-failure test must start with active interpolation");

    const PresentationFrame frame = controller.beginFrame(1280, 720);
    require(frame.shouldRender(), "the cancellation-failure test requires an acquired frame");
    const PresentationCompleteResult result = controller.presentFrame(frame);
    require(result.failure == PresentationFailure::FrameCancellationRejected && backend.cancelCalls == 1u &&
                backend.presentCalls == 0u,
            "a failed frame cancellation must not be reported as an input error");
}

void testFrameGenerationSuspensionFailureStillReleasesFrame() {
    FakePresentationBackend backend(PresentationMode::DlssFrameGeneration);
    backend.frameGenerationSupported = true;
    backend.frameGenerationUserEnabled = true;
    backend.renderingGameFrames = true;
    backend.acquireResults = {acquiredFrame(RhiFrameStatus::Success, 121u, 0u),
                              acquiredFrame(RhiFrameStatus::Success, 122u, 1u)};
    PresentationController controller(backend);
    require(controller.requestRenderingGameFrames(true),
            "the suspension-failure test must start with active interpolation");

    const PresentationFrame firstFrame = controller.beginFrame(1280, 720);
    backend.suspendFrameGenerationAccepted = false;
    require(firstFrame.shouldRender() && !controller.cancelFrame(firstFrame) && backend.cancelCalls == 1u,
            "a failed interpolation suspension must still release the acquired frame");
    const PresentationFrame secondFrame = controller.beginFrame(1280, 720);
    require(secondFrame.shouldRender(), "the controller must close its frame identity after backend release");
}

} // namespace

int main() {
    testQueuedVsyncChanges();
    testQueuedFullscreenChanges();
    testNativeLifecycleAndIdentityContract();
    testResizeInvalidationAndGeneratedFrameStatistics();
    testExplicitBackendFailures();
    testFrameGenerationLifecycle();
    testFrameGenerationInputRequirement();
    testFrameGenerationSuspendsWhileMinimized();
    testFrameGenerationResumeFailureCancelsAcquiredFrame();
    testFrameGenerationInputCancellationFailureIsReported();
    testFrameGenerationSuspensionFailureStillReleasesFrame();
    std::printf("[presentation_controller_test] PASS\n");
    return EXIT_SUCCESS;
}
