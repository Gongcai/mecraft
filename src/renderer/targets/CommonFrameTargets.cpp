#include "CommonFrameTargets.h"
#include "../debug/RenderDebugLabels.h"
#include <cstdio>

CommonFrameTargets::~CommonFrameTargets() {
    shutdown();
}

bool CommonFrameTargets::init() {
    // Fullscreen triangle VAO
    glGenVertexArrays(1, &m_fullscreenVao);
    return true;
}

void CommonFrameTargets::shutdown() {
    destroyFramebuffers();
    destroyFullscreenTriangle();
    m_ready = false;
}

bool CommonFrameTargets::ensureSize(int width, int height) {
    if (width == m_width && height == m_height && m_ready) {
        return false; // No resize needed
    }

    destroyFramebuffers();
    m_width = width;
    m_height = height;

    if (width <= 0 || height <= 0) {
        return false;
    }

    // Scene color (RGBA16F HDR)
    glGenTextures(1, &m_sceneColorTex);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Scene depth (DEPTH32F)
    glGenTextures(1, &m_sceneDepthTex);
    glBindTexture(GL_TEXTURE_2D, m_sceneDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Scene color FBO
    glGenFramebuffers(1, &m_sceneColorFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneColorFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneColorTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_sceneDepthTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "CommonFrameTargets: scene color FBO incomplete\n");
        destroyFramebuffers();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Label GL objects for RenderDoc / KHR_debug inspection
    renderer::debug::labelFramebuffer(m_sceneColorFbo, "CommonTargets.SceneColor");
    renderer::debug::labelTexture(m_sceneColorTex, "CommonTargets.SceneColorTex");
    renderer::debug::labelTexture(m_sceneDepthTex, "CommonTargets.SceneDepthTex");
    renderer::debug::labelVertexArray(m_fullscreenVao, "CommonTargets.FullscreenVAO");

    m_ready = true;
    return true;
}

void CommonFrameTargets::bindSceneColor() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneColorFbo);
    glViewport(0, 0, m_width, m_height);
}

void CommonFrameTargets::bindSceneDepth() {
    // Depth is part of scene color FBO, so just bind that
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneColorFbo);
    glViewport(0, 0, m_width, m_height);
}

void CommonFrameTargets::destroyFramebuffers() {
    if (m_sceneColorFbo) { glDeleteFramebuffers(1, &m_sceneColorFbo); m_sceneColorFbo = 0; }
    if (m_sceneColorTex) { glDeleteTextures(1, &m_sceneColorTex); m_sceneColorTex = 0; }
    if (m_sceneDepthTex) { glDeleteTextures(1, &m_sceneDepthTex); m_sceneDepthTex = 0; }
}

void CommonFrameTargets::destroyFullscreenTriangle() {
    if (m_fullscreenVao) { glDeleteVertexArrays(1, &m_fullscreenVao); m_fullscreenVao = 0; }
}
