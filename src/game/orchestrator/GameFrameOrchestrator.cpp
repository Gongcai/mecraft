#include "GameFrameOrchestrator.h"
#include "../session/GameSession.h"
#include "../states/GameStateMachine.h"
#include "../../engine/input/InputManager.h"
#ifdef MECRAFT_DEBUG
#include <chrono>
#include "../debug/DebugFrameProfiler.h"
#endif
#include "../presentation/GameplayPresentationBuilder.h"
#include "../presentation/GameplayPresentationSnapshot.h"
#include "../../renderer/core/RenderResourceHub.h"
#include "../../renderer/core/RenderScene.h"
#include "../../renderer/renderers/FirstPersonHeldItemRenderer.h"
#include "../../renderer/passes/PostProcessPass.h"
#include "../render/GameplayRenderRuntime.h"
#include "../presentation/GameplayHudPresenter.h"
#include "../audio/AudioListenerSyncSystem.h"
#include "../../engine/platform/Window.h"
#include "../../ecs/GameplayScene.h"
#include "../../world/World.h"
#include "../../player/Inventory.h"
#include "../../renderer/overlays/BlockInteractionOverlayRenderer.h"
#include "../camera/CameraController.h"

#ifdef MECRAFT_DEBUG
#include <algorithm>
#endif

bool GameFrameOrchestrator::runFixedUpdate(GameSession& session,
                                            InputManager& input,
                                            GameplayRenderRuntime* renderRuntime,
                                            double fixedStep,
                                            double& accumulator) {
#ifdef MECRAFT_DEBUG
    const auto inputStart = std::chrono::steady_clock::now();
#endif
    input.update();
    const InputSnapshot& inputSnapshot = input.snapshot();
#ifdef MECRAFT_DEBUG
    const auto inputEnd = std::chrono::steady_clock::now();
#endif

    accumulator -= fixedStep;

    // ECS pre-state stage: sample input and build intents before states consume them.
#ifdef MECRAFT_DEBUG
    const auto fixedSystemsStart = std::chrono::steady_clock::now();
    const auto fixedProfile = session.gameplayScene().runFixedUpdateProfiled(static_cast<float>(fixedStep));
    const auto fixedSystemsEnd = std::chrono::steady_clock::now();
#else
    session.gameplayScene().runFixedUpdate(static_cast<float>(fixedStep));
#endif

#ifdef MECRAFT_DEBUG
    const auto tickStart = std::chrono::steady_clock::now();
#endif
    session.gameplayScene().tickClock().advance(fixedStep);
    uint32_t ticksThisFrame = 0;
    while (session.gameplayScene().tickClock().shouldTick()
           && ticksThisFrame < session.gameplayScene().tickClock().maxTicksPerFrame()) {
        session.gameplayScene().runOneTick();
        session.gameplayScene().tickClock().consumeTick();
        ++ticksThisFrame;
    }
#ifdef MECRAFT_DEBUG
    const auto tickEnd = std::chrono::steady_clock::now();
    const auto worldStart = std::chrono::steady_clock::now();
#endif

    session.updateWorldAroundLocalPlayer();
#ifdef MECRAFT_DEBUG
    const auto worldEnd = std::chrono::steady_clock::now();
#endif

#ifdef MECRAFT_DEBUG
    const auto stateMachineStart = std::chrono::steady_clock::now();
#endif
    session.stateMachine().update(static_cast<float>(fixedStep), inputSnapshot);
#ifdef MECRAFT_DEBUG
    const auto stateEnd = std::chrono::steady_clock::now();
    if (renderRuntime) {
        if (auto* profiler = renderRuntime->profiler()) {
            profiler->recordFixedInput(std::chrono::duration<double, std::milli>(inputEnd - inputStart).count());
            const double fixedSystemMs = std::chrono::duration<double, std::milli>(fixedSystemsEnd - fixedSystemsStart).count();
            const double categorizedMs = fixedProfile.stateMs() + fixedProfile.dropMs() + fixedProfile.particleMs();
            const double uncategorizedMs = std::max(0.0, fixedSystemMs - categorizedMs);
            const double tickMs = std::chrono::duration<double, std::milli>(tickEnd - tickStart).count();
            const double stateMachineMs = std::chrono::duration<double, std::milli>(stateEnd - stateMachineStart).count();
            profiler->recordFixedState(fixedProfile.stateMs() + uncategorizedMs + stateMachineMs);
            profiler->recordFixedParticle(fixedProfile.particleMs());
            profiler->recordFixedDrop(fixedProfile.dropMs());
            profiler->recordFixedWorld(tickMs + std::chrono::duration<double, std::milli>(worldEnd - worldStart).count());
            profiler->incrementFixedStep();
        }
    }
#endif

    return session.stateMachine().isQuitToMenuRequested();
}

