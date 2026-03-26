//
// Created by Caiwe on 2026/3/25.
//

#include "Dashboard.h"

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
    ImGui::End();
}
