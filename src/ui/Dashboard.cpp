//
// Created by Caiwe on 2026/3/25.
//

#include "Dashboard.h"

#include <cstddef>

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

void Dashboard::render( Player &player,  World &world,  Camera &camera,Renderer &render) {
    // (Your code calls glfwPollEvents())
    // ...
    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    showCameraStats(camera);
    showWorldStats(world);
    showPerformanceStats(render);
    // Rendering
    // (Your code clears your framebuffer, renders your other stuff etc.)
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    // (Your code calls glfwSwapBuffers() etc.)
}

void Dashboard::showPlayerStats( Player &player) {
}

void Dashboard::showWorldStats( World &world) {
    ImGui::Begin("World Stats");
    ImGui::Text("Render Distance: %d chunks", world.getRenderDistance());
    ImGui::Text("Loaded Chunks: %zu", world.getActiveChunks().size());
    ImGui::Text("Total Vertices: %zu", world.getTotalVertexCount());
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

void Dashboard::showPerformanceStats(Renderer &render) {
    ImGui::Begin("Performance Stats");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Text("Draw Calls: %d", render.getDrawCallCount());

    int submitBudget = render.getMeshingSubmitBudget();
    if (ImGui::SliderInt("Meshing Submit Budget", &submitBudget, 1, 64)) {
        render.setMeshingSubmitBudget(submitBudget);
    }

    int regionChunkSize = render.getRegionChunkSize();
    if (ImGui::SliderInt("Region Chunk Size", &regionChunkSize, 1, 16)) {
        render.setRegionChunkSize(regionChunkSize);
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

    ImGui::End();
}
