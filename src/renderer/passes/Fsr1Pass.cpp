#include "Fsr1Pass.h"

#include "../core/Shader.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../../resource/ResourceMgr.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>

Fsr1Pass::~Fsr1Pass() {
    shutdown();
}

void Fsr1Pass::init(ResourceMgr& resourceMgr) {
    m_easuShader = resourceMgr.getShader("fsr1_easu");
    m_rcasShader = resourceMgr.getShader("fsr1_rcas");
    initFullscreenTriangle();
}

void Fsr1Pass::shutdown() {
    destroyTargets();
    destroyFullscreenTriangle();
    m_easuShader = nullptr;
    m_rcasShader = nullptr;
}

bool Fsr1Pass::execute(RhiDevice& rhiDevice,
                       const RhiTextureViewHandle swapchainColorView,
                       uint32_t inputTex,
                       const int inputWidth,
                       const int inputHeight,
                       const int outputWidth,
                       const int outputHeight,
                       const float sharpness) {
    if (inputTex == 0 || inputWidth <= 0 || inputHeight <= 0 || outputWidth <= 0 || outputHeight <= 0 ||
        m_easuShader == nullptr || m_rcasShader == nullptr || m_fullscreenVao == 0) {
        return false;
    }
    if (!ensureTargets(outputWidth, outputHeight)) {
        return false;
    }

    glm::vec4 con0;
    glm::vec4 con1;
    glm::vec4 con2;
    glm::vec4 con3;
    populateEasuConstants(con0, con1, con2, con3,
                          static_cast<float>(inputWidth),
                          static_cast<float>(inputHeight),
                          static_cast<float>(inputWidth),
                          static_cast<float>(inputHeight),
                          static_cast<float>(outputWidth),
                          static_cast<float>(outputHeight));

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, m_easuFbo);
    glViewport(0, 0, outputWidth, outputHeight);
    m_easuShader->use();
    m_easuShader->setInt("uInputTex", 0);
    m_easuShader->setVec4("uCon0", con0);
    m_easuShader->setVec4("uCon1", con1);
    m_easuShader->setVec4("uCon2", con2);
    m_easuShader->setVec4("uCon3", con3);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTex);
    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    RhiColorAttachment colorAttachment;
    colorAttachment.view = swapchainColorView;
    colorAttachment.loadOp = RhiLoadOp::Load;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "FSR1Backbuffer";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, outputWidth)),
        static_cast<uint32_t>(std::max(1, outputHeight))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1;

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);

    m_rcasShader->use();
    m_rcasShader->setInt("uInputTex", 0);
    m_rcasShader->setVec4("uCon", populateRcasConstants(sharpness));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_easuTex);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    return true;
}

bool Fsr1Pass::ensureTargets(const int width, const int height) {
    const int targetWidth = std::max(1, width);
    const int targetHeight = std::max(1, height);
    if (m_easuFbo != 0 && m_easuTex != 0 && m_width == targetWidth && m_height == targetHeight) {
        return true;
    }

    destroyTargets();
    m_width = targetWidth;
    m_height = targetHeight;

    glCreateFramebuffers(1, &m_easuFbo);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_easuTex);
    glTextureStorage2D(m_easuTex, 1, GL_RGBA8, m_width, m_height);
    glTextureParameteri(m_easuTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_easuTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_easuTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_easuTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_easuFbo, GL_COLOR_ATTACHMENT0, m_easuTex, 0);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_easuFbo, 1, &drawBuffer);
    if (glCheckNamedFramebufferStatus(m_easuFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        destroyTargets();
        return false;
    }
    return true;
}

void Fsr1Pass::destroyTargets() {
    if (m_easuTex != 0) {
        glDeleteTextures(1, &m_easuTex);
        m_easuTex = 0;
    }
    if (m_easuFbo != 0) {
        glDeleteFramebuffers(1, &m_easuFbo);
        m_easuFbo = 0;
    }
    m_width = 0;
    m_height = 0;
}

void Fsr1Pass::initFullscreenTriangle() {
    if (m_fullscreenVao == 0) {
        glGenVertexArrays(1, &m_fullscreenVao);
    }
}

void Fsr1Pass::destroyFullscreenTriangle() {
    if (m_fullscreenVao != 0) {
        glDeleteVertexArrays(1, &m_fullscreenVao);
        m_fullscreenVao = 0;
    }
}

void Fsr1Pass::populateEasuConstants(glm::vec4& con0,
                                     glm::vec4& con1,
                                     glm::vec4& con2,
                                     glm::vec4& con3,
                                     const float inputViewportWidth,
                                     const float inputViewportHeight,
                                     const float inputTextureWidth,
                                     const float inputTextureHeight,
                                     const float outputWidth,
                                     const float outputHeight) {
    const float invInputW = 1.0f / std::max(inputTextureWidth, 1.0f);
    const float invInputH = 1.0f / std::max(inputTextureHeight, 1.0f);
    const float invOutputW = 1.0f / std::max(outputWidth, 1.0f);
    const float invOutputH = 1.0f / std::max(outputHeight, 1.0f);

    con0 = glm::vec4(inputViewportWidth * invOutputW,
                     inputViewportHeight * invOutputH,
                     0.5f * inputViewportWidth * invOutputW - 0.5f,
                     0.5f * inputViewportHeight * invOutputH - 0.5f);
    con1 = glm::vec4(invInputW,
                     invInputH,
                     invInputW,
                     -invInputH);
    con2 = glm::vec4(-invInputW,
                     2.0f * invInputH,
                     invInputW,
                     2.0f * invInputH);
    con3 = glm::vec4(0.0f,
                     4.0f * invInputH,
                     0.0f,
                     0.0f);
}

glm::vec4 Fsr1Pass::populateRcasConstants(const float sharpness) {
    const float stops = std::clamp(sharpness, 0.0f, 2.0f);
    return glm::vec4(std::exp2(-stops), 0.0f, 0.0f, 0.0f);
}
