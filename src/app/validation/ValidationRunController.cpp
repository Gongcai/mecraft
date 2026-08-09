#include "app/validation/ValidationRunController.h"

#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

namespace app::validation {
namespace {

[[nodiscard]] std::string rtgiHdrCaptureFilename(const char* signal, const uint32_t sampleIndex) {
    std::ostringstream stream;
    stream << "rtgi_" << signal << '_' << std::setw(4) << std::setfill('0') << sampleIndex << ".exr";
    return stream.str();
}

} // namespace

bool ValidationRunController::configure(const AppLaunchOptions& options) {
    m_options = options;
    m_sceneContract = {};
    m_cameraPath = {};
    m_renderSettingsProfile = {};
    m_currentFrame.reset();
    m_phase = Phase::Inactive;
    m_error = ValidationRunError::None;
    m_detail.clear();
    m_completedWarmupFrames = 0u;
    m_completedSampleFrames = 0u;
    m_completedSamplesPending = 0u;
    if (!options.validationEnabled()) {
        return true;
    }

    ValidationSceneContractLoadResult loaded = loadValidationSceneContract(options.validationScenePath);
    if (!loaded.succeeded()) {
        m_error = ValidationRunError::SceneContractLoadFailed;
        m_detail = std::string(validationSceneContractErrorStableId(loaded.error)) + ":" + loaded.detail;
        m_phase = Phase::Failed;
        return false;
    }
    m_sceneContract = std::move(loaded.contract);
    m_cameraPath = std::move(loaded.cameraPath);
    m_renderSettingsProfile =
        makeValidationRenderSettingsProfile(m_sceneContract.scene, m_sceneContract.renderSettings);
    m_phase = Phase::Ready;
    return true;
}

bool ValidationRunController::beginScene(const ValidationScene scene) {
    if (m_phase != Phase::Ready) {
        fail(ValidationRunError::InvalidState, "validation scene startup requires the ready phase");
        return false;
    }
    if (scene != m_sceneContract.scene) {
        fail(ValidationRunError::SceneMismatch,
             std::string(validationSceneStableId(scene)) + "!=" + validationSceneStableId(m_sceneContract.scene));
        return false;
    }
    m_phase = Phase::Running;
    return buildCurrentFrame();
}

const ValidationFrame* ValidationRunController::currentFrame() const {
    return m_currentFrame ? &*m_currentFrame : nullptr;
}

bool ValidationRunController::completeFrame(const bool captureSucceeded, std::string captureDetail) {
    if (m_phase != Phase::Running || !m_currentFrame.has_value()) {
        fail(ValidationRunError::InvalidState, "validation frame completion requires an active frame");
        return false;
    }
    if (m_currentFrame->captureAfterRender && !captureSucceeded) {
        fail(ValidationRunError::CaptureFailed, std::move(captureDetail));
        return false;
    }

    if (m_currentFrame->collectPerformance) {
        ++m_completedSampleFrames;
        ++m_completedSamplesPending;
    } else {
        ++m_completedWarmupFrames;
    }
    m_currentFrame.reset();

    if (m_completedSampleFrames == m_options.validationSampleFrames) {
        m_phase = Phase::Complete;
        return true;
    }
    return buildCurrentFrame();
}

void ValidationRunController::fail(const ValidationRunError error, std::string detail) {
    if (error == ValidationRunError::None) {
        std::abort();
    }
    m_error = error;
    m_detail = std::move(detail);
    m_currentFrame.reset();
    m_phase = Phase::Failed;
}

bool ValidationRunController::consumeCompletedSampleFrame() {
    if (m_completedSamplesPending == 0u) {
        return false;
    }
    --m_completedSamplesPending;
    return true;
}

bool ValidationRunController::enabled() const {
    return m_phase != Phase::Inactive;
}

bool ValidationRunController::running() const {
    return m_phase == Phase::Running;
}

bool ValidationRunController::complete() const {
    return m_phase == Phase::Complete;
}

bool ValidationRunController::failed() const {
    return m_phase == Phase::Failed;
}

ValidationScene ValidationRunController::scene() const {
    return m_sceneContract.scene;
}

ValidationRunError ValidationRunController::error() const {
    return m_error;
}

const std::string& ValidationRunController::detail() const {
    return m_detail;
}

const renderer::contracts::CameraPath& ValidationRunController::cameraPath() const {
    return m_cameraPath;
}

const ValidationSceneContract& ValidationRunController::sceneContract() const {
    return m_sceneContract;
}

const ValidationRenderSettingsProfile& ValidationRunController::renderSettingsProfile() const {
    return m_renderSettingsProfile;
}

RenderSettings ValidationRunController::runtimeRenderSettings() const {
    RenderSettings settings = m_renderSettingsProfile.settings;
    if (m_options.validationRtgiCutoutTraversal.has_value()) {
        settings.rtgi.cutoutTraversal = *m_options.validationRtgiCutoutTraversal;
    }
    return settings;
}

const AppLaunchOptions& ValidationRunController::options() const {
    return m_options;
}

uint32_t ValidationRunController::completedWarmupFrames() const {
    return m_completedWarmupFrames;
}

uint32_t ValidationRunController::completedSampleFrames() const {
    return m_completedSampleFrames;
}

bool ValidationRunController::buildCurrentFrame() {
    if (m_phase != Phase::Running || m_currentFrame.has_value()) {
        fail(ValidationRunError::InvalidState, "validation frame construction requires an empty running phase");
        return false;
    }

    ValidationFrame frame;
    frame.sequenceFrameIndex = m_completedWarmupFrames + m_completedSampleFrames;
    frame.renderTimeSeconds =
        static_cast<double>(frame.sequenceFrameIndex) * static_cast<double>(kValidationFrameDeltaSeconds);

    double cameraTimeSeconds = 0.0;
    if (m_completedWarmupFrames == m_options.validationWarmupFrames) {
        frame.collectPerformance = true;
        const double sampleDenominator = static_cast<double>(m_options.validationSampleFrames - 1u);
        cameraTimeSeconds =
            m_cameraPath.durationSeconds * static_cast<double>(m_completedSampleFrames) / sampleDenominator;
        frame.captureAfterRender = m_completedSampleFrames + 1u == m_options.validationSampleFrames;
        frame.captureRtgiHdrAfterRender = m_options.validationRtgiHdrCaptureMode.has_value();
        frame.rtgiHdrCaptureSampleIndex = m_completedSampleFrames;
        if (frame.captureRtgiHdrAfterRender) {
            const ValidationRtgiHdrCaptureMode mode = *m_options.validationRtgiHdrCaptureMode;
            if (mode == ValidationRtgiHdrCaptureMode::Raw || mode == ValidationRtgiHdrCaptureMode::RawAndDenoised) {
                frame.rtgiRawCapturePath = m_options.validationRtgiHdrCaptureDirectory /
                                           rtgiHdrCaptureFilename("raw", m_completedSampleFrames);
            }
            if (mode == ValidationRtgiHdrCaptureMode::Denoised ||
                mode == ValidationRtgiHdrCaptureMode::RawAndDenoised) {
                frame.nrdDiffuseCapturePath = m_options.validationRtgiHdrCaptureDirectory /
                                              rtgiHdrCaptureFilename("denoised", m_completedSampleFrames);
            }
        }
    }

    const renderer::contracts::CameraPathError sampleError =
        renderer::contracts::sampleCameraPath(m_cameraPath, cameraTimeSeconds, frame.cameraPose);
    if (sampleError != renderer::contracts::CameraPathError::None) {
        fail(ValidationRunError::CameraSamplingFailed, renderer::contracts::cameraPathErrorStableId(sampleError));
        return false;
    }
    m_currentFrame = frame;
    return true;
}

const char* validationRunErrorStableId(const ValidationRunError error) {
    switch (error) {
    case ValidationRunError::None: return "None";
    case ValidationRunError::SceneContractLoadFailed: return "SceneContractLoadFailed";
    case ValidationRunError::SceneMismatch: return "SceneMismatch";
    case ValidationRunError::SceneInitializationFailed: return "SceneInitializationFailed";
    case ValidationRunError::InvalidState: return "InvalidState";
    case ValidationRunError::CameraSamplingFailed: return "CameraSamplingFailed";
    case ValidationRunError::CameraPoseConversionFailed: return "CameraPoseConversionFailed";
    case ValidationRunError::RenderFailed: return "RenderFailed";
    case ValidationRunError::CaptureFailed: return "CaptureFailed";
    }
    std::abort();
}

} // namespace app::validation
