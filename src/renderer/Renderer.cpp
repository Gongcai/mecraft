//
// Created by Caiwe on 2026/3/21.
//

#include "Renderer.h"



void Renderer::init(ResourceMgr &resourceMgr) {
    m_chunkShader = resourceMgr.getShader("chunk");
    m_uiShader = resourceMgr.getShader("ui");
    m_testCube = new TestCube({0.0f,0.0f,0.0f}, resourceMgr);
}

void Renderer::shutdown() {
}

void Renderer::render(const Camera &camera, const Window &window) {
    m_testCube->update();
    beginFrame(camera, window);
    renderWorld();
    endFrame(window);
}

void Renderer::beginFrame(const Camera &camera, const Window &window) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    m_projection = camera.getProjectionMatrix(window.getAspectRatio());
    m_view = camera.getViewMatrix();
}

void Renderer::renderWorld() {
    m_testCube->setViewProjection(m_projection * m_view);
    m_testCube->draw();
}

void Renderer::endFrame(const Window &window) {
    window.swapBuffers();
}

void Renderer::updateFrustum(const glm::mat4 &viewProj) {
    }

bool Renderer::isChunkInFrustum(const glm::vec3 &chunkMin, const glm::vec3 &chunkMax) const {
    return false;
}
