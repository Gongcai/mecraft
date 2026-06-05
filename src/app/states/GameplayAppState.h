#ifndef MECRAFT_GAMEPLAYAPPSTATE_H
#define MECRAFT_GAMEPLAYAPPSTATE_H

#include "IAppState.h"
#include "AppStateDependencies.h"
#include "../../game/session/GameSessionConfig.h"
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

private:
    AppStateDependencies m_deps;
    GameSessionConfig m_config;
    std::unique_ptr<Game> m_game;
    bool m_enterFailed = false;
};

#endif // MECRAFT_GAMEPLAYAPPSTATE_H
