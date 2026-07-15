#include "renderer/presentation/PresentationController.h"

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

RhiFrameAcquireResult acquiredFrame(const RhiFrameStatus status,
                                    const uint64_t frameIndex,
                                    const uint32_t imageIndex) {
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
    explicit FakePresentationBackend(
        const PresentationMode presentationMode = PresentationMode::Native)
        : m_mode(presentationMode) {
    }

    [[nodiscard]] PresentationMode mode() const override {
        return m_mode;
    }

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

    PresentationBackendPresentResult presentFrame(
        const RhiPresentInfo& info) override {
        ++presentCalls;
        lastPresentInfo = info;
        if (nextPresentResult >= presentResults.size()) {
            fail("fake backend exhausted its present results");
        }
        return presentResults[nextPresentResult++];
    }

    PresentationMode m_mode = PresentationMode::Native;
    bool resizeAccepted = true;
    uint32_t resizeCalls = 0u;
    uint32_t acquireCalls = 0u;
    uint32_t presentCalls = 0u;
    uint32_t lastWidth = 0u;
    uint32_t lastHeight = 0u;
    RhiPresentInfo lastPresentInfo;
    std::vector<RhiFrameAcquireResult> acquireResults;
    std::vector<PresentationBackendPresentResult> presentResults;
    size_t nextAcquireResult = 0u;
    size_t nextPresentResult = 0u;
};

void testNativeLifecycleAndIdentityContract() {
    FakePresentationBackend backend;
    backend.acquireResults.push_back(
        acquiredFrame(RhiFrameStatus::Success, 17u, 2u));
    backend.presentResults.push_back(
        {RhiFrameStatus::Success, 1u, 0u});
    PresentationController controller(backend);

    const PresentationFrame minimized = controller.beginFrame(0, 1080);
    require(minimized.result == PresentationResult::Skipped,
            "zero framebuffer extent must skip the frame");
    require(backend.resizeCalls == 0u && backend.acquireCalls == 0u,
            "minimized frames must not touch the presentation backend");

    const PresentationFrame frame = controller.beginFrame(1920, 1080);
    require(frame.shouldRender(), "successful acquisition must produce a renderable frame");
    require(frame.realFrameNumber == 1u,
            "the first acquired real frame must receive number one");
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
    require(backend.presentCalls == 0u,
            "identity rejection must occur before backend presentation");

    const PresentationCompleteResult presented = controller.presentFrame(frame);
    require(presented.result == PresentationResult::Presented,
            "the matching acquired frame must present successfully");
    require(backend.presentCalls == 1u &&
            backend.lastPresentInfo.frameIndex == 17u &&
            backend.lastPresentInfo.imageIndex == 2u,
            "the controller must preserve the backend frame identity");

    const PresentationCompleteResult duplicatePresent = controller.presentFrame(frame);
    require(duplicatePresent.failure == PresentationFailure::FrameNotOpen,
            "a frame must close after one presentation");

    const PresentationStatistics& stats = controller.statistics();
    require(stats.realFramesAcquired == 1u &&
            stats.realFramesPresented == 1u &&
            stats.displayedFrames == 1u,
            "native presentation counters must distinguish acquired and displayed frames");
    require(stats.skippedFrames == 1u && stats.failedOperations == 3u,
            "skipped frames and contract violations must be counted independently");
}

