#include "GameFrameOrchestrator.h"
#include "../session/GameSession.h"
#include "../states/GameStateMachine.h"
#include "../presentation/GameplayPresentationBuilder.h"
#include "../presentation/GameplayPresentationSnapshot.h"
#include "../../renderer/core/RenderResourceHub.h"
#include "../../renderer/core/RenderScene.h"
#include "../../renderer/renderers/FirstPersonHeldItemRenderer.h"
#include "../../renderer/renderers/PostProcessRenderer.h"
#include "../presentation/GameplayHudPresenter.h"
#include "../audio/AudioListenerSyncSystem.h"
#include "../../engine/platform/Window.h"
#include "../../ecs/GameplayScene.h"
#include "../../world/World.h"
#include "../../player/Inventory.h"
#include "../../renderer/overlays/BlockInteractionOverlayRenderer.h"
#include "../camera/CameraController.h"

bool GameFrameOrchestrator::runFixedUpdate(GameSession& session,
                                            GameStateMachine& stateMachine,
                                            double fixedStep,
                                            double& accumulator) {
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

    // Note: InputManager::update() and StateMachine::update() are called by Game before this
    // because they depend on Game-specific input snapshot.

    session.updateWorldAroundLocalPlayer();

    return stateMachine.isQuitToMenuRequested();
}

void GameFrameOrchestrator::syncAudioListener(AudioListenerSyncSystem& audioSync,
                                                float deltaTime,
                                                ecs::GameplayRegistry& reg) {
    audioSync.update(deltaTime, reg);
}

void GameFrameOrchestrator::renderFrame(GameSession& session,
                                         RenderResourceHub& renderer,
                                         RenderScene& renderScene,
                                         FirstPersonHeldItemRenderer& firstPersonHeldItemRenderer,
                                         GameStateMachine& stateMachine,
                                         PostProcessRenderer& postProcess,
                                         GameplayHudPresenter* hudPresenter,
                                         Window& window,
                                         float frameTime) {
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
        postProcess,
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
        hudPresenter->render(snap, stateMachine);
#ifdef MECRAFT_DEBUG
        // G7: Render debug dashboard (Dashboard is injected into presenter by Game)
        if (m_debugProfilerStats) {
            hudPresenter->renderDashboard(reg, session.world(), snap.renderCamera,
                                          renderer, renderScene, postProcess, *m_debugProfilerStats);
        }
#endif
    }
    window.swapBuffers();
}
