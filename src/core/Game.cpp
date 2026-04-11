//
// Created by Caiwe on 2026/3/21.
//
#include "Game.h"
#include "states/GameplayState.h"
#include "../world/Block.h"
#include "../audio/AudioListener.h"

#ifndef NDEBUG
#include <chrono>
#endif

Game::Game() : m_contextManager(m_actionMap,m_input), m_physicsSystem(&m_world) {
}

void Game::init(int width, int height, const char *title) {
    // 初始化opengl环境
    if (m_window.init(width, height, title)) {
        m_input.init(m_window.getHandle());
        m_input.captureMouse(true);
    }
    else {
        std::cerr << "Error while initializing the window." << std::endl;
    }
    // Load bindings
    m_actionMap.loadFromFile("../assets/config/keybindings.txt");


    // 初始化时间
    Time::init();
    // 初始化资源管理器，加载着色器/贴图
    m_resourceMgr.init();
    m_resourceMgr.buildTextureAtlas("../assets/textures/blocks", 16);
    m_resourceMgr.loadGuiTexture("widgets", "../assets/textures/gui/widgets.png", true);
    m_resourceMgr.loadGuiTexture("font_ascii", "../assets/textures/font/ascii.png", true);
    BlockRegistry::init(&m_resourceMgr);
    m_resourceMgr.buildBlockIconAtlas(64);
#ifndef NDEBUG
    BlockRegistry::printAllBlocks();
#endif
    m_world.init(3123345);
    m_world.setRenderDistance(8);
    // 初始化玩家
    m_player.init({0.0f, static_cast<float>(m_world.getSurfaceY(0, 0) + 2), 0.0f});
    // 初始化渲染器
    m_renderer.init(m_resourceMgr);
    m_dropRenderer.init(m_resourceMgr);
    m_postProcessRenderer.init(m_resourceMgr);
    m_particleSystem.init(m_resourceMgr);
    glEnable(GL_DEPTH_TEST);
    m_audioEngine.init();
    m_bgmSystem.init(m_audioEngine);
    // 初始化UI渲染器
    m_uiRenderer.init(m_resourceMgr);
    // Push initial Gameplay state
    m_stateMachine.pushState(std::make_unique<GameplayState>(
        m_stateMachine,
        m_player,
        m_contextManager,
        m_input,
        m_uiRenderer,
        m_lastSubmittedCommand,
        m_physicsSystem,
        m_world,
        m_audioEngine,
        m_particleSystem,
        m_dropSystem
    ));

    // 初始化信息仪表盘 (仅Debug模式)
#ifndef NDEBUG
    m_dashboard.init(m_window);
#endif


}

double Game::clampFrameTime(const double dt) {
    constexpr double kMaxFrameTime = 0.25;
    return dt > kMaxFrameTime ? kMaxFrameTime : dt;
}

void Game::runFixedUpdate(const double fixedStep, double& accumulator) {
#ifndef NDEBUG
    const auto inputStart = std::chrono::steady_clock::now();
#endif
    m_input.update();
#ifndef NDEBUG
    const auto inputEnd = std::chrono::steady_clock::now();
    const auto stateStart = std::chrono::steady_clock::now();
#endif
    accumulator -= fixedStep;
    m_stateMachine.update(static_cast<float>(fixedStep), m_input.snapshot());
#ifndef NDEBUG
    const auto stateEnd = std::chrono::steady_clock::now();
    const auto particleStart = std::chrono::steady_clock::now();
#endif
    m_particleSystem.update(static_cast<float>(fixedStep));
#ifndef NDEBUG
    const auto particleEnd = std::chrono::steady_clock::now();
    const auto dropStart = std::chrono::steady_clock::now();
#endif
    m_dropSystem.update(static_cast<float>(fixedStep), m_world);
#ifndef NDEBUG
    const auto dropEnd = std::chrono::steady_clock::now();
    const auto worldStart = std::chrono::steady_clock::now();
#endif
    m_world.update(m_player.getPosition());
#ifndef NDEBUG
    const auto worldEnd = std::chrono::steady_clock::now();

    m_frameProfilerDebug.fixedInputAccumMs += std::chrono::duration<double, std::milli>(inputEnd - inputStart).count();
    m_frameProfilerDebug.fixedStateAccumMs += std::chrono::duration<double, std::milli>(stateEnd - stateStart).count();
    m_frameProfilerDebug.fixedParticleAccumMs += std::chrono::duration<double, std::milli>(particleEnd - particleStart).count();
    m_frameProfilerDebug.fixedDropAccumMs += std::chrono::duration<double, std::milli>(dropEnd - dropStart).count();
    m_frameProfilerDebug.fixedWorldAccumMs += std::chrono::duration<double, std::milli>(worldEnd - worldStart).count();
    ++m_frameProfilerDebug.fixedStepCount;
#endif
}

