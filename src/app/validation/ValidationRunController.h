#ifndef MECRAFT_VALIDATION_RUN_CONTROLLER_H
#define MECRAFT_VALIDATION_RUN_CONTROLLER_H

#include "app/AppLaunchOptions.h"
#include "app/validation/ValidationSceneContract.h"
#include "renderer/contracts/CameraPathContract.h"

#include <cstdint>
#include <optional>
#include <string>

namespace app::validation {

inline constexpr float kValidationFrameDeltaSeconds = 1.0f / 60.0f;
/// Identifies every deterministic validation-run lifecycle failure.
enum class ValidationRunError : uint8_t {
    None,
    SceneContractLoadFailed,
    SceneMismatch,
    SceneInitializationFailed,
    InvalidState,
    CameraSamplingFailed,
    CameraPoseConversionFailed,
    RenderFailed,
    CaptureFailed
};

/// Describes the exact camera and clock used for one validation render frame.
struct ValidationFrame {
    renderer::contracts::CameraPathPose cameraPose;
    uint32_t sequenceFrameIndex = 0u;
    float deltaTimeSeconds = kValidationFrameDeltaSeconds;
    double renderTimeSeconds = 0.0;
    bool collectPerformance = false;
    bool captureAfterRender = false;
};

/// Drives one versioned Camera Path through fixed warmup and sample phases.
class ValidationRunController final {
public:
    /// Loads and validates the configured Camera Path before renderer startup.
    /// @param options Complete application launch configuration.
    /// @return True when validation is disabled or configuration succeeds.
    [[nodiscard]] bool configure(const AppLaunchOptions& options);

    /// Starts frame generation after the requested scene finishes loading.
    /// @param scene Runtime scene class that is ready to render.
    /// @return True when the scene matches the configured validation run.
    [[nodiscard]] bool beginScene(ValidationScene scene);

    /// Returns the immutable frame that both runtime scene classes must render.
    /// @return Current frame, or null before scene startup and after termination.
    [[nodiscard]] const ValidationFrame* currentFrame() const;

    /// Commits one successfully rendered frame and advances the fixed timeline.
    /// @param captureSucceeded True when a requested final capture was written.
    /// @param captureDetail Capture failure detail when captureSucceeded is false.
    /// @return True when the frame was accepted and the next state was built.
    [[nodiscard]] bool completeFrame(bool captureSucceeded, std::string captureDetail = {});

    /// Terminates the run with a stable runtime error.
    /// @param error Non-success error describing the failed operation.
    /// @param detail Field, resource, or renderer detail for diagnostics.
    void fail(ValidationRunError error, std::string detail);

    /// Consumes one completed performance sample for app-level statistics.
    /// @return True once for every successfully rendered sample frame.
    [[nodiscard]] bool consumeCompletedSampleFrame();

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] bool running() const;
    [[nodiscard]] bool complete() const;
    [[nodiscard]] bool failed() const;
    [[nodiscard]] ValidationScene scene() const;
    [[nodiscard]] ValidationRunError error() const;
    [[nodiscard]] const std::string& detail() const;
    [[nodiscard]] const renderer::contracts::CameraPath& cameraPath() const;
    [[nodiscard]] const ValidationSceneContract& sceneContract() const;
    [[nodiscard]] const ValidationRenderSettingsProfile& renderSettingsProfile() const;
    [[nodiscard]] const AppLaunchOptions& options() const;
    [[nodiscard]] uint32_t completedWarmupFrames() const;
    [[nodiscard]] uint32_t completedSampleFrames() const;

private:
    enum class Phase : uint8_t { Inactive, Ready, Running, Complete, Failed };

    [[nodiscard]] bool buildCurrentFrame();

    AppLaunchOptions m_options;
    ValidationSceneContract m_sceneContract;
    renderer::contracts::CameraPath m_cameraPath;
    ValidationRenderSettingsProfile m_renderSettingsProfile;
    std::optional<ValidationFrame> m_currentFrame;
    Phase m_phase = Phase::Inactive;
    ValidationRunError m_error = ValidationRunError::None;
    std::string m_detail;
    uint32_t m_completedWarmupFrames = 0u;
    uint32_t m_completedSampleFrames = 0u;
    uint32_t m_completedSamplesPending = 0u;
};

/// Returns the stable identifier used by logs and validation reports.
/// @param error Validation-run error to identify.
/// @return Process-lifetime string containing the stable error identifier.
[[nodiscard]] const char* validationRunErrorStableId(ValidationRunError error);

} // namespace app::validation

#endif // MECRAFT_VALIDATION_RUN_CONTROLLER_H
