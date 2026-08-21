#include "app/validation/ValidationRunController.h"

#include <cstdlib>
#include <iomanip>
#include <limits>
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
    m_rtgiQualityProfile.reset();
    m_currentFrame.reset();
    m_phase = Phase::Inactive;
    m_error = ValidationRunError::None;
    m_detail.clear();
    m_completedWarmupFrames = 0u;
    m_completedSampleFrames = 0u;
    m_completedSamplesPending = 0u;
    m_completedRenderAttempts = 0u;
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
    if (!options.validationRtgiQualityProfile.empty()) {
        RtgiQualityProfile profile;
        std::string detail;
        const std::filesystem::path profilePath =
            options.validationScenePath.parent_path().parent_path() / "rtgi_quality_profiles.json";
        const RtgiQualityProfileError profileError =
            loadRtgiQualityProfile(profilePath, options.validationRtgiQualityProfile, profile, detail);
        if (profileError != RtgiQualityProfileError::None || profile.sceneContractId != m_sceneContract.id ||
            profile.captureWidth != options.validationWidth || profile.captureHeight != options.validationHeight ||
            profile.cameraTimeSeconds > m_cameraPath.durationSeconds) {
            m_error = ValidationRunError::SceneContractLoadFailed;
            m_detail = profileError != RtgiQualityProfileError::None
                           ? std::string(rtgiQualityProfileErrorStableId(profileError)) + ":" + detail
                           : "RtgiQualityProfileMismatch:" + profile.id;
            m_phase = Phase::Failed;
            return false;
        }
        ValidationRenderSettingsIdentity identity;
        identity.id = profile.renderSettingsId;
        identity.version = m_sceneContract.scene == ValidationScene::Voxel ? kValidationRtgiVoxelRenderSettingsVersion
                                                                           : kValidationRtgiModelRenderSettingsVersion;
        m_renderSettingsProfile = makeValidationRenderSettingsProfile(m_sceneContract.scene, identity);
        if (m_renderSettingsProfile.id != profile.renderSettingsId) {
            m_error = ValidationRunError::SceneContractLoadFailed;
            m_detail = "RtgiQualityRenderSettingsMismatch:" + profile.renderSettingsId;
            m_phase = Phase::Failed;
            return false;
        }
        m_rtgiQualityProfile = std::move(profile);
    }
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

ValidationRenderClock ValidationRunController::currentRenderClock() const {
    return {m_completedRenderAttempts, kValidationFrameDeltaSeconds,
            static_cast<double>(m_completedRenderAttempts) * static_cast<double>(kValidationFrameDeltaSeconds)};
}

bool ValidationRunController::completeRenderAttempt() {
    if (m_phase != Phase::Running || !m_currentFrame.has_value()) {
        fail(ValidationRunError::InvalidState, "render attempt completion requires an active validation frame");
        return false;
    }
    if (m_completedRenderAttempts == std::numeric_limits<uint32_t>::max()) {
        fail(ValidationRunError::InvalidState, "validation render attempt clock overflow");
        return false;
    }
    ++m_completedRenderAttempts;
    return true;
}

bool ValidationRunController::completeFrame(const bool captureSucceeded, std::string captureDetail) {
    if (m_phase != Phase::Running || !m_currentFrame.has_value()) {
        fail(ValidationRunError::InvalidState, "validation frame completion requires an active frame");
        return false;
    }
    if ((m_currentFrame->captureAfterRender || m_currentFrame->captureRtgiHdrAfterRender) && !captureSucceeded) {
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

const std::optional<RtgiQualityProfile>& ValidationRunController::rtgiQualityProfile() const {
    return m_rtgiQualityProfile;
}

RenderSettings ValidationRunController::runtimeRenderSettings() const {
    RenderSettings settings = m_renderSettingsProfile.settings;
    if (m_options.validationRtgiCutoutTraversal.has_value()) {
        settings.rtgi.cutoutTraversal = *m_options.validationRtgiCutoutTraversal;
    }
    if (m_options.validationRtgiReference) {
        settings.nrd.enabled = false;
        settings.rtgi.referenceSamplingEnabled = true;
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

    const bool hasRtgiQualityProfile = m_rtgiQualityProfile.has_value();
    double cameraTimeSeconds = hasRtgiQualityProfile ? m_rtgiQualityProfile->cameraTimeSeconds : 0.0;
    if (m_completedWarmupFrames == m_options.validationWarmupFrames) {
        frame.collectPerformance = true;
        if (!hasRtgiQualityProfile) {
            const double sampleDenominator = static_cast<double>(m_options.validationSampleFrames - 1u);
            cameraTimeSeconds =
                m_cameraPath.durationSeconds * static_cast<double>(m_completedSampleFrames) / sampleDenominator;
        }
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
            if (hasRtgiQualityProfile && !m_options.validationRtgiReference &&
                m_completedSampleFrames + 1u == m_options.validationSampleFrames) {
                frame.rtgiLeakageNormalCapturePath =
                    m_options.validationRtgiHdrCaptureDirectory / "rtgi_leakage_normal.exr";
                frame.rtgiLeakageViewZCapturePath =
                    m_options.validationRtgiHdrCaptureDirectory / "rtgi_leakage_viewz.exr";
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
