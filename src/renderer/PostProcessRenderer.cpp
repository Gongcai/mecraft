#include "PostProcessRenderer.h"

#include "Shader.h"
#include "../core/Window.h"
#include "../resource/ResourceMgr.h"

#include <algorithm>

PostProcessRenderer::~PostProcessRenderer() {
    shutdown();
}

void PostProcessRenderer::init(ResourceMgr& resourceMgr) {
    m_postProcessShader = resourceMgr.getShader("postprocess");
    initFullscreenTriangle();
}

void PostProcessRenderer::shutdown() {
    destroyRenderTargets();
    destroyFullscreenTriangle();
    m_postProcessShader = nullptr;
    m_sceneCaptured = false;
    m_targetWidth = 0;
    m_targetHeight = 0;
}

void PostProcessRenderer::setEffects(const PostProcessEffects& effects) {
    m_effects = effects;
    m_effects.underwaterStrength = std::clamp(m_effects.underwaterStrength, 0.0f, 1.0f);
}

void PostProcessRenderer::beginScene(const Window& window) {
    m_sceneCaptured = false;

    const int width = window.getWidth();
    const int height = window.getHeight();
    if (m_postProcessShader == nullptr || width <= 0 || height <= 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, std::max(1, width), std::max(1, height));
        return;
    }

    if (!ensureRenderTargets(width, height)) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    glViewport(0, 0, width, height);
    m_sceneCaptured = true;
}

void PostProcessRenderer::endSceneAndComposite(const Window& window) {
    const int width = std::max(1, window.getWidth());
    const int height = std::max(1, window.getHeight());

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);

    if (!m_sceneCaptured || m_postProcessShader == nullptr || m_fullscreenVao == 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    m_postProcessShader->use();
    m_postProcessShader->setInt("uSceneTex", 0);
    m_postProcessShader->setBool("uUnderwaterEnabled", m_effects.underwaterEnabled);
    m_postProcessShader->setVec3("uUnderwaterTint", m_effects.underwaterTint);
    m_postProcessShader->setFloat("uUnderwaterStrength", m_effects.underwaterStrength);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

bool PostProcessRenderer::ensureRenderTargets(const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (m_sceneFbo != 0 && m_targetWidth == width && m_targetHeight == height) {
        return true;
    }

    destroyRenderTargets();

    glGenFramebuffers(1, &m_sceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);

    glGenTextures(1, &m_sceneColorTex);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneColorTex, 0);

    glGenRenderbuffers(1, &m_sceneDepthStencilRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_sceneDepthStencilRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_sceneDepthStencilRbo);

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!complete) {
        destroyRenderTargets();
        return false;
    }

    m_targetWidth = width;
    m_targetHeight = height;
    return true;
}

void PostProcessRenderer::destroyRenderTargets() {
    if (m_sceneDepthStencilRbo != 0) {
        glDeleteRenderbuffers(1, &m_sceneDepthStencilRbo);
        m_sceneDepthStencilRbo = 0;
    }
    if (m_sceneColorTex != 0) {
        glDeleteTextures(1, &m_sceneColorTex);
        m_sceneColorTex = 0;
    }
    if (m_sceneFbo != 0) {
        glDeleteFramebuffers(1, &m_sceneFbo);
        m_sceneFbo = 0;
    }
}

void PostProcessRenderer::initFullscreenTriangle() {
    if (m_fullscreenVao != 0) {
        return;
    }
    glGenVertexArrays(1, &m_fullscreenVao);
}

void PostProcessRenderer::destroyFullscreenTriangle() {
    if (m_fullscreenVao != 0) {
        glDeleteVertexArrays(1, &m_fullscreenVao);
        m_fullscreenVao = 0;
    }
}


