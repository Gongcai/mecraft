//
// Created by Caiwe on 2026/3/25.
//

// Dashboard 调试 UI 仅在 Debug 模式下编译
#ifndef NDEBUG

#include "Dashboard.h"

#include "UIRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>

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

void Dashboard::render(Player &player, World &world, Camera &camera, Renderer &render,
                       UIRenderer& uiRenderer, const FrameProfilerStats& profilerStats) {
    // (Your code calls glfwPollEvents())
    // ...
    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    showPlayerStats(player);
    showCameraStats(camera);
    showWorldStats(world, player);
    showPerformanceStats(render, profilerStats);
    showCrosshairSettings(uiRenderer);
    showHotbarSettings(uiRenderer);
    // Rendering
    // (Your code clears your framebuffer, renders your other stuff etc.)
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    // (Your code calls glfwSwapBuffers() etc.)
}

void Dashboard::showPlayerStats( Player &player) {
    ImGui::Begin("Player Stats");

    const glm::vec3 position = player.getPosition();
    ImGui::Text("Position: (%.2f, %.2f, %.2f)", position.x, position.y, position.z);
    ImGui::Text("Eye Height: %.3f", player.getEyeHeight());
    ImGui::Text("Moving: %s", player.isMoving() ? "Yes" : "No");
    ImGui::Text("Sprinting: %s", player.isSprinting() ? "Yes" : "No");

    float bobAmplitude = player.getEyeBobAmplitude();
    if (ImGui::SliderFloat("Bob Amplitude", &bobAmplitude, 0.0f, 0.3f, "%.4f")) {
        player.setEyeBobAmplitude(bobAmplitude);
    }

    float horizontalBobAmplitude = player.getEyeBobHorizontalAmplitude();
    if (ImGui::SliderFloat("Horizontal Bob Amplitude", &horizontalBobAmplitude, 0.0f, 0.3f, "%.4f")) {
        player.setEyeBobHorizontalAmplitude(horizontalBobAmplitude);
    }

    float bobFrequency = player.getEyeBobFrequency();
    ImGui::Text("Bob Frequency: %.2f", bobFrequency);
    if (ImGui::SliderFloat("Bob Frequency", &bobFrequency, 0.0f, 40.0f, "%.2f")) {
        player.setEyeBobFrequency(bobFrequency);
    }

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kRadToDeg = 180.0f / kPi;
    constexpr float kDegToRad = kPi / 180.0f;
    float phaseOffsetDegrees = player.getEyeBobPhaseOffset() * kRadToDeg;
    if (ImGui::SliderFloat("Horizontal Phase Offset (deg)", &phaseOffsetDegrees, -180.0f, 180.0f, "%.1f")) {
        player.setEyeBobPhaseOffset(phaseOffsetDegrees * kDegToRad);
    }

    constexpr int kCurveSamples = 240;
    constexpr float kPreviewSeconds = 4.0f;
    constexpr float kMaxVerticalPreview = 0.09f; // 0.3^2 from slider upper bound.
    constexpr float kMaxHorizontalPreview = 0.3f; // Matches horizontal amplitude slider upper bound.
    std::array<float, kCurveSamples> verticalCurve{};
    std::array<float, kCurveSamples> horizontalCurve{};
    const float phaseOffset = player.getEyeBobPhaseOffset();
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

    ImGui::End();
}

void Dashboard::showWorldStats(World& world, const Player& player) {
    ImGui::Begin("World Stats");
    const glm::vec3 position = player.getPosition();
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
    ImGui::End();
}

void Dashboard::showCameraStats( Camera &camera) {
    ImGui::Begin("Camera Stats");
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
    ImGui::End();
}

void Dashboard::showPerformanceStats(Renderer &render, const FrameProfilerStats& profilerStats) {
    ImGui::Begin("Performance Stats");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
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

    ImGui::Text("Draw Calls: %d", render.getDrawCallCount());
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

    const Renderer::MeshingFrameStats meshingStats = render.getMeshingFrameStats();
    ImGui::Text("Meshing Submitted: %d / frame", meshingStats.submitted);
    ImGui::Text("Meshing Completed: %d / frame", meshingStats.completed);
    ImGui::Text("Meshing In-Flight: %d", meshingStats.inFlight);

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

    ImGui::End();
}

void Dashboard::showCrosshairSettings(UIRenderer& uiRenderer) {
    ImGui::Begin("Crosshair Settings");

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

    ImGui::End();
}

void Dashboard::showHotbarSettings(UIRenderer& uiRenderer) {
    ImGui::Begin("Hotbar Settings");

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

    ImGui::End();
}

#endif // NDEBUG

