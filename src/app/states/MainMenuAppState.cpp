#include "MainMenuAppState.h"

#include "../AppSettings.h"
#include "../../Diagnostics.h"
#include "../../renderer/rhi/RhiCommandList.h"
#include "../../renderer/rhi/RhiDevice.h"
#include "../../renderer/rhi/RhiResources.h"
#include "../../save/SaveManager.h"
#include "../../engine/platform/Time.h"

#include <algorithm>
#include <iostream>

namespace {

bool beginMenuClearPass(RhiDevice& rhiDevice,
                        RhiCommandListPool& commandListPool,
                        const uint32_t width,
                        const uint32_t height,
                        RhiCommandList*& commandList) {
    const RhiTextureViewHandle colorView = rhiDevice.currentSwapchainColorView();
    const RhiTextureViewHandle depthView = rhiDevice.currentSwapchainDepthStencilView();
    if (!colorView.isValid() || !depthView.isValid()) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = colorView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "MainMenuClear";
    renderingInfo.renderArea = {
        0,
        0,
        width,
        height
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    commandList = commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr ||
        !commandList->begin({"MainMenuClear.Commands", RhiCommandListType::Graphics})) {
        return false;
    }
    commandList->textureBarrier({
        rhiDevice.currentSwapchainColorTexture(),
        RhiResourceState::Present,
        RhiResourceState::RenderTarget
    });
    commandList->beginRendering(renderingInfo);
    return true;
}

bool beginMenuOverlayPass(RhiDevice& rhiDevice,
                          RhiCommandListPool& commandListPool,
                          const uint32_t width,
                          const uint32_t height,
                          UIRenderer& uiRenderer,
                          RhiCommandList*& commandList) {
    const RhiTextureViewHandle colorView = rhiDevice.currentSwapchainColorView();
    const RhiTextureViewHandle depthView = rhiDevice.currentSwapchainDepthStencilView();
    if (!colorView.isValid() || !depthView.isValid()) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = colorView;
    colorAttachment.loadOp = RhiLoadOp::Load;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = RhiLoadOp::Load;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "MainMenuOverlay";
    renderingInfo.renderArea = {
        0,
        0,
        width,
        height
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    commandList = commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandList == nullptr ||
        !commandList->begin({"MainMenuOverlay.Commands", RhiCommandListType::Graphics})) {
        return false;
    }
    if (!uiRenderer.prepareTextFrame(*commandList)) {
        return false;
    }
    commandList->textureBarrier({
        rhiDevice.currentSwapchainColorTexture(),
        RhiResourceState::Present,
        RhiResourceState::RenderTarget
    });
    commandList->beginRendering(renderingInfo);
    return true;
}

void endMenuPass(RhiDevice& rhiDevice,
                 RhiCommandList*& commandList,
                 const bool transitionToPresent) {
    if (commandList == nullptr) {
        return;
    }

    commandList->endRendering();
    if (transitionToPresent) {
        commandList->textureBarrier({
            rhiDevice.currentSwapchainColorTexture(),
            RhiResourceState::RenderTarget,
            RhiResourceState::Present
        });
    }
    if (!commandList->end()) {
        std::abort();
    }
    RhiCommandList* submittedCommandLists[] = {commandList};
    if (!rhiDevice.submit({"MainMenu.Submit", submittedCommandLists, 1u})) {
        std::abort();
    }
    commandList = nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MainMenuAppState::MainMenuAppState(AppStateDependencies deps)
    : m_deps(deps) {}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void MainMenuAppState::onEnter() {
    m_deps.contextManager.pushContext(InputContextType::UI);
    m_deps.input.captureMouse(false);

    m_skyboxRenderer.init(m_deps.resourceMgr, m_deps.rhiDevice);
    m_skyboxYaw = 0.0f;
    m_transition.init(m_deps.resourceMgr);
    m_transitioningToGame = false;

    // ---- Main Menu callbacks ----

    m_mainMenuScreen.setLocaleManager(&m_deps.localeManager);
    m_mainMenuScreen.init(m_deps.resourceMgr);

    // "Start Game" → switch to save list
    m_mainMenuScreen.onStartClicked = [this]() {
        switchToPage(Page::SaveList);
    };

    // Multiplayer connect
    m_mainMenuScreen.onConnectClicked = [this](const std::string& address, int port) {
        m_pendingConfig = GameSessionConfig{1234, app::loadRenderDistance()};
        m_pendingConfig.serverAddress = address;
        m_pendingConfig.serverPort = static_cast<uint16_t>(port);
        m_transitioningToGame = true;
        m_transition.startFadeOut(0.5f);
    };

    m_mainMenuScreen.onQuitClicked = [this]() {
        glfwSetWindowShouldClose(m_deps.window.getHandle(), true);
    };

    // ---- Save List callbacks ----

    m_saveListScreen.setLocaleManager(&m_deps.localeManager);
    m_saveListScreen.setSavesRoot(m_savesRoot);
    m_saveListScreen.init(m_deps.resourceMgr);

    // User clicked a save → load it
    m_saveListScreen.onSaveSelected = [this](const std::string& worldFolder) {
        // Read the seed from level.json
        std::filesystem::path worldPath = m_savesRoot / worldFolder;
        save::SaveManager sm(worldPath);
        save::LevelMeta meta;
        int seed = 1234;
        if (sm.loadLevelMeta(meta)) {
            seed = static_cast<int>(meta.seed);
        }
        startGameWithWorld(worldFolder, seed);
    };

    // "Create New World" button → switch to create world screen
    m_saveListScreen.onCreateNewClicked = [this]() {
        switchToPage(Page::CreateWorld);
    };

    // "Back" → return to main menu
    m_saveListScreen.onBackClicked = [this]() {
        switchToPage(Page::MainMenu);
    };

    // ---- Create World callbacks ----

    m_createWorldScreen.setLocaleManager(&m_deps.localeManager);
    m_createWorldScreen.init(m_deps.resourceMgr);

    // "Start New Game" → create world and start
    m_createWorldScreen.onCreateWorld = [this](int seed, const std::string& displayName) {
        std::string worldName =
            CreateWorldScreen::generateWorldName(m_savesRoot);
        startGameWithWorld(worldName, seed, displayName.empty() ? worldName : displayName);
    };

    // "Back" → return to save list
    m_createWorldScreen.onBackClicked = [this]() {
        switchToPage(Page::SaveList);
    };

    // Start on the main menu page
    m_currentPage = Page::MainMenu;
    m_deps.uiRenderer.setActiveScene(&m_mainMenuScreen);
    m_mainMenuScreen.enterScene();
}

void MainMenuAppState::onExit() {
    m_deps.uiRenderer.setActiveScene(nullptr);

    switch (m_currentPage) {
    case Page::MainMenu:
        m_mainMenuScreen.exitScene();
        m_mainMenuScreen.shutdown();
        break;
    case Page::SaveList:
        m_saveListScreen.exitScene();
        m_saveListScreen.shutdown();
        break;
    case Page::CreateWorld:
        m_createWorldScreen.exitScene();
        m_createWorldScreen.shutdown();
        break;
    }

    m_skyboxRenderer.shutdown();
    m_transition.shutdown();
    m_deps.contextManager.popContext();
}

// ---------------------------------------------------------------------------
// Page switching
// ---------------------------------------------------------------------------

void MainMenuAppState::switchToPage(Page page) {
    // Exit current screen
    switch (m_currentPage) {
    case Page::MainMenu:
        m_mainMenuScreen.exitScene();
        break;
    case Page::SaveList:
        m_saveListScreen.exitScene();
        break;
    case Page::CreateWorld:
        m_createWorldScreen.exitScene();
        break;
    }

    m_currentPage = page;

    // Enter new screen
    switch (page) {
    case Page::MainMenu:
        m_deps.uiRenderer.setActiveScene(&m_mainMenuScreen);
        m_mainMenuScreen.enterScene();
        break;
    case Page::SaveList:
        m_deps.uiRenderer.setActiveScene(&m_saveListScreen);
        m_saveListScreen.enterScene();
        break;
    case Page::CreateWorld:
        m_deps.uiRenderer.setActiveScene(&m_createWorldScreen);
        m_createWorldScreen.enterScene();
        break;
    }
}

// ---------------------------------------------------------------------------
// Start game with a specific world
// ---------------------------------------------------------------------------

void MainMenuAppState::startGameWithWorld(const std::string& worldName,
                                          int seed,
                                          const std::string& displayName) {
    m_pendingConfig = GameSessionConfig{seed, app::loadRenderDistance()};
    m_pendingConfig.worldName = worldName;
    m_pendingConfig.worldDisplayName = displayName;
    m_pendingConfig.saveRoot  = m_savesRoot.string();
    m_transitioningToGame = true;
    m_transition.startFadeOut(0.5f);
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void MainMenuAppState::update(double frameTime, double& accumulator) {
    m_deps.input.update();
    const auto& snapshot = m_deps.input.snapshot();

    const UIInputRouteResult uiRouteResult =
        UIInputAdapter::routeInput(m_deps.uiRenderer, snapshot, m_deps.contextManager);

    const bool cancel = m_deps.contextManager.isActionTriggered(Action::Cancel);
    const bool menu   = m_deps.contextManager.isActionTriggered(Action::Menu);

    // Escape / Cancel handling depends on current page
    if ((menu || cancel) && uiRouteResult.aggregate != UIEventResult::Consumed) {
        switch (m_currentPage) {
        case Page::MainMenu:
            // Quit the game
            glfwSetWindowShouldClose(m_deps.window.getHandle(), true);
            accumulator = 0.0;
            return;
        case Page::SaveList:
            switchToPage(Page::MainMenu);
            accumulator = 0.0;
            return;
        case Page::CreateWorld:
            switchToPage(Page::SaveList);
            accumulator = 0.0;
            return;
        }
    }

    // Update animations on the active screen
    switch (m_currentPage) {
    case Page::MainMenu:
        m_mainMenuScreen.updateAnimations(static_cast<float>(frameTime));
        break;
    case Page::SaveList:
        m_saveListScreen.updateAnimations(static_cast<float>(frameTime));
        break;
    case Page::CreateWorld:
        m_createWorldScreen.updateAnimations(static_cast<float>(frameTime));
        break;
    }

    m_skyboxYaw += static_cast<float>(frameTime) * 3.0f;

    // Fade-out transition to gameplay
    if (m_transitioningToGame) {
        m_transition.tick(static_cast<float>(frameTime));
        if (m_transition.isDone()) {
            m_deps.appFsm.changeState(
                std::make_unique<LoadingAppState>(m_deps, m_pendingConfig));
            accumulator = 0.0;
            return;
        }
    }

    accumulator = 0.0;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void MainMenuAppState::render(double frameTime) {
    (void)frameTime;
    const Window::FramebufferSize framebufferSize = m_deps.window.getFramebufferSize();
    if (framebufferSize.width <= 0 || framebufferSize.height <= 0) {
        return;
    }
    const uint32_t width = static_cast<uint32_t>(framebufferSize.width);
    const uint32_t height = static_cast<uint32_t>(framebufferSize.height);
    if (!m_deps.rhiDevice.resizeSwapchain(width, height)) {
        MECRAFT_LOG_STREAM(std::cerr << "[MainMenuAppState] Failed to resize the RHI swapchain\n");
        return;
    }
    const RhiFrameAcquireResult frame = m_deps.rhiDevice.acquireFrame();
    if (frame.status == RhiFrameStatus::Minimized ||
        frame.status == RhiFrameStatus::OutOfDate) {
        return;
    }
    if (frame.status != RhiFrameStatus::Success &&
        frame.status != RhiFrameStatus::Suboptimal) {
        MECRAFT_LOG_STREAM(std::cerr << "[MainMenuAppState] Failed to acquire the RHI frame\n");
        return;
    }

    RhiCommandList* commandList = nullptr;
    if (!beginMenuClearPass(m_deps.rhiDevice, m_deps.commandListPool,
                            frame.width, frame.height, commandList)) {
        MECRAFT_LOG_STREAM(std::cerr << "[MainMenuAppState] Failed to begin RHI menu clear pass\n");
        (void)m_deps.rhiDevice.presentFrame(
            {frame.frameIndex, frame.imageIndex, Time::getFrameIndex()});
        return;
    }
    endMenuPass(m_deps.rhiDevice, commandList, true);

    m_skyboxRenderer.render(static_cast<int>(frame.width), static_cast<int>(frame.height),
                            static_cast<float>(frame.width) / static_cast<float>(frame.height),
                            m_skyboxYaw, 10.0f,
                            m_deps.rhiDevice);
    UIRenderContext sceneContext =
        m_deps.uiRenderer.prepareSceneContext(static_cast<int>(frame.width),
                                              static_cast<int>(frame.height),
                                              m_deps.rhiDevice,
                                              m_deps.input.snapshot());

    if (!beginMenuOverlayPass(m_deps.rhiDevice, m_deps.commandListPool,
                              frame.width, frame.height, m_deps.uiRenderer, commandList)) {
        MECRAFT_LOG_STREAM(std::cerr << "[MainMenuAppState] Failed to begin RHI menu overlay pass\n");
        (void)m_deps.rhiDevice.presentFrame(
            {frame.frameIndex, frame.imageIndex, Time::getFrameIndex()});
        return;
    }
    sceneContext.commandList = commandList;
    m_deps.uiRenderer.renderSceneOnlyPrepared(sceneContext);

    if (m_transitioningToGame) {
        m_transition.render(static_cast<int>(frame.width), static_cast<int>(frame.height),
                            *commandList);
    }

    endMenuPass(m_deps.rhiDevice, commandList, true);
    const RhiFrameStatus presentStatus = m_deps.rhiDevice.presentFrame(
        {frame.frameIndex, frame.imageIndex, Time::getFrameIndex()});
    if (presentStatus != RhiFrameStatus::Success &&
        presentStatus != RhiFrameStatus::Suboptimal &&
        presentStatus != RhiFrameStatus::OutOfDate &&
        presentStatus != RhiFrameStatus::Minimized) {
        MECRAFT_LOG_STREAM(std::cerr << "[MainMenuAppState] Failed to present the RHI frame\n");
    }
}
