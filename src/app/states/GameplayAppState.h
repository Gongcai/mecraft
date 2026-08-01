#ifndef MECRAFT_GAMEPLAYAPPSTATE_H
#define MECRAFT_GAMEPLAYAPPSTATE_H

#include "IAppState.h"
#include "AppStateDependencies.h"
#include "../../game/session/GameSessionConfig.h"
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

class Game;

class GameplayAppState : public IAppState {
public:
    explicit GameplayAppState(AppStateDependencies deps, GameSessionConfig config = {});
    GameplayAppState(AppStateDependencies deps, std::unique_ptr<Game> game);
    ~GameplayAppState() override;

    void onEnter() override;
    void onExit() override;
    void update(double frameTime, double& accumulator) override;
    void render(double frameTime) override;
    [[nodiscard]] const GpuFrameStats* gpuFrameStats() const override;
#ifdef MECRAFT_DEBUG
    void recordPollEvents(double ms, unsigned keyEvents, unsigned mouseButtonEvents, unsigned cursorPosEvents,
                          unsigned scrollEvents, unsigned charEvents, double inputCallbackMs,
                          double cursorPosCallbackMs, double imguiCallbackMs, double imguiCursorPosCallbackMs,
                          double imguiCursorPosBackendMs, double imguiWndProcMs, double imguiWndProcSlowestMs,
                          unsigned imguiWndProcSlowestMsg, unsigned imguiWndProcCount) override;
    void recordAppUpdateDispatch(double ms) override;
    void recordAppRenderDispatch(double ms) override;
#endif

private:
    [[nodiscard]] bool beginValidation();

    AppStateDependencies m_deps;
    GameSessionConfig m_config;
    std::unique_ptr<Game> m_game;
    bool m_enterFailed = false;
    bool m_quitToMenuPending = false;
    bool m_closeAppAfterExitScreenshot = false;
    bool m_validationActive = false;
    bool m_validationSceneReady = false;
    uint32_t m_validationSequenceFrame = std::numeric_limits<uint32_t>::max();
    double m_previousTimeSpeed = 1.0;
};

#endif // MECRAFT_GAMEPLAYAPPSTATE_H