void Game::syncAudioListener(const float deltaTime) {
    // Update BGM before AudioEngine cleanup so track-end detection keeps a valid source pointer.
    m_bgmSystem.update(deltaTime);
    m_audioEngine.update(deltaTime);
    AudioListener::setPosition(m_player.getEyePosition());
    AudioListener::setOrientation(
        m_player.getCamera().getFront(),
        m_player.getCamera().getUp()
    );
    //AudioListener::setGain(1.0f);
}

void Game::renderFrame() {
    m_postProcessRenderer.beginScene(m_window);
    m_renderer.render(m_world, m_player.getCamera(), m_window, m_player);
    m_dropRenderer.render(m_dropSystem, m_player.getCamera(), m_window);
    m_particleSystem.render(m_player.getCamera().getProjectionMatrix(m_window.getAspectRatio()),
                            m_player.getCamera().getViewMatrix());

    PostProcessEffects effects;
    effects.underwaterEnabled = m_player.isEyesInWater();
    m_postProcessRenderer.setEffects(effects);
    m_postProcessRenderer.endSceneAndComposite(m_window);

    m_uiRenderer.render(m_window, m_player.getInventory());
    m_stateMachine.render();
#ifndef NDEBUG
    m_dashboard.render(m_player, m_world, m_player.getCamera(), m_renderer, m_uiRenderer,
                       m_dashboardProfilerStats);
#endif
    m_window.swapBuffers();
}

#ifndef NDEBUG
void Game::publishDebugFrameProfiler(const double frameTime) {
    const auto pushHistory = [](std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples>& history,
                                const size_t index,
                                const double valueMs) {
        history[index] = static_cast<float>(valueMs);
    };
    const auto copyHistory = [](const std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples>& source,
                                const size_t writeIndex,
                                const size_t count,
                                std::array<float, Dashboard::FrameProfilerStats::kFixedHistorySamples>& destination) {
        destination.fill(0.0f);
        const size_t historySize = source.size();
        for (size_t i = 0; i < count; ++i) {
            const size_t sourceIndex = (writeIndex + historySize - count + i) % historySize;
            destination[i] = source[sourceIndex];
        }
    };
    const auto smooth = [](const double previous, const double current) {
        constexpr double kAlpha = 0.15;
        return previous + (current - previous) * kAlpha;
    };

    if (m_frameProfilerDebug.fixedStepCount > 0) {
        const double invStepCount = 1.0 / static_cast<double>(m_frameProfilerDebug.fixedStepCount);
        const double inputAvgMs = m_frameProfilerDebug.fixedInputAccumMs * invStepCount;
        const double stateAvgMs = m_frameProfilerDebug.fixedStateAccumMs * invStepCount;
        const double particleAvgMs = m_frameProfilerDebug.fixedParticleAccumMs * invStepCount;
        const double dropAvgMs = m_frameProfilerDebug.fixedDropAccumMs * invStepCount;
        const double worldAvgMs = m_frameProfilerDebug.fixedWorldAccumMs * invStepCount;
        const double totalAvgMs = inputAvgMs + stateAvgMs + particleAvgMs + dropAvgMs + worldAvgMs;

        m_frameProfilerDebug.fixedInputMs = smooth(m_frameProfilerDebug.fixedInputMs, inputAvgMs);
        m_frameProfilerDebug.fixedStateUpdateMs = smooth(m_frameProfilerDebug.fixedStateUpdateMs, stateAvgMs);
        m_frameProfilerDebug.fixedParticleUpdateMs = smooth(m_frameProfilerDebug.fixedParticleUpdateMs, particleAvgMs);
        m_frameProfilerDebug.fixedDropUpdateMs = smooth(m_frameProfilerDebug.fixedDropUpdateMs, dropAvgMs);
        m_frameProfilerDebug.fixedWorldUpdateMs = smooth(m_frameProfilerDebug.fixedWorldUpdateMs, worldAvgMs);
        m_frameProfilerDebug.fixedUpdateMs = smooth(m_frameProfilerDebug.fixedUpdateMs, totalAvgMs);

        const size_t writeIndex = m_frameProfilerDebug.historyWriteIndex;
        pushHistory(m_frameProfilerDebug.fixedUpdateHistory, writeIndex, totalAvgMs);
        pushHistory(m_frameProfilerDebug.fixedInputHistory, writeIndex, inputAvgMs);
        pushHistory(m_frameProfilerDebug.fixedStateHistory, writeIndex, stateAvgMs);
        pushHistory(m_frameProfilerDebug.fixedParticleHistory, writeIndex, particleAvgMs);
        pushHistory(m_frameProfilerDebug.fixedDropHistory, writeIndex, dropAvgMs);
        pushHistory(m_frameProfilerDebug.fixedWorldHistory, writeIndex, worldAvgMs);
        m_frameProfilerDebug.historyWriteIndex = (writeIndex + 1) % m_frameProfilerDebug.fixedUpdateHistory.size();
        if (m_frameProfilerDebug.historyCount < m_frameProfilerDebug.fixedUpdateHistory.size()) {
            ++m_frameProfilerDebug.historyCount;
        }

        m_frameProfilerDebug.fixedInputAccumMs = 0.0;
        m_frameProfilerDebug.fixedStateAccumMs = 0.0;
        m_frameProfilerDebug.fixedParticleAccumMs = 0.0;
        m_frameProfilerDebug.fixedDropAccumMs = 0.0;
        m_frameProfilerDebug.fixedWorldAccumMs = 0.0;
        m_frameProfilerDebug.fixedStepCount = 0;
    }

    m_frameProfilerDebug.audioMs = smooth(m_frameProfilerDebug.audioMs, m_dashboardProfilerStats.audioMs);
    m_frameProfilerDebug.renderMs = smooth(m_frameProfilerDebug.renderMs, m_dashboardProfilerStats.renderMs);

    m_frameProfilerDebug.publishAccumulator += frameTime;
    if (m_frameProfilerDebug.publishAccumulator < m_frameProfilerDebug.publishInterval) {
        return;
    }
    m_frameProfilerDebug.publishAccumulator -= m_frameProfilerDebug.publishInterval;

    m_dashboardProfilerStats.frameMs = frameTime * 1000.0;
    m_dashboardProfilerStats.fixedUpdateMs = m_frameProfilerDebug.fixedUpdateMs;
    m_dashboardProfilerStats.fixedInputMs = m_frameProfilerDebug.fixedInputMs;
    m_dashboardProfilerStats.fixedStateUpdateMs = m_frameProfilerDebug.fixedStateUpdateMs;
    m_dashboardProfilerStats.fixedParticleUpdateMs = m_frameProfilerDebug.fixedParticleUpdateMs;
    m_dashboardProfilerStats.fixedDropUpdateMs = m_frameProfilerDebug.fixedDropUpdateMs;
    m_dashboardProfilerStats.fixedWorldUpdateMs = m_frameProfilerDebug.fixedWorldUpdateMs;
    m_dashboardProfilerStats.audioMs = m_frameProfilerDebug.audioMs;
    m_dashboardProfilerStats.renderMs = m_frameProfilerDebug.renderMs;
    m_dashboardProfilerStats.fixedHistoryCount = m_frameProfilerDebug.historyCount;
    copyHistory(m_frameProfilerDebug.fixedUpdateHistory,
                m_frameProfilerDebug.historyWriteIndex,
                m_frameProfilerDebug.historyCount,
                m_dashboardProfilerStats.fixedUpdateHistory);
    copyHistory(m_frameProfilerDebug.fixedInputHistory,
                m_frameProfilerDebug.historyWriteIndex,
                m_frameProfilerDebug.historyCount,
                m_dashboardProfilerStats.fixedInputHistory);
    copyHistory(m_frameProfilerDebug.fixedStateHistory,
                m_frameProfilerDebug.historyWriteIndex,
                m_frameProfilerDebug.historyCount,
                m_dashboardProfilerStats.fixedStateHistory);
    copyHistory(m_frameProfilerDebug.fixedParticleHistory,
                m_frameProfilerDebug.historyWriteIndex,
                m_frameProfilerDebug.historyCount,
                m_dashboardProfilerStats.fixedParticleHistory);
    copyHistory(m_frameProfilerDebug.fixedDropHistory,
                m_frameProfilerDebug.historyWriteIndex,
                m_frameProfilerDebug.historyCount,
                m_dashboardProfilerStats.fixedDropHistory);
    copyHistory(m_frameProfilerDebug.fixedWorldHistory,
                m_frameProfilerDebug.historyWriteIndex,
                m_frameProfilerDebug.historyCount,
                m_dashboardProfilerStats.fixedWorldHistory);
}
#endif

