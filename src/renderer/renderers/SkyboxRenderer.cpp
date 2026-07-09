#include "SkyboxRenderer.h"

#include "../../Diagnostics.h"
#include <algorithm>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

#include "../core/Shader.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
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

bool createBlurColorTexture(GLuint& colorTex, int width, int height) {
    glCreateTextures(GL_TEXTURE_2D, 1, &colorTex);
    glTextureStorage2D(colorTex, 1, GL_RGBA8, width, height);
    glTextureParameteri(colorTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(colorTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(colorTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(colorTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return colorTex != 0;
}

RhiTextureHandle registerBlurTargetTexture(const GLuint texture,
                                           const int width,
                                           const int height) {
    return renderer::rhi::gl::registerTexture({
        texture,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba8Unorm,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        1u,
        1u,
        1u,
        rhiFlag(RhiTextureUsage::Sampled) |
            rhiFlag(RhiTextureUsage::ColorAttachment) |
            rhiFlag(RhiTextureUsage::TransferSrc),
        false
    });
}

bool blitBlurTargetToSwapchain(RhiDevice& rhiDevice,
                               const RhiTextureHandle source,
                               const int width,
                               const int height) {
    if (!source.isValid() || !renderer::rhi::gl::isTextureRegistered(source) ||
        !rhiDevice.resizeSwapchain(static_cast<uint32_t>(std::max(1, width)),
                                   static_cast<uint32_t>(std::max(1, height)))) {
        return false;
    }

    const RhiTextureViewHandle swapchainColorView = rhiDevice.currentSwapchainColorView();
    if (!swapchainColorView.isValid()) {
        return false;
    }

    RhiTextureBlit blit;
    blit.src = source;
    blit.dstView = swapchainColorView;

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.blitTexture(blit);
    rhiDevice.submitFrame(commandList);
    return true;
}

RhiTextureViewHandle createBlurTargetView(RhiDevice& rhiDevice, const RhiTextureHandle texture) {
    if (!texture.isValid()) {
        return {};
    }

    RhiTextureViewDesc desc;
    desc.texture = texture;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;
    return rhiDevice.createTextureView(desc);
}

void beginSkyboxBlurOutput(RhiCommandList& commandList,
                           const char* debugName,
                           const RhiTextureViewHandle view,
                           const int width,
                           const int height,
                           const bool clearColor) {
    RhiColorAttachment colorAttachment;
    colorAttachment.view = view;
    colorAttachment.loadOp = clearColor ? RhiLoadOp::Clear : RhiLoadOp::DontCare;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = debugName;
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, width)),
        static_cast<uint32_t>(std::max(1, height))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    commandList.beginRendering(renderingInfo);
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
    m_cubemapTexture = {};
}

void SkyboxRenderer::render(float aspect, float yawDegrees, float pitchDegrees, RhiDevice& rhiDevice) {
    if (m_shader == nullptr || m_blurShader == nullptr ||
        !m_cubemapTexture.isValid() || m_cubeVao == 0 || m_fullscreenVao == 0) {
        return;
    }
    const GLuint cubemapTextureId = static_cast<GLuint>(renderer::rhi::gl::textureId(m_cubemapTexture));
    if (cubemapTextureId == 0) {
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

    if (!ensureBlurTargets(rhiDevice, blurW, blurH)) {
        return;
    }

    RhiCommandList& commandList = rhiDevice.beginFrame();

    // --- Pass 1: Render skybox to scene FBO ---
    beginSkyboxBlurOutput(commandList, "SkyboxScene", m_sceneColorView, blurW, blurH, true);

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
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTextureId);

    glBindVertexArray(m_cubeVao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    commandList.endRendering();

    // --- Pass 2: Horizontal blur (scene -> ping) ---
    beginSkyboxBlurOutput(commandList, "SkyboxBlurHorizontal", m_pingColorView, blurW, blurH, false);

    m_blurShader->use();
    m_blurShader->setInt("uTexture", 0);
    m_blurShader->setVec2("uDirection", glm::vec2(1.0f / static_cast<float>(blurW), 0.0f));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    commandList.endRendering();

    // --- Pass 3: Vertical blur (ping -> pong) ---
    beginSkyboxBlurOutput(commandList, "SkyboxBlurVertical", m_pongColorView, blurW, blurH, false);

    m_blurShader->setVec2("uDirection", glm::vec2(0.0f, 1.0f / static_cast<float>(blurH)));

    glBindTexture(GL_TEXTURE_2D, m_pingColorTex);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    commandList.endRendering();
    rhiDevice.submitFrame(commandList);

    const bool blitted = blitBlurTargetToSwapchain(rhiDevice, m_pongColorHandle, vpWidth, vpHeight);

    glViewport(0, 0, vpWidth, vpHeight);

    glEnable(GL_CULL_FACE);
    glDepthFunc(GL_LESS);
    if (!blitted) {
        MECRAFT_LOG_STREAM(std::cerr << "[SkyboxRenderer] Failed to blit menu skybox through RHI\n");
    }
}

bool SkyboxRenderer::ensureBlurTargets(RhiDevice& rhiDevice, int width, int height) {
    if (width == m_blurWidth && height == m_blurHeight && m_sceneColorTex != 0 &&
        m_pingColorTex != 0 && m_pongColorTex != 0 &&
        m_sceneColorView.isValid() && m_pingColorView.isValid() && m_pongColorView.isValid() &&
        m_blurViewDevice == &rhiDevice) {
        return true;
    }

    destroyBlurTargets();

    if (!createBlurColorTexture(m_sceneColorTex, width, height) ||
        !createBlurColorTexture(m_pingColorTex, width, height) ||
        !createBlurColorTexture(m_pongColorTex, width, height)) {
        destroyBlurTargets();
        return false;
    }

    m_sceneColorHandle = registerBlurTargetTexture(m_sceneColorTex, width, height);
    m_pingColorHandle = registerBlurTargetTexture(m_pingColorTex, width, height);
    m_pongColorHandle = registerBlurTargetTexture(m_pongColorTex, width, height);
    m_sceneColorView = createBlurTargetView(rhiDevice, m_sceneColorHandle);
    m_pingColorView = createBlurTargetView(rhiDevice, m_pingColorHandle);
    m_pongColorView = createBlurTargetView(rhiDevice, m_pongColorHandle);

    if (!m_sceneColorHandle.isValid() || !m_pingColorHandle.isValid() ||
        !m_pongColorHandle.isValid() ||
        !m_sceneColorView.isValid() || !m_pingColorView.isValid() ||
        !m_pongColorView.isValid()) {
        destroyBlurTargets();
        return false;
    }

    m_blurViewDevice = &rhiDevice;
    m_blurWidth = width;
    m_blurHeight = height;
    return true;
}

void SkyboxRenderer::destroyBlurTargets() {
    if (m_blurViewDevice != nullptr && m_sceneColorView.isValid()) {
        m_blurViewDevice->destroyTextureView(m_sceneColorView);
    }
    if (m_blurViewDevice != nullptr && m_pingColorView.isValid()) {
        m_blurViewDevice->destroyTextureView(m_pingColorView);
    }
    if (m_blurViewDevice != nullptr && m_pongColorView.isValid()) {
        m_blurViewDevice->destroyTextureView(m_pongColorView);
    }
    m_sceneColorView = {};
    m_pingColorView = {};
    m_pongColorView = {};
    m_blurViewDevice = nullptr;

    renderer::rhi::gl::unregisterTextureAndReset(m_sceneColorHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_pingColorHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_pongColorHandle);

    auto deleteTexture = [](GLuint& tex) {
        if (tex != 0) { glDeleteTextures(1, &tex); tex = 0; }
    };
    deleteTexture(m_sceneColorTex);
    deleteTexture(m_pingColorTex);
    deleteTexture(m_pongColorTex);
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
