//
// Created by Caiwe on 2026/3/25.
//

// Dashboard 调试 UI 仅在 Debug 模式下编译
#ifdef MECRAFT_DEBUG

#include "Dashboard.h"

#include "ui/core/UIRenderer.h"
#include "../ecs/components/Components.h"
#include "../renderer/renderers/FirstPersonHeldItemRenderer.h"
#include "../renderer/core/RenderSettings.h"
#include "../renderer/debug/RenderDebugService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cmath>
#include <cstdint>

Dashboard::Dashboard() : m_initialized(false) {
    // Setup Dear ImGui context
}

Dashboard::~Dashboard() {
    shutdown();
}

void Dashboard::shutdown() {
    if (m_initialized) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_initialized = false;
    }
}

void Dashboard::init(const Window &window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window.getHandle(), true);          // Second param install_callback=true will install GLFW callbacks and chain to existing ones.
    ImGui_ImplOpenGL3_Init();
    m_initialized = true;
}

void Dashboard::setFirstPersonHeldItemRenderer(FirstPersonHeldItemRenderer* renderer) {
    m_firstPersonHeldItemRenderer = renderer;
}

void Dashboard::render(ecs::GameplayRegistry &registry,
                       World &world,
                       Camera &camera,
                       RenderResourceHub &render,
                       RenderScene& renderScene,
                       PostProcessPass& postProcess,
                       UIRenderer& uiRenderer,
                       FrameProfilerStats& profilerStats,
                       const std::function<void(int)>& renderDistanceSetter) {
    // (Your code calls glfwPollEvents())
    // ...
    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::GetIO().FontGlobalScale = m_fontScale;

    if (ImGui::Begin("Debug Dashboard")) {
        ImGui::SliderFloat("Font Scale", &m_fontScale, 0.8f, 3.0f, "%.1f");
        showPlayerStats(registry);
        showCameraStats(camera);
        showWorldStats(world, registry, renderDistanceSetter);
        showPerformanceStats(world, render, renderScene, postProcess, profilerStats);
        showGUIScaleSettings(uiRenderer);
        showCrosshairSettings(uiRenderer);
        showHotbarSettings(uiRenderer);
        showInventoryPanelSettings(uiRenderer);
        showCraftingGridSettings(uiRenderer);
        if (m_firstPersonHeldItemRenderer != nullptr) {
            showHeldItemPreviewSettings(*m_firstPersonHeldItemRenderer);
        }
        showTextSettings(uiRenderer);
    }
    ImGui::End();
    // Rendering
    // (Your code clears your framebuffer, renders your other stuff etc.)
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    // (Your code calls glfwSwapBuffers() etc.)
}

void Dashboard::showPlayerStats(ecs::GameplayRegistry &registry) {
    if (ImGui::CollapsingHeader("Player Stats")) {
        ecs::PlayerQuery query(registry);

        const glm::vec3 position = query.getPosition();
        ImGui::Text("Position: (%.2f, %.2f, %.2f)", position.x, position.y, position.z);
        ImGui::Text("Eye Height: %.3f", query.getEyeHeight());
        ImGui::Text("Moving: %s", query.isMoving() ? "Yes" : "No");
        ImGui::Text("Sprinting: %s", query.isSprinting() ? "Yes" : "No");

        // Read/write view bob parameters directly from ECS component
        auto view = registry.view<ecs::LocalPlayerTag, ecs::ViewBobComponent>();
        for (auto e : view) {
            auto& viewBob = view.get<ecs::ViewBobComponent>(e);

            float bobAmplitude = viewBob.amplitude;
            if (ImGui::SliderFloat("Bob Amplitude", &bobAmplitude, 0.0f, 0.3f, "%.4f")) {
                viewBob.amplitude = bobAmplitude;
            }

            float horizontalBobAmplitude = viewBob.horizontalAmplitude;
            if (ImGui::SliderFloat("Horizontal Bob Amplitude", &horizontalBobAmplitude, 0.0f, 0.3f, "%.4f")) {
                viewBob.horizontalAmplitude = horizontalBobAmplitude;
            }

            float bobFrequency = viewBob.frequency;
            ImGui::Text("Bob Frequency: %.2f", bobFrequency);
            if (ImGui::SliderFloat("Bob Frequency", &bobFrequency, 0.0f, 40.0f, "%.2f")) {
                viewBob.frequency = bobFrequency;
            }

            constexpr float kPi = 3.14159265358979323846f;
            constexpr float kRadToDeg = 180.0f / kPi;
            constexpr float kDegToRad = kPi / 180.0f;
            float phaseOffsetDegrees = viewBob.phaseOffset * kRadToDeg;
            if (ImGui::SliderFloat("Horizontal Phase Offset (deg)", &phaseOffsetDegrees, -180.0f, 180.0f, "%.1f")) {
                viewBob.phaseOffset = phaseOffsetDegrees * kDegToRad;
            }

            constexpr int kCurveSamples = 240;
            constexpr float kPreviewSeconds = 4.0f;
            constexpr float kMaxVerticalPreview = 0.09f; // 0.3^2 from slider upper bound.
            constexpr float kMaxHorizontalPreview = 0.3f; // Matches horizontal amplitude slider upper bound.
            std::array<float, kCurveSamples> verticalCurve{};
            std::array<float, kCurveSamples> horizontalCurve{};
            const float phaseOffset = viewBob.phaseOffset;
            for (int i = 0; i < kCurveSamples; ++i) {
                const float t = (kPreviewSeconds * static_cast<float>(i)) / static_cast<float>(kCurveSamples - 1);
                const float phase = t * bobFrequency;
                const float verticalRaw = bobAmplitude * static_cast<float>(std::sin(phase));
                verticalCurve[static_cast<size_t>(i)] = verticalRaw * verticalRaw;
                horizontalCurve[static_cast<size_t>(i)] = horizontalBobAmplitude * static_cast<float>(std::cos(phase + phaseOffset));
            }

            const float previewCycles = bobFrequency * kPreviewSeconds / (2.0f * kPi);
            ImGui::Text("Preview Window: %.1fs (%.2f cycles)", kPreviewSeconds, previewCycles);
            ImGui::PlotLines("Vertical Bob Curve", verticalCurve.data(), kCurveSamples, 0, nullptr,
                             0.0f, kMaxVerticalPreview, ImVec2(0.0f, 90.0f));
            ImGui::PlotLines("Horizontal Bob Curve", horizontalCurve.data(), kCurveSamples, 0, nullptr,
                             -kMaxHorizontalPreview, kMaxHorizontalPreview, ImVec2(0.0f, 90.0f));

            break; // Only one local player
        }
    }
}

void Dashboard::showWorldStats(World& world,
                               ecs::GameplayRegistry& registry,
                               const std::function<void(int)>& renderDistanceSetter) {
    if (ImGui::CollapsingHeader("World Stats")) {
        const double now = ImGui::GetTime();
        refreshWorldMetricsIfNeeded(world, now, false);

        ecs::PlayerQuery query(registry);
        const glm::vec3 position = query.getPosition();
        const int worldX = static_cast<int>(std::floor(position.x));
        const int worldZ = static_cast<int>(std::floor(position.z));
        const glm::ivec2 chunkCoords = world.getChunkCoords(worldX, worldZ);
        const TerrainBiome biome = world.getBiome(worldX, worldZ);

        ImGui::Text("Render Distance: %d chunks", world.getRenderDistance());
        ImGui::Text("Loaded Chunks: %zu", m_cachedWorldMetrics.activeChunks);
        ImGui::Text("Total Vertices: %zu", m_cachedWorldMetrics.totalVertices);
        ImGui::Text("Current Chunk: (%d, %d)", chunkCoords.x, chunkCoords.y);
        ImGui::Text("Current Biome: %s", World::biomeToString(biome));
        const auto setRenderDistance = [&](const int distance) {
            if (renderDistanceSetter) {
                renderDistanceSetter(distance);
            } else {
                world.setRenderDistance(distance);
            }
        };
        if (ImGui::Button("Increase Render Distance")) {
            setRenderDistance(world.getRenderDistance() + 1);
        }ImGui::SameLine();
        if (ImGui::Button("Decrease Render Distance")) {
            setRenderDistance(world.getRenderDistance() - 1);
        }
    }
}

void Dashboard::showCameraStats( Camera &camera) {
    if (ImGui::CollapsingHeader("Camera Stats")) {
        ImGui::Text("Position: (%.2f, %.2f, %.2f)", camera.getPosition().x, camera.getPosition().y, camera.getPosition().z);
        ImGui::Text("FOV: %.2f", camera.getFOV());ImGui::SameLine();
        static float fov = camera.getFOV();
        if (ImGui::SliderFloat("##FOV", &fov, 30.0f, 120.0f))
            camera.setFOV(fov);
        ImGui::Text("Near: %.2f", camera.getNear());ImGui::SameLine();
        static float near = camera.getNear();
        if (ImGui::SliderFloat("##Near", &near, 0.0f, 100.0f))
            camera.setNear(near);
        ImGui::Text("Far: %.2f", camera.getFar());ImGui::SameLine();
        static float far = camera.getFar();
        if (ImGui::SliderFloat("##Far", &far, 0.0f, 100.0f))
            camera.setFar(far);
    }
}

void Dashboard::refreshWorldMetricsIfNeeded(World& world, const double now, const bool forceRefresh) {
    if (!forceRefresh && m_cachedWorldMetrics.nextRefreshTime > 0.0 && now < m_cachedWorldMetrics.nextRefreshTime) {
        return;
    }

    CachedWorldMetrics metrics;
    metrics.activeChunks = world.getActiveChunks().size();
    for (const auto& [chunkKey, chunk] : world.getActiveChunks()) {
        (void)chunkKey;
        if (!chunk) {
            continue;
        }
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            const SubChunk* subChunk = chunk->getSubChunk(scy);
            if (!subChunk) {
                continue;
            }
            ++metrics.activeSubChunks;
            const SubChunkMesh& mesh = subChunk->getMesh();
            metrics.totalVertices += mesh.opaqueRange.vertexCount;
            metrics.totalVertices += mesh.cutoutRange.vertexCount;
            metrics.totalVertices += mesh.cutoutDistanceRange.vertexCount;
            metrics.totalVertices += mesh.transparentRange.vertexCount;
            metrics.totalVertices += mesh.waterRange.vertexCount;
            metrics.chunkStorageBytes += subChunk->estimatedMemoryBytes();
        }
    }
    metrics.nextRefreshTime = now + static_cast<double>(m_profilerStatsRefreshIntervalSec);
    m_cachedWorldMetrics = metrics;
}