void testResizeInvalidationAndGeneratedFrameStatistics() {
    FakePresentationBackend backend(PresentationMode::Fsr31FrameGeneration);
    backend.acquireResults = {
        acquiredFrame(RhiFrameStatus::OutOfDate, 0u, 0u),
        acquiredFrame(RhiFrameStatus::Success, 21u, 0u),
        acquiredFrame(RhiFrameStatus::Success, 22u, 1u)
    };
    backend.presentResults = {
        {RhiFrameStatus::OutOfDate, 0u, 0u},
        {RhiFrameStatus::Success, 2u, 1u}
    };
    PresentationController controller(backend);

    require(controller.beginFrame(1920, 1080).result == PresentationResult::Skipped,
            "out-of-date acquisition must skip without becoming fatal");
    const PresentationFrame firstRenderable = controller.beginFrame(1920, 1080);
    require(firstRenderable.shouldRender(),
            "the frame after swapchain invalidation must acquire successfully");
    require(controller.presentFrame(firstRenderable).result == PresentationResult::Skipped,
            "out-of-date presentation must close and skip the frame");

    const PresentationFrame generatedFrame = controller.beginFrame(1920, 1080);
    require(generatedFrame.realFrameNumber == 2u,
            "only successful real-frame acquisitions must advance real frame numbers");
    require(controller.presentFrame(generatedFrame).result == PresentationResult::Presented,
            "frame-generation backend result must present successfully");

    const PresentationStatistics& stats = controller.statistics();
    require(stats.mode == PresentationMode::Fsr31FrameGeneration,
            "statistics must retain the explicit backend mode");
    require(stats.resizeOperations == 3u && backend.resizeCalls == 3u,
            "out-of-date acquire and present results must invalidate the tracked extent");
    require(stats.realFramesAcquired == 2u && stats.realFramesPresented == 1u,
            "real-frame counters must exclude skipped acquisitions and presentations");
    require(stats.generatedFramesPresented == 1u && stats.displayedFrames == 2u,
            "generated and total displayed frames must be tracked separately");
    require(stats.skippedFrames == 2u,
            "acquire and present skips must both be recorded");
}

void testExplicitBackendFailures() {
    FakePresentationBackend resizeBackend;
    resizeBackend.resizeAccepted = false;
    PresentationController resizeController(resizeBackend);
    const PresentationFrame resizeFailure = resizeController.beginFrame(1280, 720);
    require(resizeFailure.failure == PresentationFailure::ResizeRejected,
            "resize rejection must be reported explicitly");

    FakePresentationBackend acquireBackend;
    acquireBackend.acquireResults.push_back(
        acquiredFrame(RhiFrameStatus::DeviceLost, 0u, 0u));
    PresentationController acquireController(acquireBackend);
    const PresentationFrame acquireFailure = acquireController.beginFrame(1280, 720);
    require(acquireFailure.failure == PresentationFailure::AcquireRejected &&
            acquireFailure.rhiStatus == RhiFrameStatus::DeviceLost,
            "device loss during acquisition must retain the backend status");

    FakePresentationBackend presentBackend;
    presentBackend.acquireResults.push_back(
        acquiredFrame(RhiFrameStatus::Success, 31u, 0u));
    presentBackend.presentResults.push_back(
        {RhiFrameStatus::DeviceLost, 0u, 0u});
    PresentationController presentController(presentBackend);
    const PresentationFrame presentFrame = presentController.beginFrame(1280, 720);
    const PresentationCompleteResult presentFailure =
        presentController.presentFrame(presentFrame);
    require(presentFailure.failure == PresentationFailure::PresentRejected &&
            presentFailure.rhiStatus == RhiFrameStatus::DeviceLost,
            "device loss during presentation must retain the backend status");

    FakePresentationBackend countBackend;
    countBackend.acquireResults.push_back(
        acquiredFrame(RhiFrameStatus::Success, 41u, 0u));
    countBackend.presentResults.push_back(
        {RhiFrameStatus::Success, 1u, 1u});
    PresentationController countController(countBackend);
    const PresentationFrame countFrame = countController.beginFrame(1280, 720);
    const PresentationCompleteResult countFailure =
        countController.presentFrame(countFrame);
    require(countFailure.failure == PresentationFailure::BackendFrameCountInvalid,
            "generated-frame counts must be smaller than total displayed-frame counts");
}

} // namespace

int main() {
    testNativeLifecycleAndIdentityContract();
    testResizeInvalidationAndGeneratedFrameStatistics();
    testExplicitBackendFailures();
    std::printf("[presentation_controller_test] PASS\n");
    return EXIT_SUCCESS;
}
