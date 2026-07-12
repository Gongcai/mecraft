#include "LoadingAppState.h"

#include "GameplayAppState.h"
#include "MainMenuAppState.h"
#include "../../Diagnostics.h"
#include "../../game/Game.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiResources.h"

#include <algorithm>
#include <cstdio>
#include <iostream>

namespace {

bool beginLoadingPass(RhiDevice& rhiDevice,
                      RhiCommandListPool& commandListPool,
                      const Window& window,
                      UIRenderer& uiRenderer,
                      RhiCommandList*& commandList) {
    const int width = std::max(1, window.getWidth());
    const int height = std::max(1, window.getHeight());
    if (!rhiDevice.resizeSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        return false;
    }

    const RhiTextureViewHandle colorView = rhiDevice.currentSwapchainColorView();
    if (!colorView.isValid()) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = colorView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.03f;
    colorAttachment.clearColor[1] = 0.04f;
    colorAttachment.clearColor[2] = 0.05f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "LoadingScreen";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;

    commandList = commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr ||
        !commandList->begin({"LoadingScreen.Commands", RhiCommandListType::Graphics})) {
        return false;
    }
    commandList->textureBarrier({
        rhiDevice.currentSwapchainColorTexture(),
        RhiResourceState::Present,
        RhiResourceState::RenderTarget
    });
    if (!uiRenderer.prepareTextFrame(*commandList)) {
        return false;
    }
    commandList->beginRendering(renderingInfo);
    return true;
}

void endLoadingPass(RhiDevice& rhiDevice,
                    RhiCommandList*& commandList) {
    if (commandList == nullptr) {
        return;
    }

    commandList->endRendering();
    commandList->textureBarrier({
        rhiDevice.currentSwapchainColorTexture(),
        RhiResourceState::RenderTarget,
        RhiResourceState::Present
    });
    if (!commandList->end()) {
        std::abort();
    }
    RhiCommandList* submittedCommandLists[] = {commandList};
    if (!rhiDevice.submit({"LoadingScreen.Submit", submittedCommandLists, 1u})) {
        std::abort();
    }
    commandList = nullptr;
}

} // namespace

LoadingAppState::LoadingAppState(AppStateDependencies deps, GameSessionConfig config)
    : m_deps(deps), m_config(std::move(config)) {
}

LoadingAppState::~LoadingAppState() {
    if (m_game) {
        m_game->shutdown();
    }
}

void LoadingAppState::onEnter() {
    m_deps.contextManager.pushContext(InputContextType::UI);
    m_deps.input.captureMouse(false);

    m_screen.setLocaleManager(&m_deps.localeManager);
    m_screen.init(m_deps.resourceMgr);
    m_screen.enterScene();
    m_deps.uiRenderer.setActiveScene(&m_screen);
    refreshScreen();
}

void LoadingAppState::onExit() {
    m_deps.uiRenderer.setActiveScene(nullptr);
    m_screen.exitScene();
    m_screen.shutdown();
    m_deps.contextManager.popContext();
}

void LoadingAppState::update(const double frameTime, double& accumulator) {
    m_deps.input.update();
    m_screen.updateAnimations(static_cast<float>(frameTime));

    if (!m_firstFrameRendered) {
        accumulator = 0.0;
        return;
    }

    if (!m_game) {
        m_game = createGame();
        m_game->beginLoading();
    }

    if (!m_game->isLoadingComplete() && !m_game->updateLoading(static_cast<float>(frameTime))) {
        MECRAFT_LOG_STREAM(std::cerr << "[LoadingAppState] Failed to load gameplay\n");
        if (m_game) {
            m_game->shutdown();
            m_game.reset();
        }
        m_failed = true;
        m_deps.appFsm.changeState(std::make_unique<MainMenuAppState>(m_deps));
        accumulator = 0.0;
        return;
    }

    refreshScreen();

    if (m_game && m_game->isLoadingComplete()) {
        m_deps.appFsm.changeState(
            std::make_unique<GameplayAppState>(m_deps, std::move(m_game)));
        accumulator = 0.0;
        return;
    }

    accumulator = 0.0;
}

void LoadingAppState::render(const double frameTime) {
    (void)frameTime;
    UIRenderContext sceneContext =
        m_deps.uiRenderer.prepareSceneContext(m_deps.window, m_deps.rhiDevice, m_deps.input.snapshot());

    RhiCommandList* commandList = nullptr;
    if (!beginLoadingPass(m_deps.rhiDevice, m_deps.commandListPool,
                          m_deps.window, m_deps.uiRenderer, commandList)) {
        MECRAFT_LOG_STREAM(std::cerr << "[LoadingAppState] Failed to begin RHI loading pass\n");
        return;
    }

    sceneContext.commandList = commandList;
    m_deps.uiRenderer.renderSceneOnlyPrepared(sceneContext);
    endLoadingPass(m_deps.rhiDevice, commandList);
    m_deps.rhiDevice.present();
    m_firstFrameRendered = true;
}

std::unique_ptr<Game> LoadingAppState::createGame() const {
    GameSessionDependencies deps{
        m_deps.window,
        m_deps.input,
        m_deps.actionMap,
        m_deps.contextManager,
        m_deps.resourceMgr,
        m_deps.audioEngine,
        m_deps.bgmSystem,
        m_deps.uiRenderer,
        m_deps.localeManager,
        m_deps.threadPool,
        m_deps.rhiDevice,
        m_deps.commandListPool,
        m_deps.enableDebugDashboard
    };
    return std::make_unique<Game>(m_config, deps);
}

void LoadingAppState::refreshScreen() {
    if (!m_game) {
        m_screen.setProgress(0.0f);
        m_screen.setStatusText("Preparing world");
        m_screen.setDetailText("");
        return;
    }

    const Game::LoadProgress progress = m_game->getLoadProgress();
    m_screen.setProgress(progress.progress);
    m_screen.setStatusText(progress.label);

    if (progress.targetChunks > 0) {
        char detail[96];
        std::snprintf(detail, sizeof(detail), "%d / %d chunks ready",
                      progress.loadedChunks,
                      progress.targetChunks);
        m_screen.setDetailText(detail);
    } else if (m_failed) {
        m_screen.setDetailText("Returning to menu");
    } else {
        m_screen.setDetailText("");
    }
}
