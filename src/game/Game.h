//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_GAME_H
#define MECRAFT_GAME_H
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "app/validation/ValidationSceneContract.h"
#include "engine/camera/Camera.h"
#include "renderer/capture/TextureCapture.h"
#include "renderer/contracts/CameraPathContract.h"
#include "renderer/core/FrameContext.h"
#include "session/GameSessionConfig.h"
#include "session/GameSession.h"

class GameplayHudPresenter;
class AudioListenerSyncSystem;
class GameFrameOrchestrator;
class GameplayRenderRuntime;
struct GpuFrameStats;
struct RenderGraphFrameStats;

class Game {
public:
    enum class LoadPhase { NotStarted, Session, RenderRuntime, Ecs, InitialChunks, Complete, Failed };

    struct LoadProgress {
        LoadPhase phase = LoadPhase::NotStarted;
        float progress = 0.0f;
        std::string label;
        int loadedChunks = 0;
        int targetChunks = 0;
        int inFlightChunks = 0;
        bool complete = false;
    };

    /// Constructor using structured config and dependencies.
    Game(GameSessionConfig config, GameSessionDependencies deps);
    ~Game();

    [[nodiscard]] bool init();
    void beginLoading();
    [[nodiscard]] bool updateLoading(float deltaTime);
    void shutdown();

    [[nodiscard]] bool fixedUpdate(double fixedStep, double& accumulator);
    [[nodiscard]] bool updateFrame(float deltaTime);
    void setFixedInterpolationAlpha(float alpha);
    [[nodiscard]] bool renderFrame(float frameTime);

    /// Returns the latest completed renderer GPU timing frame.
    /// @return Non-owning statistics pointer, or null before renderer initialization.
    [[nodiscard]] const GpuFrameStats* gpuFrameStats() const;

    /// Returns the latest primary Render Graph CPU/GPU timing snapshot.
    /// @return Value snapshot, or an invalid snapshot before renderer initialization.
    [[nodiscard]] RenderGraphFrameStats renderGraphFrameStats() const;
#ifdef MECRAFT_DEBUG
    void publishDebugStats(float frameTime);
    void recordPollEvents(double ms, unsigned keyEvents, unsigned mouseButtonEvents, unsigned cursorPosEvents,
                          unsigned scrollEvents, unsigned charEvents, double inputCallbackMs,
                          double cursorPosCallbackMs, double imguiCallbackMs, double imguiCursorPosCallbackMs,
                          double imguiCursorPosBackendMs, double imguiWndProcMs, double imguiWndProcSlowestMs,
                          unsigned imguiWndProcSlowestMsg, unsigned imguiWndProcCount);
    void recordAppUpdateDispatch(double ms);
    void recordAppRenderDispatch(double ms);
#endif

    [[nodiscard]] bool isQuitToMenuRequested() const;
    [[nodiscard]] bool isInitialized() const { return m_initialized; }
    [[nodiscard]] bool isLoadingComplete() const { return m_loadPhase == LoadPhase::Complete; }
    [[nodiscard]] LoadProgress getLoadProgress() const;
    void clearQuitToMenuRequest();

    /// Capture a screenshot of the current framebuffer for save thumbnail.
    void captureExitScreenshot();

    /// Schedules save-thumbnail capture during the next normal gameplay frame.
    void requestExitScreenshot();

    /// Configures the exact camera, clock, and optional PNG request for one frame.
    /// @param pose Validated Camera Path pose to apply to world rendering.
    /// @param clock Fixed validation frame clock used by temporal rendering.
    /// @param capturePath PNG destination for the final frame, or null for no capture.
    /// @return True when the double-precision pose converts to a valid render camera.
    [[nodiscard]] bool configureValidationFrame(const renderer::contracts::CameraPathPose& pose,
                                                const RenderFrameClock& clock,
                                                const std::filesystem::path* capturePath);

    /// Returns and clears the most recent validation capture result.
    /// @return Capture status after a requested frame, or no value otherwise.
    [[nodiscard]] std::optional<renderer::capture::TextureCaptureResult> takeValidationCaptureResult();

    /// Applies the verified fixture and freezes authoritative world state before rendering begins.
    /// @param contract Complete voxel scene identity, environment, and optional fixture recipe.
    /// @return True when the local non-persistent world accepted every deterministic input.
    [[nodiscard]] bool prepareValidationScene(const app::validation::ValidationSceneContract& contract);

    /// Returns the latest validation preparation or synchronization failure detail.
    /// @return Empty text before a failure, otherwise one stable structured detail.
    [[nodiscard]] const std::string& validationSceneError() const { return m_validationSceneError; }

    /// Reports whether every terrain mesh required by the frozen world is resident.
    /// @return True after all dirty, executing, and deferred meshing work has drained.
    [[nodiscard]] bool isValidationSceneReady() const;

private:
    // G1: Structured config and dependencies
    GameSessionConfig m_config;
    GameSessionDependencies m_deps;

    // G4: Game session (owns World, ECS, physics, crafting, particles, camera, presentation)
    GameSession m_session;

    // Render runtime (owns RenderResourceHub, RenderScene, entity renderers, post-process)
    std::unique_ptr<GameplayRenderRuntime> m_renderRuntime;

    // G3: Audio and HUD systems
    std::unique_ptr<GameplayHudPresenter> m_hudPresenter;
    std::unique_ptr<AudioListenerSyncSystem> m_audioSyncSystem;

    // G5: Frame orchestrator
    std::unique_ptr<GameFrameOrchestrator> m_frameOrchestrator;

    bool m_initialized = false;
    bool m_captureScreenshotOnNextFrame = false;
    std::optional<Camera> m_validationCamera;
    std::optional<RenderFrameClock> m_validationFrameClock;
    std::optional<std::filesystem::path> m_validationCapturePath;
    std::optional<renderer::capture::TextureCaptureResult> m_validationCaptureResult;
    std::optional<app::validation::ValidationVoxelFixtureIdentity> m_validationVoxelFixture;
    std::string m_validationSceneError;
    bool m_validationWorldSynchronizationPending = false;
    float m_fixedInterpolationAlpha = 0.0f;
    LoadPhase m_loadPhase = LoadPhase::NotStarted;
};

#endif //MECRAFT_GAME_H
