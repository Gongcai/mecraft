#include "GameFrameOrchestrator.h"
#include "../session/GameSession.h"
#include "../states/GameStateMachine.h"
#include "../presentation/GameplayPresentationBuilder.h"
#include "../presentation/GameplayPresentationSnapshot.h"
#include "../../renderer/core/RenderResourceHub.h"
#include "../../renderer/core/RenderScene.h"
#include "../../renderer/renderers/PostProcessRenderer.h"
#include "../../particle/RainRenderer.h"
#include "../presentation/GameplayHudPresenter.h"
#include "../audio/AudioListenerSyncSystem.h"
#include "../../engine/input/InputManager.h"
#include "../../engine/camera/Camera.h"
#include "../../ui/core/UIRenderer.h"
#include "../../engine/platform/Window.h"
#include "../../ecs/GameplayScene.h"
#include "../../ecs/util/PlayerQuery.h"
#include "../../world/World.h"
#include "../../world/WeatherSystem.h"
#include "../../player/Inventory.h"
#include "../../renderer/overlays/BlockInteractionOverlayRenderer.h"
#include "../../ui/core/UIRenderContext.h"
#include "../camera/CameraController.h"

#include <glad/glad.h>
#include <algorithm>

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

    ecs::PlayerQuery query(session.gameplayScene().registry());
    session.world().update(query.getPosition());

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
                                         GameStateMachine& stateMachine,
                                         PostProcessRenderer& postProcess,
                                         GameplayHudPresenter* hudPresenter,
                                         InputManager& input,
                                         UIRenderer& uiRenderer,
                                         Window& window,
                                         float frameTime) {
    // Activate new pipeline on first frame (after render targets are ready)
    if (!renderScene.isNewPipelineActive() && renderScene.isNewPipelineReady()) {
        renderScene.setNewPipelineActive(true);
    }

    // Build presentation snapshot from ECS (single point of ECS access)
    auto& reg = session.gameplayScene().registry();
    const auto snap = session.presentationBuilder().build(reg, session.cameraController());

    // Apply snapshot state to RenderScene
    renderScene.setRenderLocalPlayerModel(snap.renderLocalPlayerModel);
    renderScene.setHeldBlockLightValue(snap.heldBlockLightLevel);
    renderScene.setEyeInWater(snap.eyeInWater);

    // Forward vanilla renders directly to backbuffer.
    const bool skipPostProcess = renderScene.getPipelineMode() == PipelineMode::Forward;
    if (!skipPostProcess) {
        postProcess.beginScene(window);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, std::max(1, window.getWidth()), std::max(1, window.getHeight()));
    }

    const bool lightDebugActive = renderScene.isLightDebugActive();
    float cameraRainVisibility = 1.0f;

    // Convert snapshot types to renderer types
    BlockTargetRenderData targetData;
    targetData.hasTarget = snap.blockTarget.hasTarget;
    targetData.targetBlock = snap.blockTarget.targetBlock;
    BlockBreakRenderData breakData;
    breakData.active = snap.blockBreak.active;
    breakData.progress01 = snap.blockBreak.progress01;
    breakData.blockPos = snap.blockBreak.blockPos;

    // Use new pipeline path (RenderScene handles all rendering)
    renderScene.renderFrame(session.world(), snap.renderCamera, window, targetData, breakData);

    if (!lightDebugActive) {
        cameraRainVisibility = renderScene.computeCameraRainVisibility(session.world(), snap.renderCamera.getPosition());
        // Precipitation rendering
        const auto& settings = renderScene.getSettings();
        if (settings.weather.rainLinesEnabled) {
            const auto& weather = session.world().getWeatherSystem().getDerived();
            const glm::vec3 camPos = snap.renderCamera.getPosition();
            auto projMat = snap.renderCamera.getProjectionMatrix(window.getAspectRatio());
            auto viewMat = snap.renderCamera.getViewMatrix();
            float alphaScale = settings.weather.rainAlphaScale;
            const bool forwardVanillaActive = renderScene.isNewPipelineActive() &&
                                              renderScene.getPipelineMode() == PipelineMode::Forward;
            const auto& frameOutput = renderScene.getLastFrameOutput();
            const GLuint depthTex = forwardVanillaActive ? 0 : frameOutput.gbufferDepthTex;
            const bool hardwareDepthTest = !renderScene.isNewPipelineActive() || forwardVanillaActive;
            const glm::vec2 precipitationScreenSize(
                static_cast<float>(std::max(1, window.getWidth())),
                static_cast<float>(std::max(1, window.getHeight())));

            if (weather.rainStrength > 0.01f) {
                session.rainRenderer().render(projMat, viewMat, camPos,
                                    weather.rainStrength, cameraRainVisibility,
                                    alphaScale, depthTex,
                                    precipitationScreenSize, frameTime,
                                    hardwareDepthTest);
            }
            if (weather.snowStrength > 0.01f) {
                session.rainRenderer().renderSnow(projMat, viewMat, camPos,
                                        weather.snowStrength, cameraRainVisibility,
                                        alphaScale * 0.6f, depthTex,
                                        precipitationScreenSize, frameTime,
                                        hardwareDepthTest);
            }
        }
    }

    // Build post-process effects via RenderScene
    if (!skipPostProcess) {
        PostProcessEffects effects = renderScene.buildPostProcessEffects(
            session.world(), snap.renderCamera, window, cameraRainVisibility, snap.fallRollRadians);
        postProcess.setEffects(effects);
    }

    // Convert held item motion to renderer type
    HeldItemPreviewMotion heldItemMotion;
    heldItemMotion.moving = snap.heldItemMotion.moving;
    heldItemMotion.sprinting = snap.heldItemMotion.sprinting;
    heldItemMotion.bobFrequency = snap.heldItemMotion.bobFrequency;
    heldItemMotion.bobPhaseOffset = snap.heldItemMotion.bobPhaseOffset;
    heldItemMotion.cameraYawDegrees = snap.heldItemMotion.cameraYawDegrees;
    heldItemMotion.cameraPitchDegrees = snap.heldItemMotion.cameraPitchDegrees;

    // Note: renderHeldItem is still in Game because it depends on FirstPersonHeldItemRenderer
    // which is a Game-level resource. This will be moved when GameSession owns all renderers.

    // UI rendering
    PlayerStatsData playerStats;
    playerStats.health = snap.playerStats.health;
    playerStats.maxHealth = snap.playerStats.maxHealth;
    playerStats.armor = snap.playerStats.armor;
    playerStats.maxArmor = snap.playerStats.maxArmor;
    playerStats.food = snap.playerStats.food;
    playerStats.maxFood = snap.playerStats.maxFood;
    playerStats.showSurvivalStats = snap.playerStats.showSurvivalStats;

    uiRenderer.render(window, *snap.inventory, playerStats, heldItemMotion, input.snapshot());
    stateMachine.render();
    window.swapBuffers();
}