void Dashboard::showPerformanceStats(World& world, RenderResourceHub &render, RenderScene& renderScene, PostProcessPass& postProcess, FrameProfilerStats& profilerStats) {
    if (ImGui::CollapsingHeader("Performance Stats")) {
        if (ImGui::SliderFloat("Stats Refresh", &m_profilerStatsRefreshIntervalSec, 0.1f, 2.0f, "%.1f s")) {
            m_nextProfilerStatsRefreshTime = 0.0;
            m_cachedWorldMetrics.nextRefreshTime = 0.0;
        }

        const double now = ImGui::GetTime();
        if (m_nextProfilerStatsRefreshTime <= 0.0 || now >= m_nextProfilerStatsRefreshTime) {
            m_displayProfilerStats = profilerStats;
            m_displayGpuStats = render.getGpuFrameStats();
            m_displayShadowStats = render.getShadowFrameStats();
            m_displayRenderWorkStats = render.getRenderWorkStats();
            m_displayLightStats = world.getLightFrameStats();
            refreshWorldMetricsIfNeeded(world, now, false);
            m_displayFps = ImGui::GetIO().Framerate;
            m_nextProfilerStatsRefreshTime = now + static_cast<double>(m_profilerStatsRefreshIntervalSec);
        }

        const FrameProfilerStats& displayedProfilerStats = m_displayProfilerStats;

        // Helper: append max value in orange on the same line
        const auto showMax = [](const char* label, double current, double maxVal) {
            ImGui::Text("%s: %.3f ms", label, current);
            if (maxVal > 0.0) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Max: %.3f ms", maxVal);
            }
        };

        ImGui::Text("FPS: %.1f", m_displayFps);
        ImGui::Text("Frame Time: %.3f ms", m_displayFps > 0.0 ? 1000.0 / m_displayFps : 0.0);
        showMax("Loop Frame (clamped)", displayedProfilerStats.frameMs, displayedProfilerStats.maxFrameMs);
        showMax("App Update Dispatch", displayedProfilerStats.appUpdateDispatchMs, displayedProfilerStats.maxAppUpdateDispatchMs);
        showMax("App Render Dispatch", displayedProfilerStats.appRenderDispatchMs, displayedProfilerStats.maxAppRenderDispatchMs);
        showMax("Fixed Update", displayedProfilerStats.fixedUpdateMs, displayedProfilerStats.maxFixedUpdateMs);
        showMax("  - Input Update", displayedProfilerStats.fixedInputMs, displayedProfilerStats.maxFixedInputMs);
        showMax("  - State Update", displayedProfilerStats.fixedStateUpdateMs, displayedProfilerStats.maxFixedStateUpdateMs);
        showMax("  - Particle Update", displayedProfilerStats.fixedParticleUpdateMs, displayedProfilerStats.maxFixedParticleUpdateMs);
        showMax("  - Drop Update", displayedProfilerStats.fixedDropUpdateMs, displayedProfilerStats.maxFixedDropUpdateMs);
        showMax("  - World Update", displayedProfilerStats.fixedWorldUpdateMs, displayedProfilerStats.maxFixedWorldUpdateMs);
        showMax("Audio Sync", displayedProfilerStats.audioMs, displayedProfilerStats.maxAudioMs);
        showMax("Render Submit", displayedProfilerStats.renderMs, displayedProfilerStats.maxRenderMs);
        showMax("  - Snapshot", displayedProfilerStats.renderSnapshotMs, displayedProfilerStats.maxRenderSnapshotMs);
        showMax("  - Scene", displayedProfilerStats.renderSceneMs, displayedProfilerStats.maxRenderSceneMs);
        showMax("  - UI", displayedProfilerStats.renderUiMs, displayedProfilerStats.maxRenderUiMs);
        showMax("  - Dashboard", displayedProfilerStats.renderDashboardMs, displayedProfilerStats.maxRenderDashboardMs);
        showMax("  - Swap Buffers", displayedProfilerStats.swapBuffersMs, displayedProfilerStats.maxSwapBuffersMs);
        showMax("  - Other Render", displayedProfilerStats.renderOtherMs, displayedProfilerStats.maxRenderOtherMs);
        showMax("Untracked / Wait", displayedProfilerStats.untrackedMs, displayedProfilerStats.maxUntrackedMs);

        if (displayedProfilerStats.maxFrameMs > 0.0 && ImGui::Button("Reset Max Frame Time")) {
            auto resetMaxFrameStats = [](FrameProfilerStats& stats) {
                stats.maxFrameMs = 0.0;
                stats.maxFixedUpdateMs = 0.0;
                stats.maxFixedInputMs = 0.0;
                stats.maxFixedStateUpdateMs = 0.0;
                stats.maxFixedParticleUpdateMs = 0.0;
                stats.maxFixedDropUpdateMs = 0.0;
                stats.maxFixedWorldUpdateMs = 0.0;
                stats.maxAudioMs = 0.0;
                stats.maxRenderMs = 0.0;
                stats.maxAppUpdateDispatchMs = 0.0;
                stats.maxAppRenderDispatchMs = 0.0;
                stats.maxRenderSnapshotMs = 0.0;
                stats.maxRenderSceneMs = 0.0;
                stats.maxRenderUiMs = 0.0;
                stats.maxRenderDashboardMs = 0.0;
                stats.maxSwapBuffersMs = 0.0;
                stats.maxRenderOtherMs = 0.0;
                stats.maxUntrackedMs = 0.0;
            };
            resetMaxFrameStats(profilerStats);
            resetMaxFrameStats(m_displayProfilerStats);
        }

        auto historyMax = [](const float* history, const size_t count, const float fallbackMax) {
            float maxValue = 0.0f;
            for (size_t i = 0; i < count; ++i) {
                if (history[i] > maxValue) {
                    maxValue = history[i];
                }
            }
            return std::max(maxValue * 1.1f, fallbackMax);
        };

        if (displayedProfilerStats.frameHistoryCount > 1) {
            ImGui::Separator();
            ImGui::Text("Frame History");
            ImGui::PlotLines("FPS", displayedProfilerStats.fpsHistory.data(),
                             static_cast<int>(displayedProfilerStats.frameHistoryCount), 0, nullptr,
                             0.0f, historyMax(displayedProfilerStats.fpsHistory.data(), displayedProfilerStats.frameHistoryCount, 1.0f),
                             ImVec2(0.0f, 65.0f));
        }

        if (displayedProfilerStats.fixedHistoryCount > 1) {
            ImGui::PlotLines("Render Submit", displayedProfilerStats.renderHistory.data(),
                             static_cast<int>(displayedProfilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(displayedProfilerStats.renderHistory.data(), displayedProfilerStats.fixedHistoryCount, 0.1f),
                             ImVec2(0.0f, 55.0f));
        }

        if (displayedProfilerStats.fixedHistoryCount > 1) {
            ImGui::Separator();
            ImGui::Text("Fixed Update History (ms/step)");
            ImGui::PlotLines("Fixed Total", displayedProfilerStats.fixedUpdateHistory.data(),
                             static_cast<int>(displayedProfilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(displayedProfilerStats.fixedUpdateHistory.data(), displayedProfilerStats.fixedHistoryCount, 0.1f),
                             ImVec2(0.0f, 65.0f));
            ImGui::PlotLines("Input", displayedProfilerStats.fixedInputHistory.data(),
                             static_cast<int>(displayedProfilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(displayedProfilerStats.fixedInputHistory.data(), displayedProfilerStats.fixedHistoryCount, 0.1f),
                             ImVec2(0.0f, 55.0f));
            ImGui::PlotLines("State", displayedProfilerStats.fixedStateHistory.data(),
                             static_cast<int>(displayedProfilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(displayedProfilerStats.fixedStateHistory.data(), displayedProfilerStats.fixedHistoryCount, 0.1f),
                             ImVec2(0.0f, 55.0f));
            ImGui::PlotLines("Particle", displayedProfilerStats.fixedParticleHistory.data(),
                             static_cast<int>(displayedProfilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(displayedProfilerStats.fixedParticleHistory.data(), displayedProfilerStats.fixedHistoryCount, 0.1f),
                             ImVec2(0.0f, 55.0f));
            ImGui::PlotLines("Drop", displayedProfilerStats.fixedDropHistory.data(),
                             static_cast<int>(displayedProfilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(displayedProfilerStats.fixedDropHistory.data(), displayedProfilerStats.fixedHistoryCount, 0.1f),
                             ImVec2(0.0f, 55.0f));
            ImGui::PlotLines("World", displayedProfilerStats.fixedWorldHistory.data(),
                             static_cast<int>(displayedProfilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(displayedProfilerStats.fixedWorldHistory.data(), displayedProfilerStats.fixedHistoryCount, 0.1f),
                             ImVec2(0.0f, 55.0f));
        }

        ImGui::Text("Terrain RHI Submissions: %d", render.getTerrainRhiSubmitCount());

        GpuFrameStats gpuStats = m_displayGpuStats;
        bool gpuTimerEnabled = render.isGpuTimerEnabled();
        if (ImGui::Checkbox("GPU Timer Query", &gpuTimerEnabled)) {
            render.setGpuTimerEnabled(gpuTimerEnabled);
        }
        if (!gpuStats.supported) {
            ImGui::Text("GPU Timers: unsupported");
        } else if (!gpuStats.valid) {
            ImGui::Text("GPU Timers: waiting");
        } else {
            const double gpuTotalMs = gpuStats.gbufferMs + gpuStats.shadowMs + gpuStats.ssaoMs + gpuStats.ssgiMs +
                                      gpuStats.lightingMs + gpuStats.transparentMs + gpuStats.volumetricMs +
                                      gpuStats.reflectionMs + gpuStats.cloudMs + gpuStats.waterMs + gpuStats.postMs;
            ImGui::Text("GPU Total (tracked): %.3f ms", gpuTotalMs);
            ImGui::Text("GPU GBuffer: %.3f ms", gpuStats.gbufferMs);
            ImGui::Text("GPU Shadow: %.3f ms", gpuStats.shadowMs);
            ImGui::Text("GPU SSAO: %.3f ms", gpuStats.ssaoMs);
            ImGui::Text("GPU SSGI: %.3f ms", gpuStats.ssgiMs);
            ImGui::Text("GPU Lighting: %.3f ms", gpuStats.lightingMs);
            ImGui::Text("GPU Transparent: %.3f ms", gpuStats.transparentMs);
            ImGui::Text("GPU Volumetric: %.3f ms", gpuStats.volumetricMs);
            ImGui::Text("GPU Reflection: %.3f ms", gpuStats.reflectionMs);
            ImGui::Text("GPU Cloud: %.3f ms", gpuStats.cloudMs);
            ImGui::Text("GPU Water: %.3f ms", gpuStats.waterMs);
            ImGui::Text("GPU Post: %.3f ms", gpuStats.postMs);
        }
        ShadowFrameStats shadowStats = m_displayShadowStats;
        if (shadowStats.supported && shadowStats.valid) {
            ImGui::Text("CSM GPU: %.3f ms  res=%d  submitted=%d  culled=%d  maxDist=%.1f",
                        shadowStats.gpuTotalMs,
                        shadowStats.shadowResolution,
                        shadowStats.submitted,
                        shadowStats.culled,
                        shadowStats.maxCasterDistance);
            for (int cascade = 0; cascade < shadowStats.cascadeCount; ++cascade) {
                const ShadowCascadeStats& s = shadowStats.cascades[static_cast<size_t>(cascade)];
                ImGui::Text("  C%d: %.3f ms (opaque %.3f / trans %.3f) split %.1f-%.1f texel %.4f radius %.1f",
                            cascade,
                            s.gpuTotalMs,
                            s.gpuOpaqueMs,
                            s.gpuTransparentMs,
                            s.splitNear,
                            s.splitFar,
                            s.texelWorldSize,
                            s.radius);
                ImGui::Text("      box %d/%d  dist %d/%d  entries c/t %d/%d  cmds o/c/t %zu/%zu/%zu",
                            s.boxVisible,
                            s.boxCulled,
                            s.distanceVisible,
                            s.distanceCulled,
                            s.cutoutEntries,
                            s.transparentEntries,
                            s.opaqueCommands,
                            s.cutoutCommands,
                            s.transparentCommands);
            }
        } else if (gpuStats.supported && gpuTimerEnabled) {
            ImGui::Text("CSM GPU: waiting");
        }

        const RenderWorkStats& renderWork = m_displayRenderWorkStats;
        const auto bytesToMiB = [](const uint64_t bytes) {
            return static_cast<double>(bytes) / (1024.0 * 1024.0);
        };
        const auto poolUsePercent = [](const size_t usedBytes, const size_t capacityBytes) {
            return capacityBytes > 0 ? static_cast<double>(usedBytes) * 100.0 / static_cast<double>(capacityBytes) : 0.0;
        };

        const int visibleMdiSubChunks = std::max(0, renderWork.mdiSubChunkTests - renderWork.mdiSubChunksCulled);
        constexpr const char* terrainVertexFormat = "PackedBlockVertex";

        ImGui::Separator();
        ImGui::Text("Render Work");
        ImGui::Text("Active Chunks/SubChunks: %llu / %llu",
                    static_cast<unsigned long long>(m_cachedWorldMetrics.activeChunks),
                    static_cast<unsigned long long>(m_cachedWorldMetrics.activeSubChunks));
        ImGui::Text("Chunk CPU Storage (approx): %.2f MiB",
                    bytesToMiB(static_cast<uint64_t>(m_cachedWorldMetrics.chunkStorageBytes)));
        ImGui::Text("Terrain Vertex: %llu bytes (%s)",
                    static_cast<unsigned long long>(renderWork.blockVertexBytes),
                    terrainVertexFormat);
        ImGui::Text("Terrain GPU Pool: %.2f / %.2f MiB (%.1f%%)",
                    bytesToMiB(static_cast<uint64_t>(renderWork.terrainPoolUsedBytes)),
                    bytesToMiB(static_cast<uint64_t>(renderWork.terrainPoolCapacityBytes)),
                    poolUsePercent(renderWork.terrainPoolUsedBytes, renderWork.terrainPoolCapacityBytes));
        ImGui::Text("Pool Used O/C/T: %.2f / %.2f / %.2f MiB",
                    bytesToMiB(static_cast<uint64_t>(renderWork.opaquePoolUsedBytes)),
                    bytesToMiB(static_cast<uint64_t>(renderWork.cutoutPoolUsedBytes)),
                    bytesToMiB(static_cast<uint64_t>(renderWork.transparentPoolUsedBytes)));
        ImGui::Text("Pool Capacity O/C/T: %.2f / %.2f / %.2f MiB",
                    bytesToMiB(static_cast<uint64_t>(renderWork.opaquePoolCapacityBytes)),
                    bytesToMiB(static_cast<uint64_t>(renderWork.cutoutPoolCapacityBytes)),
                    bytesToMiB(static_cast<uint64_t>(renderWork.transparentPoolCapacityBytes)));
        ImGui::Text("Terrain Metadata: %.3f MiB, %llu slots (%llu free)",
                    bytesToMiB(static_cast<uint64_t>(renderWork.terrainMetadataBytes)),
                    static_cast<unsigned long long>(renderWork.terrainMetadataSlots),
                    static_cast<unsigned long long>(renderWork.terrainMetadataFreeSlots));
        ImGui::Text("Frame Vertex Read: %.2f MiB (O/C/T %.2f / %.2f / %.2f)",
                    bytesToMiB(renderWork.terrainVertexReadBytes),
                    bytesToMiB(renderWork.opaqueVertexReadBytes),
                    bytesToMiB(renderWork.cutoutVertexReadBytes),
                    bytesToMiB(renderWork.transparentVertexReadBytes));
        ImGui::Text("MDI Commands O/C/T: %llu / %llu / %llu",
                    static_cast<unsigned long long>(renderWork.opaqueCommands),
                    static_cast<unsigned long long>(renderWork.cutoutCommands),
                    static_cast<unsigned long long>(renderWork.transparentCommands));
        ImGui::Text("Transparent Batches Generic/Water: %llu / %llu",
                    static_cast<unsigned long long>(renderWork.transparentGenericCommands),
                    static_cast<unsigned long long>(renderWork.transparentWaterCommands));
        ImGui::Text("Logical Commands O/C/T: %llu / %llu / %llu",
                    static_cast<unsigned long long>(renderWork.opaqueLogicalCommands),
                    static_cast<unsigned long long>(renderWork.cutoutLogicalCommands),
                    static_cast<unsigned long long>(renderWork.transparentLogicalCommands));
        ImGui::Text("Vertices O/C/T: %llu / %llu / %llu",
                    static_cast<unsigned long long>(renderWork.opaqueVertices),
                    static_cast<unsigned long long>(renderWork.cutoutVertices),
                    static_cast<unsigned long long>(renderWork.transparentVertices));
        ImGui::Text("Transparent Vertices Generic/Water: %llu / %llu",
                    static_cast<unsigned long long>(renderWork.transparentGenericVertices),
                    static_cast<unsigned long long>(renderWork.transparentWaterVertices));
        ImGui::Text("Pool Vertices O/C/T: %llu / %llu / %llu",
                    static_cast<unsigned long long>(renderWork.opaquePoolUsedVertices),
                    static_cast<unsigned long long>(renderWork.cutoutPoolUsedVertices),
                    static_cast<unsigned long long>(renderWork.transparentPoolUsedVertices));
        ImGui::Text("Pool Fragmentation O/C/T: %.1f%% / %.1f%% / %.1f%%",
                    renderWork.opaquePoolFragmentation * 100.0f,
                    renderWork.cutoutPoolFragmentation * 100.0f,
                    renderWork.transparentPoolFragmentation * 100.0f);
        ImGui::Text("Mesh Upload: %.2f MiB, %llu vertices, deferred %llu",
                    static_cast<double>(renderWork.meshUploadBytesThisFrame) / (1024.0 * 1024.0),
                    static_cast<unsigned long long>(renderWork.meshUploadVerticesThisFrame),
                    static_cast<unsigned long long>(renderWork.meshUploadDeferredCount));
        ImGui::Text("World Buffer Upload: %.3f ms, expands %llu",
                    renderWork.worldBufferUploadMs,
                    static_cast<unsigned long long>(renderWork.worldBufferExpandCount));

        bool cutoutDistanceLimit = render.isCutoutDistanceLimitEnabled();
        if (ImGui::Checkbox("Cutout Distance Limit", &cutoutDistanceLimit)) {
            render.setCutoutDistanceLimitEnabled(cutoutDistanceLimit);
        }
        float cutoutDistanceChunks = render.getCutoutRenderDistanceChunks();
        if (ImGui::SliderFloat("Cutout Distance (chunks)", &cutoutDistanceChunks, 1.0f, 16.0f, "%.1f")) {
            render.setCutoutRenderDistanceChunks(cutoutDistanceChunks);
        }
        ImGui::Text("Cutout Skipped: %d / %d",
                    renderWork.cutoutSkippedByDistance,
                    renderWork.cutoutCandidates);
        ImGui::Text("MDI SubChunk Culled: %d / %d",
                    renderWork.mdiSubChunksCulled,
                    renderWork.mdiSubChunkTests);
        ImGui::Text("MDI SubChunk Visible: %d", visibleMdiSubChunks);

        ImGui::Text("Simulation Speed: %.2f", Time::getTimeSpeed());
        bool chunkCullingDebugEnabled = render.isChunkCullingDebugEnabled();
        if (ImGui::Checkbox("Chunk Culling Debug", &chunkCullingDebugEnabled)) {
            render.setChunkCullingDebugEnabled(chunkCullingDebugEnabled);
        }
        static float timeSpeed = Time::getTimeSpeed();
        if (ImGui::SliderFloat("Simulation Speed", &timeSpeed, 0.0f, 10.0f)) {
            Time::setTimeSpeed(timeSpeed);
        }
        int submitBudget = render.getMeshingSubmitBudget();
        if (ImGui::SliderInt("Meshing Submit Budget", &submitBudget, 1, 64)) {
            render.setMeshingSubmitBudget(submitBudget);
        }

        int regionChunkSize = render.getRegionChunkSize();
        if (ImGui::SliderInt("Region Chunk Size", &regionChunkSize, 1, 16)) {
            render.setRegionChunkSize(regionChunkSize);
        }

        const float maxAnisotropy = render.getAtlasMaxAnisotropy();
        if (maxAnisotropy > 1.0f) {
            float anisotropy = render.getAtlasAnisotropy();
            if (ImGui::SliderFloat("Atlas Anisotropy", &anisotropy, 1.0f, maxAnisotropy, "%.1fx")) {
                render.setAtlasAnisotropy(anisotropy);
            }
        } else {
            ImGui::Text("Atlas Anisotropy: not supported");
        }

        ImGui::Separator();
        ImGui::Text("Render Pipeline");
        ImGui::Text("New Pipeline: %s", renderScene.getPipelineStatus());
        ImGui::Text("New Pipeline Ready: %s", renderScene.isNewPipelineReady() ? "yes" : "no");
        bool syncRenderSceneSettings = false;
        bool newPipelineActive = renderScene.isNewPipelineActive();
        if (!renderScene.isNewPipelineReady()) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Checkbox("Use Experimental New Pipeline", &newPipelineActive)) {
            renderScene.setNewPipelineActive(newPipelineActive);
            syncRenderSceneSettings = true;
        }
        if (!renderScene.isNewPipelineReady()) {
            ImGui::EndDisabled();
        }
        ImGui::TextDisabled("Experimental: overlays and transparent ordering are still under validation.");

        RenderSettings settings = renderScene.getSettings();
        int pipelineMode = static_cast<int>(settings.pipelineMode);
        int tonemapMode = settings.postProcess.tonemapMode;
        int debugViewMode = settings.debug.viewMode;
        int weatherPresetInstant = static_cast<int>(world.getWeatherSystem().getRenderState().type);
        int weatherPresetSmooth = static_cast<int>(world.getWeatherSystem().getTargetState().type);
        static constexpr const char* kPipelineModes[] = {"Forward (Vanilla)", "Deferred (Shader Effects)"};
        static constexpr const char* kTonemapModes[] = {
            "Reinhard [Mecraft extra]",
            "AcademyFit [DerivativeMain]",
            "Filmic [Mecraft extra]",
            "AgX_Minimal [DerivativeMain]",
            "AcademyFull [DerivativeMain]",
            "AgX_Full [DerivativeMain]"
        };
        static constexpr const char* kDebugViewModes[] = {
            "0: Off",
            "1: GBuffer Albedo",
            "2: GBuffer Normal",
            "3: GBuffer Vertex AO",
            "4: Voxel Light",
            "5: Material Rough/F0/Emission",
            "6: Material SSS",
            "7: Depth",
            "8: Shadow Depth",
            "9: SSAO",
            "10: Scene Lighting",
            "11: Scene Composite",
            "12: Transparent Composite",
            "13: Transparent Depth",
            "14: Volumetric RGB",
            "15: Volumetric Transmittance",
            "16: Sky Capture",
            "17: Velocity",
            "18: History Scene",
            "19: History Depth",
            "20: Shadow Projection",
            "21: Shadow Visibility",
            "22: Shadow Bias",
            "23: CSM Cascade",
            "24: Reflection Target",
            "25: Cloud Target",
            "26: Material Kind",
            "27: Material Aux",
            "28: Reflection History",
            "29: Cloud History",
            "30: SSR Hit Mask",
            "31: Scene Resolved",
            "32: Shadow UV",
            "33: Shadow Density",
            "34: Shadow Depth Compare",
            "35: Shadow Hit Caster",
            "36: CSM Depth 0",
            "37: CSM Depth 1",
            "38: CSM Depth 2",
            "39: CSM Depth 3",
            "40: Cascade Info",
            "41: Sky Dir Raw",
            "42: Sky Dir Cloudy",
            "43: Sky Dir Raw x20",
            "44: SkyCapture Atlas + Metadata",
            "45: Lighting Balance",
            "46: VFog Density",
            "47: VFog Transmittance",
            "48: VFog Sky Only",
            "49: VFog Sun Only",
            "50: VFog Sun Gates",
            "51: VFog Integration",
            "52: VFog Sky Ray Coverage",
            "53: VFog March Detail",
            "54: VFog Sun Contrast",
            "55: VFog Sun Only x20",
            "56: VFog Sun Only x100",
            "57: VFog Shadow Visibility",
            "58: VFog Shadow Raw vs PCF",
            "59: VFog Shadow Projection",
            "60: VFog Shadow Compare",
            "61: VFog Bias Compare",
            "62: VFog Cascade Index",
            "63: VFog Receiver Depth",
            "64: VFog Sun/Sky Ratio",
            "65: VFog Beam Modulation",
            "66: VFog Density Field",
            "67: TAA Current Scratch",
            "68: TAA Current-History Delta",
            "69: Velocity Sky Highlight",
            "70: Raw Half VFog",
            "71: Upscaled VFog",
            "72: UW VL Scatter",
            "73: UW VL Shadow",
            "74: UW VL Phase",
            "75: Shadow Depth Gap",
            "76: Shadow Color0",
            "77: Shadow Color1",
            "78: Reflection Composite Delta x32",
            "79: TAA Loss x32",
            "80: TAA Wet Reject Mask",
            "81: SSGI",
            "82: SSGI x8",
            "83: SSGI Confidence"
        };
        static constexpr const char* kWeatherPresets[] = {"Clear", "Rain", "Storm", "Snow"};
        bool pipelineChanged = false;
        pipelineChanged |= ImGui::Combo("Pipeline Mode", &pipelineMode, kPipelineModes, IM_ARRAYSIZE(kPipelineModes));
        settings.pipelineMode = static_cast<PipelineMode>(pipelineMode);
        pipelineChanged |= ImGui::Combo("Deferred Debug View", &debugViewMode, kDebugViewModes, IM_ARRAYSIZE(kDebugViewModes));
        settings.debug.viewMode = debugViewMode;
        static constexpr const char* kLightDebugModes[] = {
            "0: Off",
            "1: Direct Only",
            "2: Skylight Only",
            "3: Blocklight Only",
            "4: Minimum Ambient",
            "5: Fake Bounce",
            "6: Scene Before Fog",
            "7: Sky/Direct Ratio",
            "8: NdotL",
            "9: Cloud Shadow",
            "10: Outdoor Mask",
            "11: Direct Fraction",
            "12: Before AO",
            "13: After AO",
            "14: Raw SkyLight",
            "15: SkyLight Mask",
            "16: Vertex AO",
            "17: SSAO",
            "18: Normal Y",
            "19: Contact Shadow",
            "20: Puddle Mask",
            "21: Rain Splash Mask",
            "22: Rain Ripple Normal",
            "23: Rain Ripple Strength"
        };
        int lightDebugMode = settings.debug.deferredLightDebugMode;
        pipelineChanged |= ImGui::Combo("Light Debug", &lightDebugMode, kLightDebugModes, IM_ARRAYSIZE(kLightDebugModes));
        settings.debug.deferredLightDebugMode = lightDebugMode;
        static constexpr const char* kPostprocessDebugModes[] = {
            "0: Off",
            "1: BloomData",
            "2: FogTransmittance",
            "3: BloomyFog",
            "4: RainMask",
            "5: Time Pulse",
            "6: Raindrop Pattern",
            "7: Raindrop Distortion"
        };
        int ppDebugMode = settings.debug.postprocessDebugMode;
        pipelineChanged |= ImGui::Combo("Postprocess Debug", &ppDebugMode, kPostprocessDebugModes, IM_ARRAYSIZE(kPostprocessDebugModes));
        settings.debug.postprocessDebugMode = ppDebugMode;
        static constexpr const char* kReflectionDebugModes[] = {
            "0: Off",
            "1: PixelWetness",
            "2: Reflectance",
            "3: SSR Hit",
            "4: Roughness",
            "5: SpecularWeight x8",
            "6: CompositeDelta",
            "7: Puddle Mask",
            "8: Rain Splash Mask",
            "9: Rain Ripple Normal",
            "10: Rain Ripple Strength",
            "11: F0 x8",
            "12: Sky Fallback",
            "13: Reflection RGB x8",
            "14: Has Reflection",
            "15: Sky Light Raw",
            "16: Voxel Light RG",
            "17: Material Aux",
            "18: Sky Gradient x64",
            "19: Final Contribution",
            "20: Reflection Source",
            "21: Reflectance x32",
            "22: F0 x32",
            "23: Roughness",
            "24: Reflection Source x8",
            "25: Final Contribution x32",
            "26: Reflection/Scene Ratio",
            "27: Scene Luma",
            "28: Reflection Luma x64",
            "29: Reflectance x128",
            "30: Source Gradient x128"
        };
        int reflDebugMode = settings.debug.reflectionDebugMode;
        pipelineChanged |= ImGui::Combo("Reflection Debug", &reflDebugMode, kReflectionDebugModes, IM_ARRAYSIZE(kReflectionDebugModes));
        settings.debug.reflectionDebugMode = reflDebugMode;
        pipelineChanged |= ImGui::Checkbox("Sun Shadows", &settings.shadow.enabled);
        pipelineChanged |= ImGui::Checkbox("Soft Shadows", &settings.shadow.softShadowsEnabled);
        pipelineChanged |= ImGui::Checkbox("PCSS Shadows", &settings.shadow.pcssShadowsEnabled);
        pipelineChanged |= ImGui::Checkbox("Contact Shadows", &settings.shadow.contactShadowsEnabled);
        pipelineChanged |= ImGui::Checkbox("Cloud Shadows [DM optional]", &settings.cloud.shadowsEnabled);
        pipelineChanged |= ImGui::Checkbox("Derivative Strict", &settings.debug.derivativeStrictMode);
        pipelineChanged |= ImGui::Checkbox("Block PBR Maps", &settings.blockMaterialMaps.enabled);
        if (!settings.blockMaterialMaps.enabled) {
            ImGui::BeginDisabled();
        }
        pipelineChanged |= ImGui::Checkbox("Block Normal Maps", &settings.blockMaterialMaps.normalMapsEnabled);
        ImGui::SameLine();
        pipelineChanged |= ImGui::Checkbox("Block Specular Maps", &settings.blockMaterialMaps.specularMapsEnabled);
        ImGui::SameLine();
        pipelineChanged |= ImGui::Checkbox("Block Parallax Maps", &settings.blockMaterialMaps.parallaxMapsEnabled);
        pipelineChanged |= ImGui::SliderFloat("Block Parallax Depth", &settings.blockMaterialMaps.parallaxDepth, 0.0f, 0.12f, "%.3f");
        if (!settings.blockMaterialMaps.enabled) {
            ImGui::EndDisabled();
        }
        pipelineChanged |= ImGui::Checkbox("SSAO", &settings.ssao.enabled);
        pipelineChanged |= ImGui::Checkbox("SSAO Temporal", &settings.ssao.temporalEnabled);
        pipelineChanged |= ImGui::Checkbox("SSGI", &settings.ssgi.enabled);
        ImGui::SameLine();
        if (ImGui::Button("SSGI View")) {
            settings.debug.viewMode = 81;
            debugViewMode = 81;
            pipelineChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("SSGI Strong Test")) {
            settings.ssgi.enabled = true;
            settings.ssgi.temporalEnabled = true;
            settings.ssgi.denoiseEnabled = true;
            settings.ssgi.historyWeight = 0.86f;
            settings.ssgi.radius = 8.0f;
            settings.ssgi.strength = 2.2f;
            settings.ssgi.maxDistance = 24.0f;
            settings.ssgi.thickness = 2.2f;
            settings.ssgi.denoiseStrength = 0.92f;
            settings.ssgi.radianceFilterStrength = 0.65f;
            settings.ssgi.colorBleedStrength = 0.42f;
            settings.ssgi.samples = 16;
            settings.ssgi.denoiseIterations = 3;
            settings.debug.viewMode = 0;
            debugViewMode = 0;
            pipelineChanged = true;
        }
        pipelineChanged |= ImGui::Checkbox("SSGI Temporal", &settings.ssgi.temporalEnabled);
        pipelineChanged |= ImGui::Checkbox("SSGI Denoise", &settings.ssgi.denoiseEnabled);
        pipelineChanged |= ImGui::Checkbox("Voxel GI", &settings.voxelGi.enabled);
        ImGui::SameLine();
        pipelineChanged |= ImGui::Checkbox("Voxel GI Debug", &settings.voxelGi.debugEnabled);
        ImGui::SameLine();
        if (ImGui::Button("Voxel GI Test")) {
            settings.voxelGi.enabled = true;
            settings.voxelGi.debugEnabled = false;
            settings.voxelGi.strength = 0.48f;
            settings.voxelGi.resolution = 64;
            settings.voxelGi.updateInterval = 3;
            settings.voxelGi.coneSteps = 6;
            settings.voxelGi.originSnap = 8;
            settings.voxelGi.voxelSize = 1.0f;
            settings.voxelGi.normalBias = 0.45f;
            settings.voxelGi.sampleDistance = 1.5f;
            settings.voxelGi.traceDistance = 18.0f;
            settings.voxelGi.coneAperture = 0.55f;
            settings.voxelGi.occupancyScale = 0.55f;
            settings.voxelGi.occlusionStrength = 1.55f;
            settings.voxelGi.skyBounceStrength = 0.80f;
            settings.voxelGi.sunBounceStrength = 1.35f;
            settings.voxelGi.receiverShadowBoost = 0.95f;
            pipelineChanged = true;
        }
        pipelineChanged |= ImGui::Checkbox("FSR1 Upscale", &settings.upscale.fsr1Enabled);
        pipelineChanged |= ImGui::SliderFloat("FSR1 Render Scale", &settings.upscale.renderScale, 0.50f, 1.00f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("FSR1 Sharpness", &settings.upscale.sharpness, 0.00f, 2.00f, "%.2f");
        pipelineChanged |= ImGui::Checkbox("Bloom Flag", &settings.postProcess.bloomEnabled);
        pipelineChanged |= ImGui::SliderInt("Bloom Mips", &settings.postProcess.bloomMipCount, 1, 7);
        pipelineChanged |= ImGui::SliderFloat("Bloom Threshold", &settings.postProcess.bloomThreshold, 0.0f, 3.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Bloom Amount", &settings.postProcess.bloomStrength, 0.0f, 20.0f, "%.2f");
        pipelineChanged |= ImGui::Checkbox("Depth of Field", &settings.postProcess.dofEnabled);
        pipelineChanged |= ImGui::SliderFloat("DoF Focus", &settings.postProcess.dofFocusDistance, 0.5f, 50.0f, "%.1f blocks");
        pipelineChanged |= ImGui::SliderFloat("DoF Aperture", &settings.postProcess.dofAperture, 0.8f, 22.0f, "%.1f");
        pipelineChanged |= ImGui::SliderFloat("DoF Intensity", &settings.postProcess.dofIntensity, 0.0f, 1.0f, "%.3f");
        pipelineChanged |= ImGui::Checkbox("Auto Exposure", &settings.postProcess.autoExposureEnabled);
        pipelineChanged |= ImGui::SliderFloat("Auto Exp Min", &settings.postProcess.autoExposureMin, 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        pipelineChanged |= ImGui::SliderFloat("Auto Exp Max", &settings.postProcess.autoExposureMax, 1.0f, 64.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::TextDisabled("DerivativeMain exposure target is unclamped; min/max are legacy UI fields.");
        pipelineChanged |= ImGui::SliderFloat("Auto Exp Speed", &settings.postProcess.autoExposureSpeed, 0.1f, 6.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Auto Exp Bias", &settings.postProcess.autoExposureBias, -2.0f, 2.0f, "%.2f EV");
        pipelineChanged |= ImGui::Checkbox("Sun Rays", &settings.postProcess.sunRaysEnabled);
        pipelineChanged |= ImGui::Checkbox("Water Effects", &settings.transparent.waterEffectsEnabled);
        pipelineChanged |= ImGui::Checkbox("Transparent Composite", &settings.transparent.compositeEnabled);
        pipelineChanged |= ImGui::SliderFloat("Scene Cloud Composite", &settings.cloud.sceneCloudCompositeStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Scene Reflection Composite", &settings.reflection.sceneReflectionCompositeStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::Checkbox("Shaderpack Grading", &settings.postProcess.shaderpackGradingEnabled);
        pipelineChanged |= ImGui::Checkbox("Purkinje Shift", &settings.postProcess.purkinjeShiftEnabled);
        pipelineChanged |= ImGui::Checkbox("Bloomy Fog", &settings.postProcess.bloomyFogEnabled);
        pipelineChanged |= ImGui::Checkbox("Aerial Perspective", &settings.postProcess.aerialPerspectiveEnabled);
        pipelineChanged |= ImGui::Checkbox("Volumetric Light", &settings.volumetric.lightEnabled);
        pipelineChanged |= ImGui::Checkbox("Volumetric Fog", &settings.volumetric.fogEnabled);
        pipelineChanged |= ImGui::Checkbox("VFog Sky Ray March", &settings.volumetric.skyRayEnabled);
        pipelineChanged |= ImGui::Checkbox("VFog TIME_FADE", &settings.volumetric.timeFadeEnabled);
        pipelineChanged |= ImGui::Checkbox("VFog Temporal Accumulation", &settings.volumetric.temporalEnabled);
        pipelineChanged |= ImGui::SliderInt("VFog Update Frames", &settings.volumetric.updateInterval, 1, 4);
        pipelineChanged |= ImGui::SliderFloat("VFog Temporal Weight", &settings.volumetric.temporalWeight, 0.0f, 0.99f, "%.2f");
        pipelineChanged |= ImGui::Checkbox("UW Volumetric Light", &settings.volumetric.uwLightEnabled);
        static constexpr const char* kVFogQualityTiers[] = {
            "Low: No Noise",
            "Medium: Cloudy Fog Lite",
            "High: Cloudy Fog",
            "Ultra: Cloudy Sea"
        };
        int qualityTier = settings.volumetric.qualityTier;
        pipelineChanged |= ImGui::Combo("VFog Fog Type", &qualityTier, kVFogQualityTiers, IM_ARRAYSIZE(kVFogQualityTiers));
        settings.volumetric.qualityTier = qualityTier;
        int fogSamples = settings.volumetric.fogSamples;
        pipelineChanged |= ImGui::SliderInt("VFog Samples", &fogSamples, 2, 50);
        settings.volumetric.fogSamples = fogSamples;
        // DerivativeMain-style VFog independent profile controls
        pipelineChanged |= ImGui::SliderFloat("VFog Center Height", &settings.volumetric.fogCenterHeight, 0.0f, 255.0f, "%.0f");
        pipelineChanged |= ImGui::SliderFloat("VFog Height Spread", &settings.volumetric.fogHeightSpread, 1.0f, 200.0f, "%.0f");
        pipelineChanged |= ImGui::SliderFloat("VFog Noise Scale", &settings.volumetric.fogNoiseScale, 0.001f, 0.200f, "%.3f");
        pipelineChanged |= ImGui::SliderFloat("VFog Light Strength", &settings.volumetric.fogLightStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("VFog Density Scale", &settings.volumetric.fogDensityScale, 0.0f, 10.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("UW VL Strength", &settings.volumetric.underwaterLightStrength, 0.0f, 2.0f, "%.2f");
        if (ImGui::Combo("Weather Instant (Debug)", &weatherPresetInstant, kWeatherPresets, IM_ARRAYSIZE(kWeatherPresets))) {
            world.getWeatherSystem().setDebugWeatherPresetInstant(static_cast<WeatherType>(weatherPresetInstant));
        }
        if (ImGui::Combo("Weather Smooth (Debug)", &weatherPresetSmooth, kWeatherPresets, IM_ARRAYSIZE(kWeatherPresets))) {
            world.getWeatherSystem().setDebugWeatherPresetSmooth(static_cast<WeatherType>(weatherPresetSmooth));
        }
        pipelineChanged |= ImGui::Combo("Tonemap Mode", &tonemapMode, kTonemapModes, IM_ARRAYSIZE(kTonemapModes));
        settings.postProcess.tonemapMode = tonemapMode;
        // Exposure diagnostics
        {
            float resolvedExposure = 0.8f / std::max(settings.postProcess.exposure, 0.0001f);
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Exposure Diagnostics");
            if (settings.postProcess.autoExposureEnabled) {
                ImGui::Text("Auto Exposure: %s", postProcess.isAutoExposureGpuResolved() ? "GPU texture" : "initializing");
                ImGui::TextDisabled("CPU readback diagnostics are disabled for benchmark stability.");
            } else {
                ImGui::Text("Resolved Exposure: %.4f", resolvedExposure);
                ImGui::Text("Manual Exposure: %.2f (1/exposure=%.4f)", settings.postProcess.exposure, resolvedExposure);
            }
            // SkyCapture metadata
            auto skyLux = renderScene.getSkyIlluminanceData();
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "SkyCapture Metadata (LUT units)");
            ImGui::Text("Direct: (%.6f, %.6f, %.6f)", skyLux.directIlluminance.r, skyLux.directIlluminance.g, skyLux.directIlluminance.b);
            ImGui::Text("Sky:   (%.6f, %.6f, %.6f)", skyLux.skyIlluminance.r, skyLux.skyIlluminance.g, skyLux.skyIlluminance.b);
            ImGui::Text("Sun:   (%.6f, %.6f, %.6f)", skyLux.sunIlluminance.r, skyLux.sunIlluminance.g, skyLux.sunIlluminance.b);
            ImGui::Text("Moon:  (%.6f, %.6f, %.6f)", skyLux.moonIlluminance.r, skyLux.moonIlluminance.g, skyLux.moonIlluminance.b);
            // Lighting input diagnostic — compare CPU art colors vs SkyCapture metadata
            auto skyColors = renderScene.getSkyColors();
            auto fogColor = renderScene.getFogColor();
            ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.4f, 1.0f), "Lighting Input Diagnostic");
            ImGui::Text("SunLightColor(CPU): (%.2f, %.2f, %.2f)", skyColors.sunLightColor.r, skyColors.sunLightColor.g, skyColors.sunLightColor.b);
            ImGui::Text("SkyAmbientColor(CPU): (%.2f, %.2f, %.2f)", skyColors.skyAmbientColor.r, skyColors.skyAmbientColor.g, skyColors.skyAmbientColor.b);
            ImGui::Text("FogColor(CPU):      (%.2f, %.2f, %.2f)", fogColor.r, fogColor.g, fogColor.b);
            ImGui::Text("HorizonScatter(CPU): (%.2f, %.2f, %.2f)", skyColors.horizonScatterColor.r, skyColors.horizonScatterColor.g, skyColors.horizonScatterColor.b);
            ImGui::Text("DirectSunStrength: %.2f  SkyAmbientStrength: %.2f", settings.postProcess.directSunStrength, settings.postProcess.skyAmbientStrength);
            ImGui::Text("SunWarmth: %.2f  SkyCoolness: %.2f", settings.postProcess.sunWarmth, settings.postProcess.skyCoolness);
            // Effective after-tint values (what passes actually use)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Effective After Tint");
            // coolSkyColor = mix(skyAmbient, skyAmbient * coolness tint, skyCoolness)
            float coolR = skyColors.skyAmbientColor.r * (1.0f - settings.postProcess.skyCoolness * 0.22f);
            float coolG = skyColors.skyAmbientColor.g * (1.0f + settings.postProcess.skyCoolness * 0.08f);
            float coolB = skyColors.skyAmbientColor.b * (1.0f + settings.postProcess.skyCoolness * 0.18f);
            ImGui::Text("coolSkyColor: (%.2f, %.2f, %.2f)", coolR, coolG, coolB);
            ImGui::Text("Cloud cirrus: env.sunIlluminance * 40.0");
            // VFog component diagnostics (CPU approximate — actual values are GPU-side)
            ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "Volumetric Fog Diagnostics (approx)");
            ImGui::Text("Volumetric Light (haze): %s", settings.volumetric.lightEnabled ? "ON" : "OFF");
            ImGui::Text("VFog Temporal: %s (weight: %.2f)", settings.volumetric.temporalEnabled ? "ON" : "OFF", settings.volumetric.temporalWeight);
            const auto& weather = world.getWeatherSystem().getRenderState();
            const auto targetWeather = world.getWeatherSystem().getTargetState();
            const auto& derivedWeather = world.getWeatherSystem().getDerived();
            float sunY = skyColors.sunDirection.y;
            float sunVis = (sunY > -0.08f) ? std::min((sunY + 0.08f) / 0.26f, 1.0f) : 0.0f;
            ImGui::Text("env.sunIlluminance: (%.2f, %.2f, %.2f)", skyLux.sunIlluminance.r, skyLux.sunIlluminance.g, skyLux.sunIlluminance.b);
            ImGui::Text("env.skyIlluminance:  (%.2f, %.2f, %.2f)", skyLux.skyIlluminance.r, skyLux.skyIlluminance.g, skyLux.skyIlluminance.b);
            ImGui::Text("sunVisibility(CPU): %.3f  (sunDir.y=%.2f)", sunVis, sunY);
            ImGui::Text("VFog strength: %.2f", settings.volumetric.fogStrength);
            ImGui::Text("VFog profile: center=%.0f  spread=%.0f  noise=%.3f  light=%.2f  density=%.2f",
                settings.volumetric.fogCenterHeight, settings.volumetric.fogHeightSpread, settings.volumetric.fogNoiseScale,
                settings.volumetric.fogLightStrength, settings.volumetric.fogDensityScale);
            ImGui::Text("VFog baseDensity: 1.0  (VolumetricSettings defaults)");
            const char* tierNames[] = {"Low(0.5x)", "Medium(1.4x)", "High(9.0x)", "Ultra(48.0x)"};
            ImGui::Text("VFog tier: %s  maxDist: 260  heightSpread: %.0f", tierNames[qualityTier], settings.volumetric.fogHeightSpread);
            {
                float meFade = (sunY < 0.18f) ? 0.37f + 1.2f * std::max(0.0f, -sunY) : 1.7f;
                float meWeight = std::pow(std::clamp(1.0f - meFade * std::abs(sunY - 0.18f), 0.0f, 1.0f), 2.0f);
                float timeMidnight = (sunY < 0.0f ? 1.0f : 0.0f) * (1.0f - meWeight);
                float wetness = derivedWeather.skyWetness;
                // DerivativeMain VolumetricFog.glsl:210-213: airDensity and mistDensity both get TIME_FADE
                // with max(..., wetness) floor so rain never fully suppresses fog.
                float airGate = settings.volumetric.timeFadeEnabled
                    ? std::max(std::clamp(meWeight + 0.25f, 0.0f, 1.0f) + timeMidnight * 4.0f, wetness)
                    : 1.0f;
                float mistGate = settings.volumetric.timeFadeEnabled
                    ? std::max(meWeight * meWeight + timeMidnight * 2.0f, wetness)
                    : 1.0f;
                float tierMultiplier = qualityTier <= 0 ? 0.5f : (qualityTier <= 1 ? 1.4f : (qualityTier <= 2 ? 9.0f : 48.0f));
                ImGui::Text("VFog TIME_FADE gates: %s  air=%.3f mist=%.3f wet=%.2f",
                    settings.volumetric.timeFadeEnabled ? "ON" : "OFF", airGate, mistGate, wetness);
                ImGui::Text("VFog effective tier density: %.2f x mistGate = %.3f", tierMultiplier, tierMultiplier * mistGate);
                if (weather.type == WeatherType::Clear && mistGate < 0.02f) {
                    ImGui::TextDisabled("Clear noon: DerivativeMain TIME_FADE suppresses mist density; FOG_TYPE mostly affects density shape.");
                }
            }
            // Active light direction (shadow system uses this)
            ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.5f, 1.0f), "Active Light");
            ImGui::Text("SunDir: (%.2f, %.2f, %.2f)  MoonDir: (%.2f, %.2f, %.2f)",
                skyColors.sunDirection.x, skyColors.sunDirection.y, skyColors.sunDirection.z,
                skyColors.moonDirection.x, skyColors.moonDirection.y, skyColors.moonDirection.z);
            ImGui::Text("SunVis: %.3f  MoonVis: %.6f  DayFactor: %.3f",
                skyColors.sunVisibility,
                skyColors.moonVisibility,
                world.getDayNightSystem().getSkyIntensity());
            // Time of day slider (0-1200 seconds, 20 min cycle)
            {
                auto& dayNight = world.getDayNightSystem();
                float tod = dayNight.getTimeOfDay();
                if (ImGui::SliderFloat("Time of Day", &tod, 0.0f, 1200.0f, "%.0f s")) {
                    dayNight.setTimeOfDay(tod);
                }
            }
            // DerivativeMain time weights (shader-side, from sun direction)
            {
                float sunY = skyColors.sunDirection.y;
                float sunX = skyColors.sunDirection.x;
                float meFade = (sunY < 0.18f) ? 0.37f + 1.2f * std::max(0.0f, -sunY) : 1.7f;
                float meWeight = std::pow(std::clamp(1.0f - meFade * std::abs(sunY - 0.18f), 0.0f, 1.0f), 2.0f);
                float timeNoon = (sunY > 0.0f ? 1.0f : 0.0f) * (1.0f - meWeight);
                float timeMidnight = (sunY < 0.0f ? 1.0f : 0.0f) * (1.0f - meWeight);
                float timeSunrise = (sunX > 0.0f ? 1.0f : 0.0f) * meWeight;
                float timeSunset = (sunX < 0.0f ? 1.0f : 0.0f) * meWeight;
                ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.5f, 1.0f), "Derivative Time Weights");
                ImGui::Text("Noon: %.3f  Midnight: %.3f  Sunrise: %.3f  Sunset: %.3f",
                    timeNoon, timeMidnight, timeSunrise, timeSunset);
                ImGui::Text("meWeight: %.3f  (sunY=%.3f, sunX=%.3f)", meWeight, sunY, sunX);
                ImGui::Text("CPU DayFactor: %.3f  vs  GPU meWeight: %.3f",
                    world.getDayNightSystem().getSkyIntensity(), meWeight);
            }
            // Weather state
            const char* tonemapNames[] = {
                "Reinhard [Mecraft]",
                "AcademyFit [DerivMain]",
                "Filmic [Mecraft]",
                "AgX_Minimal [DerivMain]",
                "AcademyFull [DerivMain]",
                "AgX_Full [DerivMain]"
            };
            ImGui::Text("Tonemap: %s", tonemapNames[std::clamp(settings.postProcess.tonemapMode, 0, 5)]);
            const char* weatherNames[] = {"Clear", "Rain", "Storm", "Snow"};
            ImGui::Text("Weather current: %s  wet=%.2f storm=%.2f aerialRed=%.2f",
                weatherNames[static_cast<int>(weather.type)],
                weather.wetness, weather.storm, weather.aerialReduction);
            ImGui::Text("Weather target:  %s  wet=%.2f storm=%.2f aerialRed=%.2f",
                weatherNames[static_cast<int>(targetWeather.type)],
                targetWeather.wetness, targetWeather.storm, targetWeather.aerialReduction);
            {
                float overcastShadow = 1.0f + (0.03f - 1.0f) * derivedWeather.skyWetness;
                ImGui::Text("Derived weather: sky=%.2f fog=%.2f cloud=%.2f surface=%.2f rain=%.2f",
                    derivedWeather.skyWetness,
                    derivedWeather.fogWetness,
                    derivedWeather.cloudWetness,
                    derivedWeather.surfaceWetness,
                    derivedWeather.rainStrength);
                ImGui::Text("Weather direct shadow: %.3f  (mix(1.0, 0.03, derived.skyWetness))",
                    overcastShadow);
                pipelineChanged |= ImGui::SliderFloat("Direct Occlusion Override", &settings.weather.directWeatherOcclusion, -1.0f, 1.0f, "%.2f (<0=auto, >=0=bypass all cloud shadow)");
                ImGui::TextDisabled("SkyCapture metadata stays weather-independent; direct rain dimming happens in deferred cloudShadow.");
            }
            // Weather debug overrides — multiply against DerivativeMain formula.
            // Default values (1.0 / 0.0) = no override, use DerivativeMain contract.
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.5f, 1.0f), "Weather Debug Override");
                ImGui::TextDisabled("Multiplies against DerivativeMain formula. 1.0/0.0 = no override.");
                pipelineChanged |= ImGui::SliderFloat("Skylight Scale##weather", &settings.weather.skylightScale, 0.0f, 1.0f, "%.2f (x DerivMain)");
                pipelineChanged |= ImGui::SliderFloat("Exposure Bias##weather", &settings.weather.exposureBias, -2.0f, 2.0f, "%.2f EV");
                pipelineChanged |= ImGui::SliderFloat("Post Rain Fog##weather", &settings.weather.postRainFog, 0.0f, 2.0f, "%.2f (x DerivMain)");
                pipelineChanged |= ImGui::SliderFloat("Rain Alpha Scale##weather", &settings.weather.rainAlphaScale, 0.0f, 5.0f, "%.2f");
                pipelineChanged |= ImGui::Checkbox("Weather Rain Lines", &settings.weather.rainLinesEnabled);
                pipelineChanged |= ImGui::Checkbox("Scene Particles", &settings.weather.particlesEnabled);
                pipelineChanged |= ImGui::Checkbox("Rain Wet Surfaces", &settings.weather.wetSurfacesEnabled);
                pipelineChanged |= ImGui::Checkbox("Rain Surface Ripples", &settings.weather.surfaceRipplesEnabled);
            }
            ImGui::Separator();
        }
        ImGui::TextUnformatted("Shadow Projection: CSM Linear");
        if (settings.debug.disableGreedyMeshing) {
            settings.debug.disableGreedyMeshing = false;
            for (const auto& [chunkKey, chunk] : world.getActiveChunks()) {
                (void)chunkKey;
                if (chunk) {
                    chunk->markExistingSubChunksDirty();
                }
            }
            pipelineChanged = true;
        }
        if (ImGui::Button("Preset Neutral")) {
            settings.postProcess.tonemapMode = 1;

            settings.shadow.softShadowsEnabled = true;
            settings.shadow.pcssShadowsEnabled = false;
            settings.shadow.contactShadowsEnabled = false;
            settings.cloud.shadowsEnabled = false;
            settings.cloud.updateInterval = 2;
            settings.cloud.timeScale = 0.35f;
            settings.postProcess.directSunStrength = 1.0f;
            settings.postProcess.skyAmbientStrength = 0.55f;
            settings.postProcess.minimumAmbient = 0.09f;
            settings.postProcess.shadowMinLight = 0.18f;
            settings.postProcess.shadowContrast = 1.0f;
            settings.postProcess.shadowTintStrength = 0.18f;
            settings.postProcess.blockLightStrength = 1.0f;
            settings.postProcess.fakeBounceStrength = 0.04f;
            world.getWeatherSystem().setDebugWeatherPreset(WeatherType::Clear);
            settings.postProcess.aerialStrength = 0.25f;
            settings.postProcess.horizonScatterStrength = 0.35f;
            settings.volumetric.fogStrength = 0.0f;
            settings.volumetric.fogDensityScale = 0.0f;
            settings.volumetric.updateInterval = 1;
            settings.postProcess.bloomThreshold = 1.05f;
            settings.postProcess.bloomStrength = 0.10f;
            settings.postProcess.bloomMipCount = 3;
            settings.postProcess.sunRaysEnabled = false;
            settings.postProcess.dofEnabled = false;
            settings.postProcess.autoExposureEnabled = false;
            settings.postProcess.autoExposureMin = 0.70f;
            settings.postProcess.autoExposureMax = 1.40f;
            settings.postProcess.autoExposureSpeed = 1.0f;
            settings.postProcess.autoExposureBias = 0.0f;
            settings.postProcess.exposure = 12.0f;
            settings.postProcess.vibrance = 0.0f;
            settings.postProcess.highlightCompression = 0.35f;
            settings.postProcess.filmEmulationStrength = 0.0f;
            settings.postProcess.redModifierStrength = 0.0f;
            settings.postProcess.colorLumaR = 1.0f;
            settings.postProcess.colorLumaG = 1.0f;
            settings.postProcess.colorLumaB = 1.0f;
            settings.postProcess.albedoDesaturation = 0.0f;
            settings.postProcess.sunWarmth = 0.0f;
            settings.postProcess.skyCoolness = 0.0f;
            settings.postProcess.shadowDesaturation = 0.0f;
            settings.postProcess.splitToneStrength = 0.0f;
            settings.postProcess.vignetteStrength = 0.0f;
            settings.postProcess.sharpenStrength = 0.0f;
            settings.postProcess.saturation = 1.0f;
            settings.postProcess.contrast = 1.0f;
            pipelineChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Preset Natural")) {
            settings.postProcess.tonemapMode = 1;

            settings.shadow.softShadowsEnabled = true;
            settings.shadow.pcssShadowsEnabled = true;
            settings.shadow.contactShadowsEnabled = false;
            settings.shadow.pcssStrength = 0.72f;
            settings.cloud.shadowsEnabled = false;  // DerivativeMain CLOUDS_SHADOW default off
            settings.cloud.updateInterval = 2;
            settings.postProcess.directSunStrength = 1.36f;
            settings.postProcess.skyAmbientStrength = 0.36f;
            settings.postProcess.minimumAmbient = 0.055f;
            settings.shadow.contactShadowStrength = 0.12f;
            settings.cloud.shadowStrength = 0.28f;
            settings.cloud.shadowScale = 0.0045f;
            settings.cloud.shadowSpeed = 0.018f;
            settings.cloud.timeScale = 0.35f;
            settings.postProcess.shadowMinLight = 0.08f;
            settings.postProcess.shadowContrast = 1.28f;
            settings.postProcess.shadowTintStrength = 0.28f;
            settings.postProcess.blockLightStrength = 1.0f;
            settings.postProcess.fakeBounceStrength = 0.06f;
            world.getWeatherSystem().setDebugWeatherPreset(WeatherType::Clear);
            settings.postProcess.aerialStrength = 0.48f;
            settings.postProcess.horizonScatterStrength = 0.70f;
            settings.volumetric.fogStrength = 1.0f;
            settings.volumetric.updateInterval = 1;
            settings.postProcess.bloomThreshold = 0.0f;
            settings.postProcess.bloomStrength = 1.0f;
            settings.postProcess.bloomMipCount = 5;
            settings.postProcess.sunRaysEnabled = false;
            settings.postProcess.dofEnabled = false;
            settings.postProcess.autoExposureEnabled = true;
            settings.postProcess.autoExposureMin = 0.001f;
            settings.postProcess.autoExposureMax = 64.0f;
            settings.postProcess.autoExposureSpeed = 1.0f;
            settings.postProcess.autoExposureBias = 0.0f;
            settings.postProcess.exposure = 12.0f;
            settings.postProcess.vibrance = 0.0f;
            settings.postProcess.highlightCompression = 0.0f;
            settings.postProcess.filmEmulationStrength = 0.0f;
            settings.postProcess.redModifierStrength = 0.35f;
            settings.postProcess.colorLumaR = 1.02f;
            settings.postProcess.colorLumaG = 1.0f;
            settings.postProcess.colorLumaB = 0.96f;
            settings.postProcess.albedoDesaturation = 0.0f;
            settings.postProcess.sunWarmth = 0.34f;
            settings.postProcess.skyCoolness = 0.18f;
            settings.postProcess.shadowDesaturation = 0.22f;
            settings.postProcess.splitToneStrength = 0.0f;
            settings.postProcess.vignetteStrength = 0.0f;
            settings.postProcess.sharpenStrength = 0.3f;
            settings.postProcess.saturation = 1.0f;
            settings.postProcess.contrast = 1.0f;
            pipelineChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Preset Contrast")) {
            settings.postProcess.tonemapMode = 1;

            settings.shadow.softShadowsEnabled = true;
            settings.shadow.pcssShadowsEnabled = true;
            settings.shadow.contactShadowsEnabled = false;
            settings.shadow.pcssStrength = 0.82f;
            settings.cloud.shadowsEnabled = false;  // DerivativeMain CLOUDS_SHADOW default off
            settings.cloud.updateInterval = 2;
            settings.postProcess.directSunStrength = 1.58f;
            settings.postProcess.skyAmbientStrength = 0.28f;
            settings.postProcess.minimumAmbient = 0.04f;
            settings.shadow.contactShadowStrength = 0.16f;
            settings.cloud.shadowStrength = 0.28f;
            settings.cloud.shadowScale = 0.0055f;
            settings.cloud.shadowSpeed = 0.020f;
            settings.cloud.timeScale = 0.35f;
            settings.postProcess.shadowMinLight = 0.055f;
            settings.postProcess.shadowContrast = 1.52f;
            settings.postProcess.shadowTintStrength = 0.34f;
            settings.postProcess.blockLightStrength = 1.05f;
            settings.postProcess.fakeBounceStrength = 0.08f;
            world.getWeatherSystem().setDebugWeatherPreset(WeatherType::Clear);
            settings.postProcess.aerialStrength = 0.58f;
            settings.postProcess.horizonScatterStrength = 0.82f;
            settings.volumetric.fogStrength = 1.0f;
            settings.volumetric.updateInterval = 1;
            settings.postProcess.bloomThreshold = 0.0f;
            settings.postProcess.bloomStrength = 1.0f;
            settings.postProcess.bloomMipCount = 5;
            settings.postProcess.sunRaysEnabled = false;
            settings.postProcess.dofEnabled = false;
            settings.postProcess.autoExposureEnabled = true;
            settings.postProcess.autoExposureMin = 0.001f;
            settings.postProcess.autoExposureMax = 64.0f;
            settings.postProcess.autoExposureSpeed = 1.0f;
            settings.postProcess.autoExposureBias = 0.0f;
            settings.postProcess.exposure = 12.0f;
            settings.postProcess.vibrance = 0.04f;
            settings.postProcess.highlightCompression = 0.0f;
            settings.postProcess.filmEmulationStrength = 0.0f;
            settings.postProcess.redModifierStrength = 0.45f;
            settings.postProcess.colorLumaR = 1.04f;
            settings.postProcess.colorLumaG = 1.0f;
            settings.postProcess.colorLumaB = 0.93f;
            settings.postProcess.albedoDesaturation = 0.0f;
            settings.postProcess.sunWarmth = 0.48f;
            settings.postProcess.skyCoolness = 0.24f;
            settings.postProcess.shadowDesaturation = 0.34f;
            settings.postProcess.splitToneStrength = 0.0f;
            settings.postProcess.vignetteStrength = 0.0f;
            settings.postProcess.sharpenStrength = 0.3f;
            settings.postProcess.saturation = 1.0f;
            settings.postProcess.contrast = 1.06f;
            pipelineChanged = true;
        }
        pipelineChanged |= ImGui::SliderInt("Shadow Resolution", &settings.shadow.resolution, 512, 4096);
        pipelineChanged |= ImGui::SliderFloat("Shadow Distance", &settings.shadow.distance, 64.0f, 192.0f, "%.1f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Softness", &settings.shadow.softness, 0.1f, 4.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("PCSS Strength", &settings.shadow.pcssStrength, 0.0f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Const Bias", &settings.shadow.constantBias, 0.0f, 0.004f, "%.4f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Slope Bias", &settings.shadow.slopeBias, 0.0f, 0.012f, "%.4f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Normal Offset", &settings.shadow.normalOffset, 0.0f, 0.12f, "%.3f");
        pipelineChanged |= ImGui::SliderFloat("Contact Shadow Strength", &settings.shadow.contactShadowStrength, 0.0f, 0.6f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Cloud Shadow Strength", &settings.cloud.shadowStrength, 0.0f, 0.8f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Cloud Shadow Scale", &settings.cloud.shadowScale, 0.001f, 0.02f, "%.4f");
        pipelineChanged |= ImGui::SliderFloat("Cloud Time Scale", &settings.cloud.timeScale, 0.05f, 2.0f, "%.2f");
        pipelineChanged |= ImGui::SliderInt("Cloud Update Frames", &settings.cloud.updateInterval, 1, 4);
        ImGui::TextDisabled("DerivativeMain CLOUDS_SPEED adapter. Legacy Cloud Shadow Speed is ignored by the DM cloud path.");
        pipelineChanged |= ImGui::SliderFloat("Post Sun Ray Strength", &settings.postProcess.sunRayStrength, 0.0f, 0.6f, "%.2f");
        ImGui::TextDisabled("VFog Strength 1.00 matches DerivativeMain VOLUMETRIC_FOG_DENSITY baseline");
        pipelineChanged |= ImGui::SliderFloat("VFog Shadow Bias Scale", &settings.volumetric.shadowBiasScale, 0.0f, 4.0f, "%.2f");
        // A/B test toggles for TAA/VFog temporal convergence diagnosis
        ImGui::Separator();
        ImGui::TextDisabled("TAA/VFog A/B Test");
        pipelineChanged |= ImGui::Checkbox("Freeze R1 Dither", &settings.volumetric.freezeR1);
        pipelineChanged |= ImGui::Checkbox("Freeze Upscale Bias", &settings.volumetric.freezeBias);
        pipelineChanged |= ImGui::Checkbox("TAA", &settings.taa.enabled);
        pipelineChanged |= ImGui::Checkbox("Force Zero Velocity", &settings.taa.forceZeroVelocity);
        pipelineChanged |= ImGui::Checkbox("Freeze TAA Jitter", &settings.taa.freezeJitter);
        pipelineChanged |= ImGui::SliderFloat("Color Temperature", &settings.postProcess.colorTemperature, 0.0f, 2.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Vibrance", &settings.postProcess.vibrance, -0.5f, 0.8f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Highlight Compress", &settings.postProcess.highlightCompression, 0.0f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Film Emulation", &settings.postProcess.filmEmulationStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Red Modifier", &settings.postProcess.redModifierStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Channel R", &settings.postProcess.colorLumaR, 0.5f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Channel G", &settings.postProcess.colorLumaG, 0.5f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Channel B", &settings.postProcess.colorLumaB, 0.5f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Albedo Desat", &settings.postProcess.albedoDesaturation, 0.0f, 0.8f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Sun Warmth", &settings.postProcess.sunWarmth, 0.0f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Sky Coolness", &settings.postProcess.skyCoolness, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Desat", &settings.postProcess.shadowDesaturation, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Split Tone", &settings.postProcess.splitToneStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Vignette", &settings.postProcess.vignetteStrength, 0.0f, 0.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Tint", &settings.postProcess.shadowTintStrength, 0.0f, 0.8f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Direct Sun", &settings.postProcess.directSunStrength, 0.0f, 3.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Sky Ambient", &settings.postProcess.skyAmbientStrength, 0.0f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Minimum Ambient", &settings.postProcess.minimumAmbient, 0.0f, 0.4f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Min Light", &settings.postProcess.shadowMinLight, 0.0f, 0.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Contrast", &settings.postProcess.shadowContrast, 0.5f, 2.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Block Light", &settings.postProcess.blockLightStrength, 0.0f, 2.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Fake Bounce", &settings.postProcess.fakeBounceStrength, 0.0f, 0.3f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Aerial Strength", &settings.postProcess.aerialStrength, 0.0f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Horizon Scatter", &settings.postProcess.horizonScatterStrength, 0.0f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Volumetric Fog Strength", &settings.volumetric.fogStrength, 0.0f, 2.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Noise Dither", &settings.postProcess.noiseDitherStrength, 0.0f, 0.05f, "%.3f");
        pipelineChanged |= ImGui::SliderFloat("CAS Sharpen", &settings.postProcess.sharpenStrength, 0.0f, 0.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("SSAO Radius", &settings.ssao.radius, 0.25f, 8.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("SSAO Strength", &settings.ssao.strength, 0.0f, 2.0f, "%.2f");
        pipelineChanged |= ImGui::SliderInt("SSAO Samples", &settings.ssao.samples, 1, 64);
        pipelineChanged |= ImGui::SliderFloat("SSAO History Weight", &settings.ssao.historyWeight, 0.0f, 0.98f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("SSGI Radius", &settings.ssgi.radius, 0.5f, 24.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("SSGI Strength", &settings.ssgi.strength, 0.0f, 4.0f, "%.2f");
        pipelineChanged |= ImGui::SliderInt("SSGI Samples", &settings.ssgi.samples, 1, 32);
        pipelineChanged |= ImGui::SliderFloat("SSGI Max Distance", &settings.ssgi.maxDistance, 1.0f, 48.0f, "%.1f");
        pipelineChanged |= ImGui::SliderFloat("SSGI Thickness", &settings.ssgi.thickness, 0.1f, 8.0f, "%.2f");
        pipelineChanged |= ImGui::SliderInt("SSGI Denoise Passes", &settings.ssgi.denoiseIterations, 0, 4);
        pipelineChanged |= ImGui::SliderFloat("SSGI Denoise Strength", &settings.ssgi.denoiseStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("SSGI Radiance Filter", &settings.ssgi.radianceFilterStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("SSGI Color Bleed", &settings.ssgi.colorBleedStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("SSGI History Weight", &settings.ssgi.historyWeight, 0.0f, 0.98f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Voxel GI Strength", &settings.voxelGi.strength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderInt("Voxel GI Resolution", &settings.voxelGi.resolution, 16, 128);
        pipelineChanged |= ImGui::SliderInt("Voxel GI Update Frames", &settings.voxelGi.updateInterval, 1, 20);
        pipelineChanged |= ImGui::SliderInt("Voxel GI Cone Steps", &settings.voxelGi.coneSteps, 1, 12);
        pipelineChanged |= ImGui::SliderInt("Voxel GI Origin Snap", &settings.voxelGi.originSnap, 1, 32);
        pipelineChanged |= ImGui::SliderFloat("Voxel GI Voxel Size", &settings.voxelGi.voxelSize, 1.0f, 4.0f, "%.0f");
        pipelineChanged |= ImGui::SliderFloat("Voxel GI Normal Bias", &settings.voxelGi.normalBias, 0.0f, 2.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Voxel GI Sample Distance", &settings.voxelGi.sampleDistance, 1.0f, 12.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Voxel GI Trace Distance", &settings.voxelGi.traceDistance, 4.0f, 48.0f, "%.1f");
        pipelineChanged |= ImGui::SliderFloat("Voxel GI Cone Aperture", &settings.voxelGi.coneAperture, 0.05f, 1.50f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Voxel GI Occupancy Scale", &settings.voxelGi.occupancyScale, 0.0f, 2.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Voxel GI Occlusion Strength", &settings.voxelGi.occlusionStrength, 0.0f, 4.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Voxel GI Sky Bounce", &settings.voxelGi.skyBounceStrength, 0.0f, 2.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Voxel GI Sun Bounce", &settings.voxelGi.sunBounceStrength, 0.0f, 3.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Voxel GI Shadow Boost", &settings.voxelGi.receiverShadowBoost, 0.0f, 2.0f, "%.2f");
        const VoxelGiClipmapStats& voxelGiStats = renderScene.getVoxelGiClipmapStats();
        const auto voxelGiModeName = [](const VoxelGiClipmapUpdateMode mode) {
            switch (mode) {
                case VoxelGiClipmapUpdateMode::Disabled: return "Disabled";
                case VoxelGiClipmapUpdateMode::Idle: return "Idle";
                case VoxelGiClipmapUpdateMode::Full: return "Full";
                case VoxelGiClipmapUpdateMode::Shifted: return "Shifted";
            }
            return "Unknown";
        };
        ImGui::Text("Voxel GI Mode: %s (%s)",
                    voxelGiModeName(voxelGiStats.mode),
                    voxelGiStats.valid ? "valid" : "invalid");
        ImGui::Text("Voxel GI Volume: %d^3, mips %d, origin (%d,%d,%d), delta (%d,%d,%d)",
                    voxelGiStats.resolution,
                    voxelGiStats.mipLevels,
                    voxelGiStats.originBlock.x,
                    voxelGiStats.originBlock.y,
                    voxelGiStats.originBlock.z,
                    voxelGiStats.deltaVoxels.x,
                    voxelGiStats.deltaVoxels.y,
                    voxelGiStats.deltaVoxels.z);
        ImGui::Text("Voxel GI Work: sampled %llu, reused %llu, uploaded %llu, copied %llu, boxes %d",
                    static_cast<unsigned long long>(voxelGiStats.sampledVoxels),
                    static_cast<unsigned long long>(voxelGiStats.reusedVoxels),
                    static_cast<unsigned long long>(voxelGiStats.uploadedVoxels),
                    static_cast<unsigned long long>(voxelGiStats.copiedVoxels),
                    voxelGiStats.uploadedBoxes);
        ImGui::Text("Voxel GI Light: sky %.3f, sun %.3f",
                    voxelGiStats.skyRadianceScale,
                    voxelGiStats.sunRadianceScale);
        pipelineChanged |= ImGui::Checkbox("Reflection Temporal", &settings.reflection.temporalEnabled);
        pipelineChanged |= ImGui::SliderFloat("Reflection History Weight", &settings.reflection.historyWeight, 0.0f, 0.98f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Manual Exposure Value", &settings.postProcess.exposure, 0.1f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        pipelineChanged |= ImGui::SliderFloat("Gamma", &settings.postProcess.gamma, 1.0f, 3.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Saturation", &settings.postProcess.saturation, 0.0f, 2.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Contrast", &settings.postProcess.contrast, 0.5f, 2.0f, "%.2f");
        ImGui::Text("Active Pipeline: %s", renderScene.activePipelineName());
        ImGui::Text("Pipeline Status: %s", renderScene.getPipelineStatus());
        if (pipelineChanged || syncRenderSceneSettings) {
            renderScene.setSettings(settings);
        }

        ImGui::Separator();
        ImGui::Text("Distance Fog");
        bool fogChanged = false;
        if (ImGui::Button("Natural Distance")) {
            settings.fog.enabled = true;
            settings.fog.mode = 0; // Linear
            settings.fog.autoDistanceByRenderDistance = true;
            settings.fog.autoEndOffsetChunks = -0.25f;
            settings.fog.autoFadeWidthChunks = 2.5f;
            settings.fog.density = 0.006f;
            fogChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cinematic Haze")) {
            settings.fog.enabled = true;
            settings.fog.mode = 2; // Exp2
            settings.fog.autoDistanceByRenderDistance = true;
            settings.fog.autoEndOffsetChunks = -0.8f;
            settings.fog.autoFadeWidthChunks = 3.0f;
            settings.fog.density = 0.020f;
            fogChanged = true;
        }

        fogChanged |= ImGui::Checkbox("Enable Fog", &settings.fog.enabled);

        int fogMode = settings.fog.mode;
        static constexpr const char* kFogModeItems[] = { "Linear", "Exp", "Exp2" };
        if (ImGui::Combo("Fog Mode", &fogMode, kFogModeItems, IM_ARRAYSIZE(kFogModeItems))) {
            settings.fog.mode = fogMode;
            fogChanged = true;
        }

        float fogColor[3] = { settings.fog.color.x, settings.fog.color.y, settings.fog.color.z };
        if (ImGui::ColorEdit3("Fog Color", fogColor)) {
            settings.fog.color = glm::vec3(fogColor[0], fogColor[1], fogColor[2]);
            fogChanged = true;
        }

        fogChanged |= ImGui::Checkbox("Auto Distance (Render Distance)", &settings.fog.autoDistanceByRenderDistance);

        fogChanged |= ImGui::SliderFloat("Auto End Offset (chunks)", &settings.fog.autoEndOffsetChunks, -2.0f, 1.0f, "%.2f");

        fogChanged |= ImGui::SliderFloat("Auto Fade Width (chunks)", &settings.fog.autoFadeWidthChunks, 0.25f, 4.0f, "%.2f");

        if (settings.fog.autoDistanceByRenderDistance) {
            const float chunkSize = static_cast<float>(Chunk::SIZE_X);
            const float renderDistanceChunks = static_cast<float>(std::max(1, world.getRenderDistance()));
            const float autoEnd = std::max(0.0f, (renderDistanceChunks + settings.fog.autoEndOffsetChunks) * chunkSize);
            const float autoStart = std::max(0.0f, autoEnd - settings.fog.autoFadeWidthChunks * chunkSize);
            ImGui::Text("Auto Fog Range: %.1f -> %.1f", autoStart, autoEnd);
        }

        if (settings.fog.autoDistanceByRenderDistance) {
            ImGui::BeginDisabled();
        }
        fogChanged |= ImGui::SliderFloat("Fog Start", &settings.fog.startDistance, 0.0f, 600.0f, "%.1f");
        fogChanged |= ImGui::SliderFloat("Fog End", &settings.fog.endDistance, 1.0f, 800.0f, "%.1f");
        if (settings.fog.autoDistanceByRenderDistance) {
            ImGui::EndDisabled();
        }

        fogChanged |= ImGui::SliderFloat("Fog Density", &settings.fog.density, 0.001f, 0.05f, "%.4f");

        // Apply fog settings to RenderScene
        if (fogChanged) {
            renderScene.setSettings(settings);
        }

        const RenderResourceHub::MeshingFrameStats meshingStats = render.getMeshingFrameStats();
        ImGui::Text("Meshing Submitted: %d / frame", meshingStats.submitted);
        ImGui::Text("Meshing Completed: %d / frame", meshingStats.completed);
        ImGui::Text("Meshing In-Flight: %d", meshingStats.inFlight);
        ImGui::Text("Meshing Stale Dropped: %d / frame", meshingStats.staleDropped);
        ImGui::Text("Meshing Deferred Results: %d", meshingStats.deferredResults);
        ImGui::Text("Meshing Build: last %.3f ms, avg %.3f ms", meshingStats.lastBuildMs, meshingStats.averageBuildMs);

        const LightFrameStats& lightStats = m_displayLightStats;
        ImGui::Text("Light Submitted: %d / frame", lightStats.submitted);
        ImGui::Text("Light Completed: %d / frame", lightStats.completed);
        ImGui::Text("Light In-Flight: %d", lightStats.inFlight);
        ImGui::Text("Light Queued/Dirty/Pending: %d / %d / %d",
                    lightStats.queued,
                    lightStats.dirty,
                    lightStats.pendingCompleted);
        ImGui::Text("Light Submitted Reasons C/B/N: %d / %d / %d",
                    lightStats.submittedChunkLoaded,
                    lightStats.submittedBlockChanged,
                    lightStats.submittedNeighborBoundary);
        ImGui::Text("Light Dirty Reasons C/B/N: %d / %d / %d",
                    lightStats.dirtyChunkLoaded,
                    lightStats.dirtyBlockChanged,
                    lightStats.dirtyNeighborBoundary);
        ImGui::Text("Light Block Changes: %d calls, %d chunks, last (%d,%d,%d) %u -> %u",
                    lightStats.blockChangeCalls,
                    lightStats.blockChangeUniqueChunks,
                    lightStats.lastBlockChangeX,
                    lightStats.lastBlockChangeY,
                    lightStats.lastBlockChangeZ,
                    static_cast<unsigned>(lightStats.lastBlockChangeOld),
                    static_cast<unsigned>(lightStats.lastBlockChangeNew));
        ImGui::Text("Light Boundary Sync: %d", lightStats.boundarySync);
        ImGui::Text("Light Nodes Visited: %d", lightStats.nodesVisited);
        ImGui::Text("Light Stale Dropped: %d", lightStats.staleDropped);
        ImGui::Text("Light Requeued: %d", lightStats.requeued);
        ImGui::Text("Light Worker: %.3f ms, Merge: %.3f ms", lightStats.workerMs, lightStats.mergeMs);

        const uint32_t greedyBefore = meshingStats.lastOpaqueFacesBeforeGreedy;
        const uint32_t greedyAfter = meshingStats.lastOpaqueFacesAfterGreedy;
        const float greedyReduction = greedyBefore > 0
            ? (100.0f * static_cast<float>(greedyBefore - greedyAfter) / static_cast<float>(greedyBefore))
            : 0.0f;
        ImGui::Text("Opaque Faces: %u -> %u (%.1f%% fewer)", greedyBefore, greedyAfter, greedyReduction);

        const uint32_t transparentGreedyBefore = meshingStats.lastTransparentFacesBeforeGreedy;
        const uint32_t transparentGreedyAfter = meshingStats.lastTransparentFacesAfterGreedy;
        const float transparentGreedyReduction = transparentGreedyBefore > 0
            ? (100.0f * static_cast<float>(transparentGreedyBefore - transparentGreedyAfter) / static_cast<float>(transparentGreedyBefore))
            : 0.0f;
        ImGui::Text("Transparent Faces: %u -> %u (%.1f%% fewer)",
                    transparentGreedyBefore,
                    transparentGreedyAfter,
                    transparentGreedyReduction);
        ImGui::Text("Opaque Vertices: %u", meshingStats.lastOpaqueVertexCount);

        const size_t historyCount = render.getMeshingHistoryCount();
        if (historyCount > 1) {
            const auto& submittedHistory = render.getMeshingSubmittedHistory();
            const auto& completedHistory = render.getMeshingCompletedHistory();
            const auto& inFlightHistory = render.getMeshingInFlightHistory();

            ImGui::PlotLines("Submitted History", submittedHistory.data(), static_cast<int>(historyCount), 0, nullptr, 0.0f, 64.0f, ImVec2(0.0f, 60.0f));
            ImGui::PlotLines("Completed History", completedHistory.data(), static_cast<int>(historyCount), 0, nullptr, 0.0f, 64.0f, ImVec2(0.0f, 60.0f));
            ImGui::PlotLines("In-Flight History", inFlightHistory.data(), static_cast<int>(historyCount), 0, nullptr, 0.0f, 256.0f, ImVec2(0.0f, 60.0f));
        }

        const CullingFrameStats cullingStats = render.getCullingFrameStats();
        const float regionPassRate = cullingStats.regionTests > 0
            ? (100.0f * static_cast<float>(cullingStats.regionPassed) / static_cast<float>(cullingStats.regionTests))
            : 0.0f;
        const float columnPassRate = cullingStats.columnTests > 0
            ? (100.0f * static_cast<float>(cullingStats.columnPassed) / static_cast<float>(cullingStats.columnTests))
            : 0.0f;
        const float chunkPassRate = cullingStats.chunkTests > 0
            ? (100.0f * static_cast<float>(cullingStats.chunkPassed) / static_cast<float>(cullingStats.chunkTests))
            : 0.0f;

        ImGui::Separator();
        ImGui::Text("Culling Stats");
        ImGui::Text("Region Tests: %d, Pass: %.1f%%", cullingStats.regionTests, regionPassRate);
        ImGui::Text("Column Tests: %d, Pass: %.1f%%", cullingStats.columnTests, columnPassRate);
        ImGui::Text("Chunk Tests: %d, Pass: %.1f%%", cullingStats.chunkTests, chunkPassRate);
        ImGui::Text("Chunk Culled: %d", cullingStats.chunkCulled);
        if (chunkCullingDebugEnabled) {
            ImGui::Indent();
            ImGui::Text("Left:   %d", cullingStats.chunkCulledByPlane[static_cast<size_t>(FrustumPlane::Left)]);
            ImGui::Text("Right:  %d", cullingStats.chunkCulledByPlane[static_cast<size_t>(FrustumPlane::Right)]);
            ImGui::Text("Bottom: %d", cullingStats.chunkCulledByPlane[static_cast<size_t>(FrustumPlane::Bottom)]);
            ImGui::Text("Top:    %d", cullingStats.chunkCulledByPlane[static_cast<size_t>(FrustumPlane::Top)]);
            ImGui::Text("Near:   %d", cullingStats.chunkCulledByPlane[static_cast<size_t>(FrustumPlane::Near)]);
            ImGui::Text("Far:    %d", cullingStats.chunkCulledByPlane[static_cast<size_t>(FrustumPlane::Far)]);
            ImGui::Unindent();
        }
    }
}

void Dashboard::showGUIScaleSettings(UIRenderer& uiRenderer) {
    if (ImGui::CollapsingHeader("GUI Scale")) {
        ImGui::TextWrapped("GUI Scale controls the size of all UI elements (Minecraft-style).");
        ImGui::Separator();

        // Current GUI scale
        const GUIScale currentScale = uiRenderer.getGUIScale();
        int selectedScale = static_cast<int>(currentScale);

        // Scale options
        const char* scaleOptions[] = {
            "Auto (Based on Resolution)",
            "Small (0.5x)",
            "Normal (1.0x)",
            "Large (2.0x)",
            "Extra Large (3.0x)"
        };

        if (ImGui::Combo("GUI Scale", &selectedScale, scaleOptions, IM_ARRAYSIZE(scaleOptions))) {
            uiRenderer.setGUIScale(static_cast<GUIScale>(selectedScale));
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Scale Strategy per Element:");
        ImGui::BulletText("Crosshair: None (pixel-perfect)");
        ImGui::BulletText("HUD/Hotbar/Inventory: Uniform");
        ImGui::BulletText("Console/Text: Text-adaptive");
    }
}

void Dashboard::showCrosshairSettings(UIRenderer& uiRenderer) {
    if (ImGui::CollapsingHeader("Crosshair Settings")) {
        float size = uiRenderer.getCrosshairSize();
        if (ImGui::SliderFloat("Size", &size, 0.5f, 4.0f)) {
            uiRenderer.setCrosshairSize(size);
        }

        const auto& currentColor = uiRenderer.getCrosshairColor();
        float color[4] = {
            currentColor[0],
            currentColor[1],
            currentColor[2],
            currentColor[3]
        };
        if (ImGui::ColorEdit4("Color", color)) {
            uiRenderer.setCrosshairColor({color[0], color[1], color[2], color[3]});
        }
    }
}

void Dashboard::showHotbarSettings(UIRenderer& uiRenderer) {
    if (ImGui::CollapsingHeader("Hotbar Settings")) {
        const auto& bgColor = uiRenderer.getHotbarBgColor();
        float bg[4] = { bgColor[0], bgColor[1], bgColor[2], bgColor[3] };
        if (ImGui::ColorEdit4("Background Color", bg)) {
            uiRenderer.setHotbarBgColor({ bg[0], bg[1], bg[2], bg[3] });
        }

        const auto& borderColor = uiRenderer.getHotbarBorderColor();
        float border[4] = { borderColor[0], borderColor[1], borderColor[2], borderColor[3] };
        if (ImGui::ColorEdit4("Selection Border Color", border)) {
            uiRenderer.setHotbarBorderColor({ border[0], border[1], border[2], border[3] });
        }

        const auto& iconTint = uiRenderer.getHotbarIconTintColor();
        float icon[4] = { iconTint[0], iconTint[1], iconTint[2], iconTint[3] };
        if (ImGui::ColorEdit4("Icon Tint Color", icon)) {
            uiRenderer.setHotbarIconTintColor({ icon[0], icon[1], icon[2], icon[3] });
        }

        ImGui::Separator();
        ImGui::Text("Count Text");
        float countScale = uiRenderer.getHotbarCountTextScale();
        if (ImGui::SliderFloat("Count Scale", &countScale, 0.05f, 1.0f, "%.3f")) {
            uiRenderer.setHotbarCountTextScale(countScale);
        }
    }
}

void Dashboard::showInventoryPanelSettings(UIRenderer& uiRenderer) {
    if (ImGui::CollapsingHeader("Inventory Panel Settings")) {
        InventoryPanelLayout layout = uiRenderer.getInventoryPanelLayout();
        bool changed = false;

        changed |= ImGui::SliderFloat("Anchor X", &layout.anchorX, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::SliderFloat("Anchor Y", &layout.anchorY, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::SliderFloat("Offset X", &layout.offsetX, -1200.0f, 1200.0f, "%.1f");
        changed |= ImGui::SliderFloat("Offset Y", &layout.offsetY, -1200.0f, 1200.0f, "%.1f");
        changed |= ImGui::SliderFloat("Panel Scale", &layout.panelScale, 0.5f, 4.0f, "%.2f");
        ImGui::Text("Texture Base: %.0fx%.0f", InventoryPanelLayout::kTextureWidth, InventoryPanelLayout::kTextureHeight);
        ImGui::Separator();
        changed |= ImGui::SliderFloat("Grid Offset X", &layout.gridOffsetX, -40.0f, 120.0f, "%.1f");
        changed |= ImGui::SliderFloat("Grid Offset Y", &layout.gridOffsetY, -40.0f, 120.0f, "%.1f");
        changed |= ImGui::SliderFloat("Slot Size", &layout.slotSize, 6.0f, 36.0f, "%.1f");
        changed |= ImGui::SliderFloat("Column Gap", &layout.columnGap, -4.0f, 16.0f, "%.1f");
        changed |= ImGui::SliderFloat("Row Gap", &layout.rowGap, -4.0f, 16.0f, "%.1f");
        changed |= ImGui::SliderFloat("Row4 Extra Gap", &layout.row4ExtraGap, -4.0f, 40.0f, "%.1f");

        if (changed) {
            uiRenderer.setInventoryPanelLayout(layout);
        }

        ImGui::Separator();
        ImGui::Text("Count Text");
        float invCountOffsetX = uiRenderer.getInventoryCountTextOffsetX();
        if (ImGui::SliderFloat("Inv Count Offset X", &invCountOffsetX, -1.0f, 1.0f, "%.3f")) {
            uiRenderer.setInventoryCountTextOffsetX(invCountOffsetX);
        }
        float invCountOffsetY = uiRenderer.getInventoryCountTextOffsetY();
        if (ImGui::SliderFloat("Inv Count Offset Y", &invCountOffsetY, -1.0f, 1.0f, "%.3f")) {
            uiRenderer.setInventoryCountTextOffsetY(invCountOffsetY);
        }
        float invCountScale = uiRenderer.getInventoryCountTextScale();
        if (ImGui::SliderFloat("Inv Count Scale", &invCountScale, 0.05f, 1.0f, "%.3f")) {
            uiRenderer.setInventoryCountTextScale(invCountScale);
        }
    }
}

void Dashboard::showCraftingGridSettings(UIRenderer& uiRenderer) {
    if (ImGui::CollapsingHeader("Crafting Grid Settings")) {
        InventoryPanelLayout panelLayout = uiRenderer.getInventoryPanelLayout();
        CraftingGridLayout craftLayout = panelLayout.craftingGrid;
        bool changed = false;

        ImGui::Text("2x2 Crafting Grid");
        changed |= ImGui::SliderFloat("Craft Offset X", &craftLayout.offsetX, -40.0f, 170.0f, "%.1f");
        changed |= ImGui::SliderFloat("Craft Offset Y", &craftLayout.offsetY, -40.0f, 170.0f, "%.1f");
        changed |= ImGui::SliderFloat("Craft Slot Size", &craftLayout.slotSize, 6.0f, 36.0f, "%.1f");
        changed |= ImGui::SliderFloat("Craft Column Gap", &craftLayout.columnGap, -4.0f, 16.0f, "%.1f");
        changed |= ImGui::SliderFloat("Craft Row Gap", &craftLayout.rowGap, -4.0f, 16.0f, "%.1f");

        ImGui::Separator();
        ImGui::Text("Result Slot");
        changed |= ImGui::SliderFloat("Result Offset X", &craftLayout.resultOffsetX, -40.0f, 170.0f, "%.1f");
        changed |= ImGui::SliderFloat("Result Offset Y", &craftLayout.resultOffsetY, -40.0f, 170.0f, "%.1f");
        changed |= ImGui::SliderFloat("Result Slot Size", &craftLayout.resultSlotSize, 6.0f, 36.0f, "%.1f");

        if (changed) {
            panelLayout.craftingGrid = craftLayout;
            uiRenderer.setInventoryPanelLayout(panelLayout);
        }
    }
}

void Dashboard::showTextSettings(UIRenderer& uiRenderer) {
    if (ImGui::CollapsingHeader("Text Settings")) {
        float caretBlinkMs = uiRenderer.getCommandCaretBlinkPeriodMs();
        if (ImGui::SliderFloat("Command Caret Blink (ms)", &caretBlinkMs, 120.0f, 1500.0f, "%.0f")) {
            uiRenderer.setCommandCaretBlinkPeriodMs(caretBlinkMs);
        }
    }
}

void Dashboard::showHeldItemPreviewSettings(FirstPersonHeldItemRenderer& firstPersonHeldItemRenderer) {
    if (ImGui::CollapsingHeader("First Person Held Item")) {
        FirstPersonHeldItemRenderer::Config config = firstPersonHeldItemRenderer.getConfig();
        bool changed = false;

        changed |= ImGui::SliderFloat("FOV", &config.fovDegrees, 20.0f, 120.0f, "%.1f");

        ImGui::Separator();
        ImGui::Text("Arm");
        changed |= ImGui::SliderFloat("Arm X", &config.armPosX, -2.0f, 2.0f, "%.3f");
        changed |= ImGui::SliderFloat("Arm Y", &config.armPosY, -2.0f, 2.0f, "%.3f");
        changed |= ImGui::SliderFloat("Arm Z", &config.armPosZ, -4.0f, -0.05f, "%.3f");
        changed |= ImGui::SliderFloat("Arm Pitch", &config.armPitchDegrees, -180.0f, 180.0f, "%.1f");
        changed |= ImGui::SliderFloat("Arm Yaw", &config.armYawDegrees, -180.0f, 180.0f, "%.1f");
        changed |= ImGui::SliderFloat("Arm Roll", &config.armRollDegrees, -180.0f, 180.0f, "%.1f");
        changed |= ImGui::SliderFloat("Arm Scale", &config.armScale, 0.05f, 3.0f, "%.3f");

        ImGui::Separator();
        ImGui::Text("Item");
        changed |= ImGui::SliderFloat("Item X", &config.itemPosX, -2.0f, 2.0f, "%.3f");
        changed |= ImGui::SliderFloat("Item Y", &config.itemPosY, -2.0f, 2.0f, "%.3f");
        changed |= ImGui::SliderFloat("Item Z", &config.itemPosZ, -4.0f, -0.05f, "%.3f");
        changed |= ImGui::SliderFloat("Item Pitch", &config.itemPitchDegrees, -180.0f, 180.0f, "%.1f");
        changed |= ImGui::SliderFloat("Item Yaw", &config.itemYawDegrees, -180.0f, 180.0f, "%.1f");
        changed |= ImGui::SliderFloat("Item Roll", &config.itemRollDegrees, -180.0f, 180.0f, "%.1f");
        changed |= ImGui::SliderFloat("Item Scale", &config.itemScale, 0.05f, 3.0f, "%.3f");
        changed |= ImGui::SliderFloat("Block Pitch", &config.blockPitchDegrees, -180.0f, 180.0f, "%.1f");
        changed |= ImGui::SliderFloat("Block Yaw", &config.blockYawDegrees, -180.0f, 180.0f, "%.1f");
        changed |= ImGui::SliderFloat("Block Scale", &config.blockScale, 0.05f, 3.0f, "%.3f");

        ImGui::Separator();
        ImGui::Text("Motion");
        changed |= ImGui::SliderFloat("Equip Drop", &config.equipDrop, 0.0f, 3.0f, "%.3f");
        changed |= ImGui::SliderFloat("Equip Speed", &config.equipSpeed, 0.1f, 30.0f, "%.2f");
        changed |= ImGui::SliderFloat("Bob X", &config.bobOffsetX, -0.3f, 0.3f, "%.4f");
        changed |= ImGui::SliderFloat("Bob Y", &config.bobOffsetY, -0.3f, 0.3f, "%.4f");
        changed |= ImGui::SliderFloat("Bob Roll", &config.bobRollDegrees, -20.0f, 20.0f, "%.2f");
        changed |= ImGui::SliderFloat("View Lag Speed", &config.viewLagFollowSpeed, 0.1f, 40.0f, "%.2f");
        changed |= ImGui::SliderFloat("View Lag Max", &config.viewLagMaxDegrees, 0.0f, 45.0f, "%.1f");
        changed |= ImGui::SliderFloat("View Lag X", &config.viewLagOffsetX, -0.05f, 0.05f, "%.4f");
        changed |= ImGui::SliderFloat("View Lag Y", &config.viewLagOffsetY, -0.05f, 0.05f, "%.4f");
        changed |= ImGui::SliderFloat("View Lag Yaw", &config.viewLagYawDegrees, -2.0f, 2.0f, "%.3f");
        changed |= ImGui::SliderFloat("View Lag Pitch", &config.viewLagPitchDegrees, -2.0f, 2.0f, "%.3f");

        if (ImGui::TreeNode("Swing")) {
            changed |= ImGui::SliderFloat("Swing Duration", &config.swingDurationSeconds, 0.05f, 2.0f, "%.3f");
            changed |= ImGui::SliderFloat("Arm Swing X", &config.armSwingX, -1.0f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat("Arm Swing Y", &config.armSwingY, -1.0f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat("Arm Swing Z", &config.armSwingZ, -1.0f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat("Arm Swing Pitch", &config.armSwingPitchDegrees, -180.0f, 180.0f, "%.1f");
            changed |= ImGui::SliderFloat("Arm Swing Yaw", &config.armSwingYawDegrees, -180.0f, 180.0f, "%.1f");
            changed |= ImGui::SliderFloat("Arm Swing Roll", &config.armSwingRollDegrees, -180.0f, 180.0f, "%.1f");
            changed |= ImGui::SliderFloat("Item Swing X", &config.itemSwingX, -1.0f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat("Item Swing Y", &config.itemSwingY, -1.0f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat("Item Swing Z", &config.itemSwingZ, -1.0f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat("Item Swing Pitch", &config.itemSwingPitchDegrees, -180.0f, 180.0f, "%.1f");
            changed |= ImGui::SliderFloat("Item Swing Yaw", &config.itemSwingYawDegrees, -180.0f, 180.0f, "%.1f");
            changed |= ImGui::SliderFloat("Item Swing Roll", &config.itemSwingRollDegrees, -180.0f, 180.0f, "%.1f");
            ImGui::TreePop();
        }

        if (changed) {
            firstPersonHeldItemRenderer.setConfig(config);
        }
        if (ImGui::Button("Reset Held Item Defaults")) {
            firstPersonHeldItemRenderer.resetConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Held Item Config")) {
            firstPersonHeldItemRenderer.saveConfig();
        }
    }
}

#endif // NDEBUG
