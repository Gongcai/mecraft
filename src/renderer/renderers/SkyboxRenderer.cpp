#include "SkyboxRenderer.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../core/Shader.h"
#include "../../resource/ResourceMgr.h"

namespace {
constexpr float kCubeVertices[] = {
    // Back face (-Z)
    -1.0f,  1.0f, -1.0f,   -1.0f, -1.0f, -1.0f,    1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,    1.0f,  1.0f, -1.0f,   -1.0f,  1.0f, -1.0f,
    // Front face (+Z)
    -1.0f, -1.0f,  1.0f,   -1.0f,  1.0f,  1.0f,    1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,    1.0f, -1.0f,  1.0f,   -1.0f, -1.0f,  1.0f,
    // Left face (-X)
    -1.0f,  1.0f,  1.0f,   -1.0f,  1.0f, -1.0f,   -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f,  1.0f,   -1.0f,  1.0f,  1.0f,
    // Right face (+X)
     1.0f,  1.0f, -1.0f,    1.0f,  1.0f,  1.0f,    1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,    1.0f, -1.0f, -1.0f,    1.0f,  1.0f, -1.0f,
    // Top face (+Y)
    -1.0f,  1.0f,  1.0f,    1.0f,  1.0f,  1.0f,    1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,   -1.0f,  1.0f, -1.0f,   -1.0f,  1.0f,  1.0f,
    // Bottom face (-Y)
    -1.0f, -1.0f, -1.0f,    1.0f, -1.0f, -1.0f,    1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,   -1.0f, -1.0f,  1.0f,   -1.0f, -1.0f, -1.0f,
};

GLuint createFboWithColor(GLuint& colorTex, int width, int height) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!complete) {
        glDeleteTextures(1, &colorTex);
        colorTex = 0;
        glDeleteFramebuffers(1, &fbo);
        return 0;
    }
    return fbo;
}
}

void SkyboxRenderer::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("skybox");
    m_blurShader = resourceMgr.getShader("blur");
    m_cubemapTexture = resourceMgr.getCubemap("menu_skybox");
    initCubeMesh();

    glGenVertexArrays(1, &m_fullscreenVao);
}

void SkyboxRenderer::shutdown() {
    destroyBlurTargets();
    destroyCubeMesh();

    if (m_fullscreenVao != 0) {
        glDeleteVertexArrays(1, &m_fullscreenVao);
        m_fullscreenVao = 0;
    }

    m_shader = nullptr;
    m_blurShader = nullptr;
    m_cubemapTexture = 0;
}

void SkyboxRenderer::render(float aspect, float yawDegrees, float pitchDegrees) {
    if (m_shader == nullptr || m_cubemapTexture == 0 || m_cubeVao == 0) {
        return;
    }

    GLint viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int vpWidth = viewport[2];
    const int vpHeight = viewport[3];

    if (vpWidth <= 0 || vpHeight <= 0) {
        return;
    }

    // Half resolution for blur (matches Minecraft's approach)
    const int blurW = std::max(1, vpWidth / 2);
    const int blurH = std::max(1, vpHeight / 2);

    if (!ensureBlurTargets(blurW, blurH)) {
        return;
    }

    // --- Pass 1: Render skybox to scene FBO ---
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    glViewport(0, 0, blurW, blurH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glm::mat4 view(1.0f);
    view = glm::rotate(view, glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    view = glm::rotate(view, glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 projection = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 100.0f);

    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    m_shader->use();
    m_shader->setMat4("uView", view);
    m_shader->setMat4("uProjection", projection);
    m_shader->setInt("uSkybox", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemapTexture);

    glBindVertexArray(m_cubeVao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    if (m_blurShader == nullptr || m_fullscreenVao == 0) {
        // No blur shader — just blit scene to screen
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, blurW, blurH, 0, 0, vpWidth, vpHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, vpWidth, vpHeight);
        glEnable(GL_CULL_FACE);
        glDepthFunc(GL_LESS);
        return;
    }

    // --- Pass 2: Horizontal blur (scene -> ping) ---
    glBindFramebuffer(GL_FRAMEBUFFER, m_pingFbo);
    glViewport(0, 0, blurW, blurH);

    m_blurShader->use();
    m_blurShader->setInt("uTexture", 0);
    m_blurShader->setVec2("uDirection", glm::vec2(1.0f / static_cast<float>(blurW), 0.0f));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // --- Pass 3: Vertical blur (ping -> pong) ---
    glBindFramebuffer(GL_FRAMEBUFFER, m_pongFbo);
    glViewport(0, 0, blurW, blurH);

    m_blurShader->setVec2("uDirection", glm::vec2(0.0f, 1.0f / static_cast<float>(blurH)));

    glBindTexture(GL_TEXTURE_2D, m_pingColorTex);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // --- Pass 4: Blit blurred result to screen ---
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_pongFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, blurW, blurH, 0, 0, vpWidth, vpHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, vpWidth, vpHeight);

    glEnable(GL_CULL_FACE);
    glDepthFunc(GL_LESS);
}

bool SkyboxRenderer::ensureBlurTargets(int width, int height) {
    if (width == m_blurWidth && height == m_blurHeight && m_sceneFbo != 0) {
        return true;
    }

    destroyBlurTargets();

    m_sceneFbo = createFboWithColor(m_sceneColorTex, width, height);
    m_pingFbo = createFboWithColor(m_pingColorTex, width, height);
    m_pongFbo = createFboWithColor(m_pongColorTex, width, height);

    if (m_sceneFbo == 0 || m_pingFbo == 0 || m_pongFbo == 0) {
        destroyBlurTargets();
        return false;
    }

    m_blurWidth = width;
    m_blurHeight = height;
    return true;
}

void SkyboxRenderer::destroyBlurTargets() {
    auto deleteFbo = [](GLuint& fbo, GLuint& tex) {
        if (tex != 0) { glDeleteTextures(1, &tex); tex = 0; }
        if (fbo != 0) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
    };
    deleteFbo(m_sceneFbo, m_sceneColorTex);
    deleteFbo(m_pingFbo, m_pingColorTex);
    deleteFbo(m_pongFbo, m_pongColorTex);
    m_blurWidth = 0;
    m_blurHeight = 0;
}

void SkyboxRenderer::initCubeMesh() {
    if (m_cubeVao != 0) return;

    glGenVertexArrays(1, &m_cubeVao);
    glGenBuffers(1, &m_cubeVbo);

    glBindVertexArray(m_cubeVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_cubeVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVertices), kCubeVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void SkyboxRenderer::destroyCubeMesh() {
    if (m_cubeVbo != 0) {
        glDeleteBuffers(1, &m_cubeVbo);
        m_cubeVbo = 0;
    }
    if (m_cubeVao != 0) {
        glDeleteVertexArrays(1, &m_cubeVao);
        m_cubeVao = 0;
    }
}
