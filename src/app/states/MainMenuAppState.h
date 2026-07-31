#ifndef MECRAFT_MAINMENUAPPSTATE_H
#define MECRAFT_MAINMENUAPPSTATE_H

#include "IAppState.h"
#include "AppStateMachine.h"
#include "AppStateDependencies.h"
#include "LoadingAppState.h"
#include "../../game/session/GameSessionConfig.h"
#include "../../ui/screens/MainMenuScreen.h"
#include "../../ui/screens/SaveListScreen.h"
#include "../../ui/screens/CreateWorldScreen.h"
#include "../../ui/core/ScreenTransition.h"
#include "../../ui/core/UIInputAdapter.h"
#include "../../renderer/renderers/SkyboxRenderer.h"
#include <GLFW/glfw3.h>
#include <filesystem>

class MainMenuAppState : public IAppState {
public:
    explicit MainMenuAppState(AppStateDependencies deps);

    void onEnter() override;
    void onExit() override;
    void update(double frameTime, double& accumulator) override;
    void render(double frameTime) override;
    [[nodiscard]] const GpuFrameStats* gpuFrameStats() const override { return nullptr; }

private:
    enum class Page { MainMenu, SaveList, CreateWorld };

    void switchToPage(Page page);
    void startGameWithWorld(const std::string& worldName, int seed, const std::string& displayName = {});

    AppStateDependencies m_deps;

    MainMenuScreen m_mainMenuScreen;
    SaveListScreen m_saveListScreen;
    CreateWorldScreen m_createWorldScreen;

    Page m_currentPage = Page::MainMenu;

    ScreenTransition m_transition;
    GameSessionConfig m_pendingConfig;
    bool m_transitioningToGame = false;

    SkyboxRenderer m_skyboxRenderer;
    float m_skyboxYaw = 0.0f;

    std::filesystem::path m_savesRoot = "saves";
};

#endif // MECRAFT_MAINMENUAPPSTATE_H
