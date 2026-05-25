#include "ScreenTransition.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "../../renderer/Shader.h"
#include "../../resource/ResourceMgr.h"

ScreenTransition::~ScreenTransition() {
    shutdown();
}

void ScreenTransition::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("ui_color");
    initMesh();
}

void ScreenTransition::shutdown() {
    cleanupMesh();
    m_shader = nullptr;
}

void ScreenTransition::initMesh() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    float vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ScreenTransition::cleanupMesh() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

void ScreenTransition::startFadeOut(float duration) {
    m_alphaTween.start(0.0f, 1.0f, duration, EasingType::Linear);
}

void ScreenTransition::startFadeIn(float duration) {
    m_alphaTween.start(1.0f, 0.0f, duration, EasingType::Linear);
}

void ScreenTransition::tick(float dt) {
    m_alphaTween.tick(dt);
}

void ScreenTransition::render(int screenW, int screenH) const {
    if (!m_shader || m_vao == 0 || m_alphaTween.isDone()) return;

    float a = m_alphaTween.value();
    if (a <= 0.0f) return;

    // Save GL state
    GLboolean depthTest;
    glGetBooleanv(GL_DEPTH_TEST, &depthTest);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Use NDC coordinates directly (the shader expects pixel coords, but we pass NDC and set screen to 1,1)
    // Actually, the ui_color shader divides by uScreenSize. To render full-screen in NDC,
    // we use vertices in NDC space and set uScreenSize to (1,1) so division is identity.
    m_shader->use();
    m_shader->setVec2("uScreenSize", glm::vec2(1.0f, 1.0f));
    m_shader->setVec4("uColor", glm::vec4(0.0f, 0.0f, 0.0f, a));

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // Restore GL state
    if (depthTest) glEnable(GL_DEPTH_TEST);
}
