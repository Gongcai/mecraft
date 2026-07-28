#ifndef MECRAFT_LOADINGAPPSTATE_H
#define MECRAFT_LOADINGAPPSTATE_H

#include "IAppState.h"
#include "AppStateDependencies.h"
#include "../../game/session/GameSessionConfig.h"
#include "../../ui/screens/LoadingScreen.h"

#include <memory>

class Game;

class LoadingAppState : public IAppState {
public:
    LoadingAppState(AppStateDependencies deps, GameSessionConfig config);
    ~LoadingAppState() override;

    void onEnter() override;
    void onExit() override;
    void update(double frameTime, double& accumulator) override;
    void render(double frameTime) override;
    [[nodiscard]] const GpuFrameStats* gpuFrameStats() const override {
        return nullptr;
    }

private:
    [[nodiscard]] std::unique_ptr<Game> createGame() const;
    void refreshScreen();

    AppStateDependencies m_deps;
    GameSessionConfig m_config;
    LoadingScreen m_screen;
    std::unique_ptr<Game> m_game;
    bool m_firstFrameRendered = false;
    bool m_failed = false;
};

#endif // MECRAFT_LOADINGAPPSTATE_H