void GameFrameOrchestrator::syncAudioListener(AudioListenerSyncSystem& audioSync,
                                                float deltaTime,
                                                GameSession& session) {
    audioSync.update(deltaTime, session.gameplayScene().registry());
}

void GameFrameOrchestrator::renderFrame(GameSession& session,
                                         GameplayRenderRuntime& renderRuntime,
                                         GameplayHudPresenter* hudPresenter,
                                         Window& window,
                                         float frameTime) {
    // Obtain renderer references from the aggregate
    auto& renderScene = renderRuntime.renderScene();
    auto& firstPersonHeldItemRenderer = renderRuntime.firstPersonHeldItemRenderer();
    auto& postProcess = renderScene.postProcessPass();
    auto& renderer = renderRuntime.resourceHub();

#ifdef MECRAFT_DEBUG
    const auto renderStart = std::chrono::steady_clock::now();
#endif

    // Build presentation snapshot from ECS (single point of ECS access)
    auto& reg = session.gameplayScene().registry();
    const auto snap = session.presentationBuilder().build(reg, session.cameraController());

    // Apply snapshot state to RenderScene
    renderScene.setRenderLocalPlayerModel(snap.renderLocalPlayerModel);
    renderScene.setHeldBlockLightValue(snap.heldBlockLightLevel);
    renderScene.setEyeInWater(snap.eyeInWater);

    // Convert snapshot types to renderer types
    BlockTargetRenderData targetData;
    targetData.hasTarget = snap.blockTarget.hasTarget;
    targetData.targetBlock = snap.blockTarget.targetBlock;
    BlockBreakRenderData breakData;
    breakData.active = snap.blockBreak.active;
    breakData.progress01 = snap.blockBreak.progress01;
    breakData.blockPos = snap.blockBreak.blockPos;

    FirstPersonHeldItemMotion firstPersonMotion;
    firstPersonMotion.moving = snap.heldItemMotion.moving;
    firstPersonMotion.sprinting = snap.heldItemMotion.sprinting;
    firstPersonMotion.bobFrequency = snap.heldItemMotion.bobFrequency;
    firstPersonMotion.bobPhaseOffset = snap.heldItemMotion.bobPhaseOffset;
    firstPersonMotion.cameraYawDegrees = snap.heldItemMotion.cameraYawDegrees;
    firstPersonMotion.cameraPitchDegrees = snap.heldItemMotion.cameraPitchDegrees;
    if (snap.blockBreak.active) {
        firstPersonHeldItemRenderer.setContinuousSwing(true);
    } else {
        firstPersonHeldItemRenderer.setContinuousSwing(false);
        if (snap.heldItemSwingSequence != m_lastHeldItemSwingSequence) {
            firstPersonHeldItemRenderer.triggerSwing();
        }
    }
    m_lastHeldItemSwingSequence = snap.heldItemSwingSequence;

    RenderGameplayFrameRequest renderRequest{
        session.worldView(),
        snap.renderCamera,
        window,
        session.world().getDayNightSystem(),
        session.world().getWeatherSystem(),
        targetData,
        breakData,
        session.rainRenderer(),
        frameTime,
        snap.fallRollRadians,
        &firstPersonHeldItemRenderer,
        snap.inventory,
        &firstPersonMotion,
        snap.inventory != nullptr && !snap.renderLocalPlayerModel
    };
    renderScene.renderGameplayFrame(renderRequest);

    // G3: Delegate UI rendering to GameplayHudPresenter
    if (hudPresenter) {
        hudPresenter->render(snap, session.stateMachine());
#ifdef MECRAFT_DEBUG
        // G7: Render debug dashboard (Dashboard is injected into presenter by Game)
        if (renderRuntime.dashboardProfilerStats()) {
            hudPresenter->renderDashboard(reg, session.world(), snap.renderCamera,
                                          renderer, renderScene, postProcess, *renderRuntime.dashboardProfilerStats());
        }
#endif
    }
    window.swapBuffers();

#ifdef MECRAFT_DEBUG
    const auto renderEnd = std::chrono::steady_clock::now();
    if (auto* profiler = renderRuntime.profiler()) {
        profiler->recordRender(std::chrono::duration<double, std::milli>(renderEnd - renderStart).count());
    }
#endif
}