void Game::run() {
    constexpr double kFixedStep = TICK_RATE;
    double accumulator = 0.0;

    while (!m_window.shouldClose()) {
        // 1) Pump OS events and advance frame clock.
        m_window.pollEvents();
        Time::update();

        const double frameTime = clampFrameTime(Time::deltaTime);
        accumulator += frameTime;

        // 2) Consume as many fixed simulation steps as needed.
        while (accumulator >= kFixedStep) {
            runFixedUpdate(kFixedStep, accumulator);
        }

        // 3) Sync listener and submit render passes.
#ifndef NDEBUG
        const auto audioStart = std::chrono::steady_clock::now();
#endif
        syncAudioListener(static_cast<float>(frameTime));
#ifndef NDEBUG
        const auto audioEnd = std::chrono::steady_clock::now();
        const auto renderStart = std::chrono::steady_clock::now();
#endif
        renderFrame();
#ifndef NDEBUG
        const auto renderEnd = std::chrono::steady_clock::now();

        m_dashboardProfilerStats.audioMs = std::chrono::duration<double, std::milli>(audioEnd - audioStart).count();
        m_dashboardProfilerStats.renderMs = std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();
        publishDebugFrameProfiler(frameTime);
#endif
    }
}

void Game::shutdown() {
    m_bgmSystem.shutdown();
    m_audioEngine.shutdown();
    m_particleSystem.shutdown();
    m_uiRenderer.shutdown();
    m_postProcessRenderer.shutdown();
    m_dropRenderer.shutdown();
    m_renderer.shutdown();
}
