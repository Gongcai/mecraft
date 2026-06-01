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
    const auto stateStart = std::chrono::steady_clock::now();
#endif

    accumulator -= fixedStep;

    // ECS pre-state stage: sample input and build intents before states consume them.
    session.gameplayScene().runFixedUpdate(static_cast<float>(fixedStep));

    session.gameplayScene().tickClock().advance(fixedStep);
    uint32_t ticksThisFrame = 0;
    while (session.gameplayScene().tickClock().shouldTick()
           && ticksThisFrame < session.gameplayScene().tickClock().maxTicksPerFrame()) {
        session.gameplayScene().runOneTick();
        session.gameplayScene().tickClock().consumeTick();
        ++ticksThisFrame;
    }

    session.updateWorldAroundLocalPlayer();

    session.stateMachine().update(static_cast<float>(fixedStep), inputSnapshot);
#ifdef MECRAFT_DEBUG
    const auto stateEnd = std::chrono::steady_clock::now();
    if (renderRuntime) {
        if (auto* profiler = renderRuntime->profiler()) {
            profiler->recordFixedInput(std::chrono::duration<double, std::milli>(inputEnd - inputStart).count());
            profiler->recordFixedState(std::chrono::duration<double, std::milli>(stateEnd - stateStart).count());
            profiler->recordFixedParticle(0.0);
            profiler->recordFixedDrop(0.0);
            profiler->recordFixedWorld(0.0);
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
        session.world(),
        snap.renderCamera,
        window,
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
}
