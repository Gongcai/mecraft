//
// Created by Caiwe on 2026/3/25.
//

// Dashboard 调试 UI 仅在 Debug 模式下编译
#ifdef MECRAFT_DEBUG

#include "Dashboard.h"

#include "UIRenderer.h"
#include "../ecs/components/Components.h"
#include "../renderer/FirstPersonHeldItemRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cmath>
#include <cstdint>

Dashboard::Dashboard() {
    // Setup Dear ImGui context

}

Dashboard::~Dashboard() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
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
}

void Dashboard::setFirstPersonHeldItemRenderer(FirstPersonHeldItemRenderer* renderer) {
    m_firstPersonHeldItemRenderer = renderer;
}

void Dashboard::render(ecs::GameplayRegistry &registry,
                       World &world,
                       Camera &camera,
                       Renderer &render,
                       PostProcessRenderer& postProcess,
                       UIRenderer& uiRenderer,
                       const FrameProfilerStats& profilerStats) {
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
        showWorldStats(world, registry);
        showPerformanceStats(world, render, postProcess, profilerStats);
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

void Dashboard::showWorldStats(World& world, ecs::GameplayRegistry& registry) {
    if (ImGui::CollapsingHeader("World Stats")) {
        ecs::PlayerQuery query(registry);
        const glm::vec3 position = query.getPosition();
        const int worldX = static_cast<int>(std::floor(position.x));
        const int worldZ = static_cast<int>(std::floor(position.z));
        const glm::ivec2 chunkCoords = world.getChunkCoords(worldX, worldZ);
        const TerrainBiome biome = world.getBiome(worldX, worldZ);

        ImGui::Text("Render Distance: %d chunks", world.getRenderDistance());
        ImGui::Text("Loaded Chunks: %zu", world.getActiveChunks().size());
        ImGui::Text("Total Vertices: %zu", world.getTotalVertexCount());
        ImGui::Text("Current Chunk: (%d, %d)", chunkCoords.x, chunkCoords.y);
        ImGui::Text("Current Biome: %s", World::biomeToString(biome));
        if (ImGui::Button("Increase Render Distance")) {
            world.setRenderDistance(world.getRenderDistance() + 1);
        }ImGui::SameLine();
        if (ImGui::Button("Decrease Render Distance")) {
            world.setRenderDistance(world.getRenderDistance() - 1);
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

void Dashboard::showPerformanceStats(World& world, Renderer &render, PostProcessRenderer& postProcess, const FrameProfilerStats& profilerStats) {
    if (ImGui::CollapsingHeader("Performance Stats")) {
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame Time: %.3f ms", 1000.0 / ImGui::GetIO().Framerate);
        ImGui::Text("Loop Frame (clamped): %.3f ms", profilerStats.frameMs);
        ImGui::Text("Fixed Update: %.3f ms", profilerStats.fixedUpdateMs);
        ImGui::Text("  - Input Update: %.3f ms", profilerStats.fixedInputMs);
        ImGui::Text("  - State Update: %.3f ms", profilerStats.fixedStateUpdateMs);
        ImGui::Text("  - Particle Update: %.3f ms", profilerStats.fixedParticleUpdateMs);
        ImGui::Text("  - Drop Update: %.3f ms", profilerStats.fixedDropUpdateMs);
        ImGui::Text("  - World Update: %.3f ms", profilerStats.fixedWorldUpdateMs);
        ImGui::Text("Audio Sync: %.3f ms", profilerStats.audioMs);
        ImGui::Text("Render Submit: %.3f ms", profilerStats.renderMs);

        if (profilerStats.fixedHistoryCount > 1) {
            auto historyMax = [&](const std::array<float, FrameProfilerStats::kFixedHistorySamples>& history) {
                float maxValue = 0.0f;
                for (size_t i = 0; i < profilerStats.fixedHistoryCount; ++i) {
                    if (history[i] > maxValue) {
                        maxValue = history[i];
                    }
                }
                return std::max(maxValue * 1.1f, 0.1f);
            };

            ImGui::Separator();
            ImGui::Text("Fixed Update History (ms/step)");
            ImGui::PlotLines("Fixed Total", profilerStats.fixedUpdateHistory.data(),
                             static_cast<int>(profilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(profilerStats.fixedUpdateHistory), ImVec2(0.0f, 65.0f));
            ImGui::PlotLines("Input", profilerStats.fixedInputHistory.data(),
                             static_cast<int>(profilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(profilerStats.fixedInputHistory), ImVec2(0.0f, 55.0f));
            ImGui::PlotLines("State", profilerStats.fixedStateHistory.data(),
                             static_cast<int>(profilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(profilerStats.fixedStateHistory), ImVec2(0.0f, 55.0f));
            ImGui::PlotLines("Particle", profilerStats.fixedParticleHistory.data(),
                             static_cast<int>(profilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(profilerStats.fixedParticleHistory), ImVec2(0.0f, 55.0f));
            ImGui::PlotLines("Drop", profilerStats.fixedDropHistory.data(),
                             static_cast<int>(profilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(profilerStats.fixedDropHistory), ImVec2(0.0f, 55.0f));
            ImGui::PlotLines("World", profilerStats.fixedWorldHistory.data(),
                             static_cast<int>(profilerStats.fixedHistoryCount), 0, nullptr,
                             0.0f, historyMax(profilerStats.fixedWorldHistory), ImVec2(0.0f, 55.0f));
        }

        if (render.isMultiDrawIndirectEnabled()) {
            ImGui::Text("GL Submissions: %d (MDI)", render.getGlSubmitCount());
        } else {
            ImGui::Text("Draw Calls: %d", render.getDrawCallCount());
        }

        Renderer::GpuFrameStats gpuStats = render.getGpuFrameStats();
        bool gpuTimerEnabled = render.isGpuTimerEnabled();
        if (ImGui::Checkbox("GPU Timer Query", &gpuTimerEnabled)) {
            render.setGpuTimerEnabled(gpuTimerEnabled);
        }
        if (!gpuStats.supported) {
            ImGui::Text("GPU Timers: unsupported");
        } else if (!gpuStats.valid) {
            ImGui::Text("GPU Timers: waiting");
        } else {
            ImGui::Text("GPU GBuffer: %.3f ms", gpuStats.gbufferMs);
            ImGui::Text("GPU Shadow: %.3f ms", gpuStats.shadowMs);
            ImGui::Text("GPU SSAO: %.3f ms", gpuStats.ssaoMs);
            ImGui::Text("GPU Lighting: %.3f ms", gpuStats.lightingMs);
            ImGui::Text("GPU Transparent: %.3f ms", gpuStats.transparentMs);
            ImGui::Text("GPU Volumetric: %.3f ms", gpuStats.volumetricMs);
            ImGui::Text("GPU Reflection: %.3f ms", gpuStats.reflectionMs);
            ImGui::Text("GPU Cloud: %.3f ms", gpuStats.cloudMs);
            ImGui::Text("GPU Water: %.3f ms", gpuStats.waterMs);
            ImGui::Text("GPU Post: %.3f ms", gpuStats.postMs);
        }

        Renderer::RenderWorkStats renderWork = render.getRenderWorkStats();
        const double vertexBytes = static_cast<double>(renderWork.blockVertexBytes);
        const auto toMiB = [&](const uint64_t vertices) {
            return static_cast<double>(vertices) * vertexBytes / (1024.0 * 1024.0);
        };
        const auto toPoolMiB = [&](const size_t vertices) {
            return static_cast<double>(vertices) * vertexBytes / (1024.0 * 1024.0);
        };

        ImGui::Separator();
        ImGui::Text("Render Work");
        ImGui::Text("BlockVertex: %llu bytes", static_cast<unsigned long long>(renderWork.blockVertexBytes));
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
        ImGui::Text("Vertex Read O/C/T: %.2f / %.2f / %.2f MiB",
                    toMiB(renderWork.opaqueVertices),
                    toMiB(renderWork.cutoutVertices),
                    toMiB(renderWork.transparentVertices));
        ImGui::Text("Pool Used O/C/T: %.2f / %.2f / %.2f MiB",
                    toPoolMiB(renderWork.opaquePoolUsedVertices),
                    toPoolMiB(renderWork.cutoutPoolUsedVertices),
                    toPoolMiB(renderWork.transparentPoolUsedVertices));
        ImGui::Text("Pool Capacity O/C/T: %.2f / %.2f / %.2f MiB",
                    toPoolMiB(renderWork.opaquePoolCapacityVertices),
                    toPoolMiB(renderWork.cutoutPoolCapacityVertices),
                    toPoolMiB(renderWork.transparentPoolCapacityVertices));
        ImGui::Text("Pool Fragmentation O/C/T: %.1f%% / %.1f%% / %.1f%%",
                    renderWork.opaquePoolFragmentation * 100.0f,
                    renderWork.cutoutPoolFragmentation * 100.0f,
                    renderWork.transparentPoolFragmentation * 100.0f);

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

        ImGui::Text("Game Time Speed: %.2f",Time::getTimeSpeed());
        bool chunkCullingDebugEnabled = render.isChunkCullingDebugEnabled();
        if (ImGui::Checkbox("Chunk Culling Debug", &chunkCullingDebugEnabled)) {
            render.setChunkCullingDebugEnabled(chunkCullingDebugEnabled);
        }
        static float timeSpeed = Time::getTimeSpeed();
        if (ImGui::SliderFloat("Game Time Speed", &timeSpeed, 0.0f, 10.0f)) {
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
        Renderer::RenderPipelineSettings pipeline = render.getRenderPipelineSettings();
        int pipelineMode = static_cast<int>(pipeline.mode);
        int tonemapMode = pipeline.tonemapMode;
        int debugViewMode = pipeline.debugViewMode;
        int weatherPresetInstant = static_cast<int>(world.getWeatherSystem().getRenderState().type);
        int weatherPresetSmooth = static_cast<int>(world.getWeatherSystem().getTargetState().type);
        static constexpr const char* kPipelineModes[] = {"Forward Legacy", "Hybrid Deferred"};
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
            "65: VFog Beam Modulation"
        };
        static constexpr const char* kWeatherPresets[] = {"Clear", "Rain", "Storm", "Snow"};
        bool pipelineChanged = false;
        pipelineChanged |= ImGui::Combo("Pipeline Mode", &pipelineMode, kPipelineModes, IM_ARRAYSIZE(kPipelineModes));
        pipeline.mode = static_cast<Renderer::RenderPipelineMode>(pipelineMode);
        pipelineChanged |= ImGui::Combo("Deferred Debug View", &debugViewMode, kDebugViewModes, IM_ARRAYSIZE(kDebugViewModes));
        pipeline.debugViewMode = debugViewMode;
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
            "19: Contact Shadow"
        };
        int lightDebugMode = pipeline.deferredLightDebugMode;
        pipelineChanged |= ImGui::Combo("Light Debug", &lightDebugMode, kLightDebugModes, IM_ARRAYSIZE(kLightDebugModes));
        pipeline.deferredLightDebugMode = lightDebugMode;
        static constexpr const char* kPostprocessDebugModes[] = {
            "0: Off",
            "1: BloomData",
            "2: FogTransmittance",
            "3: BloomyFog",
            "4: RainMask"
        };
        int ppDebugMode = pipeline.postprocessDebugMode;
        pipelineChanged |= ImGui::Combo("Postprocess Debug", &ppDebugMode, kPostprocessDebugModes, IM_ARRAYSIZE(kPostprocessDebugModes));
        pipeline.postprocessDebugMode = ppDebugMode;
        static constexpr const char* kReflectionDebugModes[] = {
            "0: Off",
            "1: PixelWetness",
            "2: Reflectance",
            "3: SSR Hit",
            "4: Roughness",
            "5: SpecularWeight",
            "6: CompositeDelta"
        };
        int reflDebugMode = pipeline.reflectionDebugMode;
        pipelineChanged |= ImGui::Combo("Reflection Debug", &reflDebugMode, kReflectionDebugModes, IM_ARRAYSIZE(kReflectionDebugModes));
        pipeline.reflectionDebugMode = reflDebugMode;
        pipelineChanged |= ImGui::Checkbox("Sun Shadows", &pipeline.shadowsEnabled);
        pipelineChanged |= ImGui::Checkbox("Soft Shadows", &pipeline.softShadowsEnabled);
        pipelineChanged |= ImGui::Checkbox("PCSS Shadows", &pipeline.pcssShadowsEnabled);
        pipelineChanged |= ImGui::Checkbox("Contact Shadows", &pipeline.contactShadowsEnabled);
        pipelineChanged |= ImGui::Checkbox("Cloud Shadows", &pipeline.cloudShadowsEnabled);
        pipelineChanged |= ImGui::Checkbox("Derivative Strict", &pipeline.derivativeStrictMode);
        pipelineChanged |= ImGui::Checkbox("SSAO", &pipeline.ssaoEnabled);
        pipelineChanged |= ImGui::Checkbox("Bloom Flag", &pipeline.bloomEnabled);
        pipelineChanged |= ImGui::SliderFloat("Bloom Threshold", &pipeline.bloomThreshold, 0.0f, 3.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Bloom Amount", &pipeline.bloomStrength, 0.0f, 5.0f, "%.2f");
        pipelineChanged |= ImGui::Checkbox("Depth of Field", &pipeline.dofEnabled);
        pipelineChanged |= ImGui::SliderFloat("DoF Focus", &pipeline.dofFocusDistance, 0.5f, 50.0f, "%.1f blocks");
        pipelineChanged |= ImGui::SliderFloat("DoF Aperture", &pipeline.dofAperture, 0.8f, 22.0f, "%.1f");
        pipelineChanged |= ImGui::SliderFloat("DoF Intensity", &pipeline.dofIntensity, 0.0f, 1.0f, "%.3f");
        pipelineChanged |= ImGui::Checkbox("Auto Exposure", &pipeline.autoExposureEnabled);
        pipelineChanged |= ImGui::SliderFloat("Auto Exp Min", &pipeline.autoExposureMin, 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        pipelineChanged |= ImGui::SliderFloat("Auto Exp Max", &pipeline.autoExposureMax, 1.0f, 64.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::TextDisabled("DerivativeMain exposure target is unclamped; min/max are legacy UI fields.");
        pipelineChanged |= ImGui::SliderFloat("Auto Exp Speed", &pipeline.autoExposureSpeed, 0.1f, 6.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Auto Exp Bias", &pipeline.autoExposureBias, -2.0f, 2.0f, "%.2f EV");
        pipelineChanged |= ImGui::Checkbox("Sun Rays", &pipeline.sunRaysEnabled);
        pipelineChanged |= ImGui::Checkbox("Water Effects", &pipeline.waterEffectsEnabled);
        pipelineChanged |= ImGui::Checkbox("Transparent Composite", &pipeline.transparentCompositeEnabled);
        pipelineChanged |= ImGui::SliderFloat("Scene Cloud Composite", &pipeline.sceneCloudCompositeStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Scene Reflection Composite", &pipeline.sceneReflectionCompositeStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::Checkbox("Shaderpack Grading", &pipeline.shaderpackGradingEnabled);
        pipelineChanged |= ImGui::Checkbox("Purkinje Shift", &pipeline.purkinjeShiftEnabled);
        pipelineChanged |= ImGui::Checkbox("Bloomy Fog", &pipeline.bloomyFogEnabled);
        pipelineChanged |= ImGui::Checkbox("Aerial Perspective", &pipeline.aerialPerspectiveEnabled);
        pipelineChanged |= ImGui::Checkbox("Volumetric Fog", &pipeline.volumetricFogEnabled);
        pipelineChanged |= ImGui::Checkbox("VFog Sky Ray March", &pipeline.volumetricSkyRayEnabled);
        pipelineChanged |= ImGui::Checkbox("VFog TIME_FADE", &pipeline.volumetricTimeFadeEnabled);
        static constexpr const char* kVFogQualityTiers[] = {"Low", "Medium", "High", "Ultra"};
        int qualityTier = pipeline.volumetricQualityTier;
        pipelineChanged |= ImGui::Combo("VFog Quality Tier", &qualityTier, kVFogQualityTiers, IM_ARRAYSIZE(kVFogQualityTiers));
        pipeline.volumetricQualityTier = qualityTier;
        // DerivativeMain-style VFog independent profile controls
        pipelineChanged |= ImGui::SliderFloat("VFog Center Height", &pipeline.vfogCenterHeight, 0.0f, 255.0f, "%.0f");
        pipelineChanged |= ImGui::SliderFloat("VFog Height Spread", &pipeline.vfogHeightSpread, 0.0f, 128.0f, "%.0f");
        pipelineChanged |= ImGui::SliderFloat("VFog Noise Scale", &pipeline.vfogNoiseScale, 0.001f, 0.200f, "%.3f");
        pipelineChanged |= ImGui::SliderFloat("VFog Light Strength", &pipeline.vfogLightStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("VFog Density Scale", &pipeline.vfogDensityScale, 0.0f, 10.0f, "%.2f");
        if (ImGui::Combo("Weather Instant (Debug)", &weatherPresetInstant, kWeatherPresets, IM_ARRAYSIZE(kWeatherPresets))) {
            world.getWeatherSystem().setDebugWeatherPresetInstant(static_cast<WeatherType>(weatherPresetInstant));
        }
        if (ImGui::Combo("Weather Smooth (Debug)", &weatherPresetSmooth, kWeatherPresets, IM_ARRAYSIZE(kWeatherPresets))) {
            world.getWeatherSystem().setDebugWeatherPresetSmooth(static_cast<WeatherType>(weatherPresetSmooth));
        }
        pipelineChanged |= ImGui::Combo("Tonemap Mode", &tonemapMode, kTonemapModes, IM_ARRAYSIZE(kTonemapModes));
        pipeline.tonemapMode = tonemapMode;
        // Exposure diagnostics
        {
            float adapted = postProcess.getAdaptedExposure();
            float target = postProcess.getTargetExposure();
            float avgLum = postProcess.getAverageLuminance();
            float resolvedExposure = pipeline.autoExposureEnabled ? adapted : (0.8f / std::max(pipeline.exposure, 0.0001f));
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Exposure Diagnostics");
            ImGui::Text("Resolved Exposure: %.4f", resolvedExposure);
            if (pipeline.autoExposureEnabled) {
                ImGui::Text("Adapted: %.4f  Target: %.4f", adapted, target);
                ImGui::Text("Avg Luminance: %.3f", avgLum);
            } else {
                ImGui::Text("Manual Exposure: %.2f (1/exposure=%.4f)", pipeline.exposure, resolvedExposure);
            }
            // SkyCapture metadata
            auto skyLux = render.getSkyIlluminanceData();
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "SkyCapture Metadata (LUT units)");
            ImGui::Text("Direct: (%.6f, %.6f, %.6f)", skyLux.directIlluminance.r, skyLux.directIlluminance.g, skyLux.directIlluminance.b);
            ImGui::Text("Sky:   (%.6f, %.6f, %.6f)", skyLux.skyIlluminance.r, skyLux.skyIlluminance.g, skyLux.skyIlluminance.b);
            ImGui::Text("Sun:   (%.6f, %.6f, %.6f)", skyLux.sunIlluminance.r, skyLux.sunIlluminance.g, skyLux.sunIlluminance.b);
            ImGui::Text("Moon:  (%.6f, %.6f, %.6f)", skyLux.moonIlluminance.r, skyLux.moonIlluminance.g, skyLux.moonIlluminance.b);
            // Lighting input diagnostic — compare CPU art colors vs SkyCapture metadata
            auto skyColors = render.getSkyColors();
            auto fogColor = render.getFogColor();
            ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.4f, 1.0f), "Lighting Input Diagnostic");
            ImGui::Text("SunLightColor(CPU): (%.2f, %.2f, %.2f)", skyColors.sunLightColor.r, skyColors.sunLightColor.g, skyColors.sunLightColor.b);
            ImGui::Text("SkyAmbientColor(CPU): (%.2f, %.2f, %.2f)", skyColors.skyAmbientColor.r, skyColors.skyAmbientColor.g, skyColors.skyAmbientColor.b);
            ImGui::Text("FogColor(CPU):      (%.2f, %.2f, %.2f)", fogColor.r, fogColor.g, fogColor.b);
            ImGui::Text("HorizonScatter(CPU): (%.2f, %.2f, %.2f)", skyColors.horizonScatterColor.r, skyColors.horizonScatterColor.g, skyColors.horizonScatterColor.b);
            ImGui::Text("DirectSunStrength: %.2f  SkyAmbientStrength: %.2f", pipeline.directSunStrength, pipeline.skyAmbientStrength);
            ImGui::Text("SunWarmth: %.2f  SkyCoolness: %.2f", pipeline.sunWarmth, pipeline.skyCoolness);
            // Effective after-tint values (what passes actually use)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Effective After Tint");
            // coolSkyColor = mix(skyAmbient, skyAmbient * coolness tint, skyCoolness)
            float coolR = skyColors.skyAmbientColor.r * (1.0f - pipeline.skyCoolness * 0.22f);
            float coolG = skyColors.skyAmbientColor.g * (1.0f + pipeline.skyCoolness * 0.08f);
            float coolB = skyColors.skyAmbientColor.b * (1.0f + pipeline.skyCoolness * 0.18f);
            ImGui::Text("coolSkyColor: (%.2f, %.2f, %.2f)", coolR, coolG, coolB);
            ImGui::Text("Cloud cirrus: env.sunIlluminance * 40.0");
            // VFog component diagnostics (CPU approximate — actual values are GPU-side)
            ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "Volumetric Fog Diagnostics (approx)");
            const auto& weather = world.getWeatherSystem().getRenderState();
            const auto targetWeather = world.getWeatherSystem().getTargetState();
            const auto& derivedWeather = world.getWeatherSystem().getDerived();
            float sunY = skyColors.sunDirection.y;
            float sunVis = (sunY > -0.08f) ? std::min((sunY + 0.08f) / 0.26f, 1.0f) : 0.0f;
            ImGui::Text("env.sunIlluminance: (%.2f, %.2f, %.2f)", skyLux.sunIlluminance.r, skyLux.sunIlluminance.g, skyLux.sunIlluminance.b);
            ImGui::Text("env.skyIlluminance:  (%.2f, %.2f, %.2f)", skyLux.skyIlluminance.r, skyLux.skyIlluminance.g, skyLux.skyIlluminance.b);
            ImGui::Text("sunVisibility(CPU): %.3f  (sunDir.y=%.2f)", sunVis, sunY);
            ImGui::Text("VFog strength: %.2f", pipeline.volumetricFogStrength);
            ImGui::Text("VFog profile: center=%.0f  spread=%.0f  noise=%.3f  light=%.2f  density=%.2f",
                pipeline.vfogCenterHeight, pipeline.vfogHeightSpread, pipeline.vfogNoiseScale,
                pipeline.vfogLightStrength, pipeline.vfogDensityScale);
            ImGui::Text("VFog baseDensity: 1.0  (VolumetricSettings defaults)");
            const char* tierNames[] = {"Low(0.5x)", "Medium(1.4x)", "High(9.0x)", "Ultra(48.0x)"};
            ImGui::Text("VFog tier: %s  maxDist: 260  heightFalloff: 0.022", tierNames[qualityTier]);
            {
                float meFade = (sunY < 0.18f) ? 0.37f + 1.2f * std::max(0.0f, -sunY) : 1.7f;
                float meWeight = std::pow(std::clamp(1.0f - meFade * std::abs(sunY - 0.18f), 0.0f, 1.0f), 2.0f);
                float timeMidnight = (sunY < 0.0f ? 1.0f : 0.0f) * (1.0f - meWeight);
                float wetness = derivedWeather.skyWetness;
                float airGate = pipeline.volumetricTimeFadeEnabled
                    ? std::clamp(meWeight + 0.25f, 0.0f, 1.0f) + timeMidnight * 4.0f
                    : 1.0f;
                float mistGate = pipeline.volumetricTimeFadeEnabled
                    ? meWeight * meWeight + timeMidnight * 2.0f
                    : 1.0f;
                float tierMultiplier = qualityTier <= 0 ? 0.5f : (qualityTier <= 1 ? 1.4f : (qualityTier <= 2 ? 9.0f : 48.0f));
                ImGui::Text("VFog TIME_FADE gates: %s  air=%.3f mist=%.3f wet=%.2f",
                    pipeline.volumetricTimeFadeEnabled ? "ON" : "OFF", airGate, mistGate, wetness);
                ImGui::Text("VFog effective tier density: %.2f x mistGate = %.3f", tierMultiplier, tierMultiplier * mistGate);
                if (weather.type == WeatherType::Clear && mistGate < 0.02f) {
                    ImGui::TextDisabled("Clear noon: DerivativeMain TIME_FADE suppresses mist density; quality tiers mostly affect samples.");
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
            ImGui::Text("Tonemap: %s", tonemapNames[std::clamp(pipeline.tonemapMode, 0, 5)]);
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
                pipelineChanged |= ImGui::SliderFloat("Direct Occlusion Override", &pipeline.directWeatherOcclusion, -1.0f, 1.0f, "%.2f (<0=auto, >=0=bypass all cloud shadow)");
                ImGui::TextDisabled("SkyCapture metadata stays weather-independent; direct rain dimming happens in deferred cloudShadow.");
            }
            // Weather debug overrides — multiply against DerivativeMain formula.
            // Default values (1.0 / 0.0) = no override, use DerivativeMain contract.
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.5f, 1.0f), "Weather Debug Override");
                ImGui::TextDisabled("Multiplies against DerivativeMain formula. 1.0/0.0 = no override.");
                pipelineChanged |= ImGui::SliderFloat("Skylight Scale##weather", &pipeline.weatherSkylightScale, 0.0f, 1.0f, "%.2f (x DerivMain)");
                pipelineChanged |= ImGui::SliderFloat("Exposure Bias##weather", &pipeline.weatherExposureBias, -2.0f, 2.0f, "%.2f EV");
                pipelineChanged |= ImGui::SliderFloat("Post Rain Fog##weather", &pipeline.weatherPostRainFog, 0.0f, 2.0f, "%.2f (x DerivMain)");
                pipelineChanged |= ImGui::SliderFloat("Rain Alpha Scale##weather", &pipeline.weatherRainAlphaScale, 0.0f, 5.0f, "%.2f");
            }
            ImGui::Separator();
        }
        ImGui::TextUnformatted("Shadow Projection: CSM Linear");
        if (pipeline.debugDisableGreedyMeshing) {
            pipeline.debugDisableGreedyMeshing = false;
            for (const auto& [chunkKey, chunk] : world.getActiveChunks()) {
                (void)chunkKey;
                if (chunk) {
                    chunk->markExistingSubChunksDirty();
                }
            }
            pipelineChanged = true;
        }
        if (ImGui::Button("Preset Neutral")) {
            pipeline.tonemapMode = 1;

            pipeline.softShadowsEnabled = true;
            pipeline.pcssShadowsEnabled = false;
            pipeline.contactShadowsEnabled = false;
            pipeline.cloudShadowsEnabled = false;
            pipeline.directSunStrength = 1.0f;
            pipeline.skyAmbientStrength = 0.55f;
            pipeline.minimumAmbient = 0.09f;
            pipeline.shadowMinLight = 0.18f;
            pipeline.shadowContrast = 1.0f;
            pipeline.shadowTintStrength = 0.18f;
            pipeline.blockLightStrength = 1.0f;
            pipeline.fakeBounceStrength = 0.04f;
            world.getWeatherSystem().setDebugWeatherPreset(WeatherType::Clear);
            pipeline.aerialStrength = 0.25f;
            pipeline.horizonScatterStrength = 0.35f;
            pipeline.volumetricFogStrength = 0.0f;
            pipeline.vfogDensityScale = 0.0f;
            pipeline.bloomThreshold = 1.05f;
            pipeline.bloomStrength = 0.10f;
            pipeline.sunRaysEnabled = false;
            pipeline.dofEnabled = false;
            pipeline.autoExposureEnabled = false;
            pipeline.autoExposureMin = 0.70f;
            pipeline.autoExposureMax = 1.40f;
            pipeline.autoExposureSpeed = 1.0f;
            pipeline.autoExposureBias = 0.0f;
            pipeline.exposure = 12.0f;
            pipeline.vibrance = 0.0f;
            pipeline.kappaGradingStrength = 0.0f;
            pipeline.highlightCompression = 0.35f;
            pipeline.filmEmulationStrength = 0.0f;
            pipeline.redModifierStrength = 0.0f;
            pipeline.colorLumaR = 1.0f;
            pipeline.colorLumaG = 1.0f;
            pipeline.colorLumaB = 1.0f;
            pipeline.albedoDesaturation = 0.0f;
            pipeline.sunWarmth = 0.0f;
            pipeline.skyCoolness = 0.0f;
            pipeline.shadowDesaturation = 0.0f;
            pipeline.splitToneStrength = 0.0f;
            pipeline.vignetteStrength = 0.0f;
            pipeline.sharpenStrength = 0.0f;
            pipeline.saturation = 1.0f;
            pipeline.contrast = 1.0f;
            pipelineChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Preset Natural")) {
            pipeline.tonemapMode = 1;

            pipeline.softShadowsEnabled = true;
            pipeline.pcssShadowsEnabled = true;
            pipeline.contactShadowsEnabled = false;
            pipeline.shadowPcssStrength = 0.72f;
            pipeline.cloudShadowsEnabled = true;
            pipeline.directSunStrength = 1.36f;
            pipeline.skyAmbientStrength = 0.36f;
            pipeline.minimumAmbient = 0.055f;
            pipeline.contactShadowStrength = 0.12f;
            pipeline.cloudShadowStrength = 0.28f;
            pipeline.cloudShadowScale = 0.0045f;
            pipeline.cloudShadowSpeed = 0.018f;
            pipeline.shadowMinLight = 0.08f;
            pipeline.shadowContrast = 1.28f;
            pipeline.shadowTintStrength = 0.28f;
            pipeline.blockLightStrength = 1.0f;
            pipeline.fakeBounceStrength = 0.06f;
            world.getWeatherSystem().setDebugWeatherPreset(WeatherType::Clear);
            pipeline.aerialStrength = 0.48f;
            pipeline.horizonScatterStrength = 0.70f;
            pipeline.volumetricFogStrength = 0.52f;
            pipeline.bloomThreshold = 0.0f;
            pipeline.bloomStrength = 1.0f;
            pipeline.sunRaysEnabled = false;
            pipeline.dofEnabled = false;
            pipeline.autoExposureEnabled = true;
            pipeline.autoExposureMin = 0.001f;
            pipeline.autoExposureMax = 64.0f;
            pipeline.autoExposureSpeed = 1.0f;
            pipeline.autoExposureBias = 0.0f;
            pipeline.exposure = 12.0f;
            pipeline.vibrance = 0.0f;
            pipeline.kappaGradingStrength = 0.0f;
            pipeline.highlightCompression = 0.0f;
            pipeline.filmEmulationStrength = 0.0f;
            pipeline.redModifierStrength = 0.35f;
            pipeline.colorLumaR = 1.02f;
            pipeline.colorLumaG = 1.0f;
            pipeline.colorLumaB = 0.96f;
            pipeline.albedoDesaturation = 0.0f;
            pipeline.sunWarmth = 0.34f;
            pipeline.skyCoolness = 0.18f;
            pipeline.shadowDesaturation = 0.22f;
            pipeline.splitToneStrength = 0.0f;
            pipeline.vignetteStrength = 0.0f;
            pipeline.sharpenStrength = 0.3f;
            pipeline.saturation = 1.0f;
            pipeline.contrast = 1.0f;
            pipelineChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Preset Contrast")) {
            pipeline.tonemapMode = 1;

            pipeline.softShadowsEnabled = true;
            pipeline.pcssShadowsEnabled = true;
            pipeline.contactShadowsEnabled = false;
            pipeline.shadowPcssStrength = 0.82f;
            pipeline.cloudShadowsEnabled = true;
            pipeline.directSunStrength = 1.58f;
            pipeline.skyAmbientStrength = 0.28f;
            pipeline.minimumAmbient = 0.04f;
            pipeline.contactShadowStrength = 0.16f;
            pipeline.cloudShadowStrength = 0.28f;
            pipeline.cloudShadowScale = 0.0055f;
            pipeline.cloudShadowSpeed = 0.020f;
            pipeline.shadowMinLight = 0.055f;
            pipeline.shadowContrast = 1.52f;
            pipeline.shadowTintStrength = 0.34f;
            pipeline.blockLightStrength = 1.05f;
            pipeline.fakeBounceStrength = 0.08f;
            world.getWeatherSystem().setDebugWeatherPreset(WeatherType::Clear);
            pipeline.aerialStrength = 0.58f;
            pipeline.horizonScatterStrength = 0.82f;
            pipeline.volumetricFogStrength = 0.68f;
            pipeline.bloomThreshold = 0.0f;
            pipeline.bloomStrength = 1.0f;
            pipeline.sunRaysEnabled = false;
            pipeline.dofEnabled = false;
            pipeline.autoExposureEnabled = true;
            pipeline.autoExposureMin = 0.001f;
            pipeline.autoExposureMax = 64.0f;
            pipeline.autoExposureSpeed = 1.0f;
            pipeline.autoExposureBias = 0.0f;
            pipeline.exposure = 12.0f;
            pipeline.vibrance = 0.04f;
            pipeline.kappaGradingStrength = 0.0f;
            pipeline.highlightCompression = 0.0f;
            pipeline.filmEmulationStrength = 0.0f;
            pipeline.redModifierStrength = 0.45f;
            pipeline.colorLumaR = 1.04f;
            pipeline.colorLumaG = 1.0f;
            pipeline.colorLumaB = 0.93f;
            pipeline.albedoDesaturation = 0.0f;
            pipeline.sunWarmth = 0.48f;
            pipeline.skyCoolness = 0.24f;
            pipeline.shadowDesaturation = 0.34f;
            pipeline.splitToneStrength = 0.0f;
            pipeline.vignetteStrength = 0.0f;
            pipeline.sharpenStrength = 0.3f;
            pipeline.saturation = 1.0f;
            pipeline.contrast = 1.06f;
            pipelineChanged = true;
        }
        pipelineChanged |= ImGui::SliderInt("Shadow Resolution", &pipeline.shadowResolution, 512, 4096);
        pipelineChanged |= ImGui::SliderFloat("Shadow Distance", &pipeline.shadowDistance, 64.0f, 192.0f, "%.1f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Softness", &pipeline.shadowSoftness, 0.1f, 4.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("PCSS Strength", &pipeline.shadowPcssStrength, 0.0f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Const Bias", &pipeline.shadowConstantBias, 0.0f, 0.004f, "%.4f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Slope Bias", &pipeline.shadowSlopeBias, 0.0f, 0.012f, "%.4f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Normal Offset", &pipeline.shadowNormalOffset, 0.0f, 0.12f, "%.3f");
        pipelineChanged |= ImGui::SliderFloat("Contact Shadow Strength", &pipeline.contactShadowStrength, 0.0f, 0.6f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Cloud Shadow Strength", &pipeline.cloudShadowStrength, 0.0f, 0.8f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Cloud Shadow Scale", &pipeline.cloudShadowScale, 0.001f, 0.02f, "%.4f");
        pipelineChanged |= ImGui::SliderFloat("Cloud Shadow Speed", &pipeline.cloudShadowSpeed, 0.0f, 0.08f, "%.3f");
        pipelineChanged |= ImGui::SliderFloat("Post Sun Ray Strength", &pipeline.sunRayStrength, 0.0f, 0.6f, "%.2f");
        ImGui::TextDisabled("VFog Light Strength: deprecated (DerivativeMain path ignores it)");
        pipelineChanged |= ImGui::SliderFloat("VFog Shadow Bias Scale", &pipeline.volumetricShadowBiasScale, 0.0f, 4.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Color Temperature", &pipeline.colorTemperature, 0.0f, 2.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Vibrance", &pipeline.vibrance, -0.5f, 0.8f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Kappa Grade", &pipeline.kappaGradingStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Highlight Compress", &pipeline.highlightCompression, 0.0f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Film Emulation", &pipeline.filmEmulationStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Red Modifier", &pipeline.redModifierStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Channel R", &pipeline.colorLumaR, 0.5f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Channel G", &pipeline.colorLumaG, 0.5f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Channel B", &pipeline.colorLumaB, 0.5f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Albedo Desat", &pipeline.albedoDesaturation, 0.0f, 0.8f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Sun Warmth", &pipeline.sunWarmth, 0.0f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Sky Coolness", &pipeline.skyCoolness, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Desat", &pipeline.shadowDesaturation, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Split Tone", &pipeline.splitToneStrength, 0.0f, 1.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Vignette", &pipeline.vignetteStrength, 0.0f, 0.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Tint", &pipeline.shadowTintStrength, 0.0f, 0.8f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Direct Sun", &pipeline.directSunStrength, 0.0f, 3.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Sky Ambient", &pipeline.skyAmbientStrength, 0.0f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Minimum Ambient", &pipeline.minimumAmbient, 0.0f, 0.4f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Min Light", &pipeline.shadowMinLight, 0.0f, 0.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Shadow Contrast", &pipeline.shadowContrast, 0.5f, 2.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Block Light", &pipeline.blockLightStrength, 0.0f, 2.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Fake Bounce", &pipeline.fakeBounceStrength, 0.0f, 0.3f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Aerial Strength", &pipeline.aerialStrength, 0.0f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Horizon Scatter", &pipeline.horizonScatterStrength, 0.0f, 1.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Volumetric Fog Strength", &pipeline.volumetricFogStrength, 0.0f, 1.2f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Noise Dither", &pipeline.noiseDitherStrength, 0.0f, 0.05f, "%.3f");
        pipelineChanged |= ImGui::SliderFloat("CAS Sharpen", &pipeline.sharpenStrength, 0.0f, 0.5f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("SSAO Radius", &pipeline.ssaoRadius, 0.25f, 8.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("SSAO Strength", &pipeline.ssaoStrength, 0.0f, 2.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Manual Exposure Value", &pipeline.exposure, 0.1f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        pipelineChanged |= ImGui::SliderFloat("Gamma", &pipeline.gamma, 1.0f, 3.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Saturation", &pipeline.saturation, 0.0f, 2.0f, "%.2f");
        pipelineChanged |= ImGui::SliderFloat("Contrast", &pipeline.contrast, 0.5f, 2.0f, "%.2f");
        ImGui::Text("Hybrid Deferred: %s", render.isHybridDeferredReady() ? "ready" : "not ready");
        if (pipelineChanged) {
            render.setRenderPipelineSettings(pipeline);
        }

        ImGui::Separator();
        ImGui::Text("Distance Fog");
        Renderer::FogSettings fog = render.getFogSettings();
        bool fogPresetApplied = false;
        if (ImGui::Button("Natural Distance")) {
            render.setFogEnabled(true);
            render.setFogMode(Renderer::FogMode::Linear);
            render.setFogAutoDistanceEnabled(true);
            render.setFogAutoEndOffsetChunks(-0.25f);
            render.setFogAutoFadeWidthChunks(2.5f);
            render.setFogDensity(0.006f);
            fogPresetApplied = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cinematic Haze")) {
            render.setFogEnabled(true);
            render.setFogMode(Renderer::FogMode::Exp2);
            render.setFogAutoDistanceEnabled(true);
            render.setFogAutoEndOffsetChunks(-0.8f);
            render.setFogAutoFadeWidthChunks(3.0f);
            render.setFogDensity(0.020f);
            fogPresetApplied = true;
        }
        if (fogPresetApplied) {
            fog = render.getFogSettings();
        }

        bool fogEnabled = fog.enabled;
        if (ImGui::Checkbox("Enable Fog", &fogEnabled)) {
            render.setFogEnabled(fogEnabled);
            fog.enabled = fogEnabled;
        }

        int fogMode = static_cast<int>(fog.mode);
        static constexpr const char* kFogModeItems[] = { "Linear", "Exp", "Exp2" };
        if (ImGui::Combo("Fog Mode", &fogMode, kFogModeItems, IM_ARRAYSIZE(kFogModeItems))) {
            render.setFogMode(static_cast<Renderer::FogMode>(fogMode));
        }

        float fogColor[3] = { fog.color.x, fog.color.y, fog.color.z };
        if (ImGui::ColorEdit3("Fog Color", fogColor)) {
            render.setFogColor(glm::vec3(fogColor[0], fogColor[1], fogColor[2]));
        }

        bool fogAutoDistance = fog.autoDistanceByRenderDistance;
        if (ImGui::Checkbox("Auto Distance (Render Distance)", &fogAutoDistance)) {
            render.setFogAutoDistanceEnabled(fogAutoDistance);
            fog.autoDistanceByRenderDistance = fogAutoDistance;
        }

        float fogAutoEndOffset = fog.autoEndOffsetChunks;
        if (ImGui::SliderFloat("Auto End Offset (chunks)", &fogAutoEndOffset, -2.0f, 1.0f, "%.2f")) {
            render.setFogAutoEndOffsetChunks(fogAutoEndOffset);
            fog.autoEndOffsetChunks = fogAutoEndOffset;
        }

        float fogAutoFadeWidth = fog.autoFadeWidthChunks;
        if (ImGui::SliderFloat("Auto Fade Width (chunks)", &fogAutoFadeWidth, 0.25f, 4.0f, "%.2f")) {
            render.setFogAutoFadeWidthChunks(fogAutoFadeWidth);
            fog.autoFadeWidthChunks = fogAutoFadeWidth;
        }

        if (fog.autoDistanceByRenderDistance) {
            const float chunkSize = static_cast<float>(Chunk::SIZE_X);
            const float renderDistanceChunks = static_cast<float>(std::max(1, world.getRenderDistance()));
            const float autoEnd = std::max(0.0f, (renderDistanceChunks + fog.autoEndOffsetChunks) * chunkSize);
            const float autoStart = std::max(0.0f, autoEnd - fog.autoFadeWidthChunks * chunkSize);
            ImGui::Text("Auto Fog Range: %.1f -> %.1f", autoStart, autoEnd);
        }

        float fogStart = fog.startDistance;
        float fogEnd = fog.endDistance;
        bool fogDistanceChanged = false;
        if (fog.autoDistanceByRenderDistance) {
            ImGui::BeginDisabled();
        }
        fogDistanceChanged |= ImGui::SliderFloat("Fog Start", &fogStart, 0.0f, 600.0f, "%.1f");
        fogDistanceChanged |= ImGui::SliderFloat("Fog End", &fogEnd, 1.0f, 800.0f, "%.1f");
        if (fog.autoDistanceByRenderDistance) {
            ImGui::EndDisabled();
        }
        if (fogDistanceChanged) {
            render.setFogLinearDistances(fogStart, fogEnd);
        }

        float fogDensity = fog.density;
        if (ImGui::SliderFloat("Fog Density", &fogDensity, 0.001f, 0.05f, "%.4f")) {
            render.setFogDensity(fogDensity);
        }

        const Renderer::MeshingFrameStats meshingStats = render.getMeshingFrameStats();
        ImGui::Text("Meshing Submitted: %d / frame", meshingStats.submitted);
        ImGui::Text("Meshing Completed: %d / frame", meshingStats.completed);
        ImGui::Text("Meshing In-Flight: %d", meshingStats.inFlight);
        ImGui::Text("Meshing Build: last %.3f ms, avg %.3f ms", meshingStats.lastBuildMs, meshingStats.averageBuildMs);

        const LightFrameStats lightStats = world.getLightFrameStats();
        ImGui::Text("Light Submitted: %d / frame", lightStats.submitted);
        ImGui::Text("Light Completed: %d / frame", lightStats.completed);
        ImGui::Text("Light In-Flight: %d", lightStats.inFlight);
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

        const Renderer::CullingFrameStats cullingStats = render.getCullingFrameStats();
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
            ImGui::Text("Left:   %d", cullingStats.chunkCulledByPlane[static_cast<size_t>(Renderer::FrustumPlane::Left)]);
            ImGui::Text("Right:  %d", cullingStats.chunkCulledByPlane[static_cast<size_t>(Renderer::FrustumPlane::Right)]);
            ImGui::Text("Bottom: %d", cullingStats.chunkCulledByPlane[static_cast<size_t>(Renderer::FrustumPlane::Bottom)]);
            ImGui::Text("Top:    %d", cullingStats.chunkCulledByPlane[static_cast<size_t>(Renderer::FrustumPlane::Top)]);
            ImGui::Text("Near:   %d", cullingStats.chunkCulledByPlane[static_cast<size_t>(Renderer::FrustumPlane::Near)]);
            ImGui::Text("Far:    %d", cullingStats.chunkCulledByPlane[static_cast<size_t>(Renderer::FrustumPlane::Far)]);
            ImGui::Unindent();
        }
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
