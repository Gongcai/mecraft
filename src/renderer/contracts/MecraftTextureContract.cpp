#include "MecraftTextureContract.h"

#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"

#include <array>

#include <glad/glad.h>

namespace MecraftTextureContract {
namespace {

constexpr std::array<const char*, 6> kShadowSamplerNames = {
    "uCsmShadowMap",
    "uCsmShadowDepthRaw",
    "uCsmShadowDepthAll",
    "uCsmShadowDepthAllRaw",
    "uCsmShadowColor0",
    "uCsmShadowColor1",
};

uint32_t g_neutralDepthRaw = 0;
uint32_t g_neutralDepthCompare = 0;
uint32_t g_neutralColor0 = 0;
uint32_t g_neutralColor1 = 0;
RhiDevice* g_rhiDevice = nullptr;
RhiTextureHandle g_neutralDepthRawHandle;
RhiTextureViewHandle g_neutralDepthRawView;
bool g_neutralTexturesInitialized = false;

uint32_t createTexture2DArray() {
    GLuint texture = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &texture);
    return texture;
}

void destroyTexture(uint32_t& texture) {
    if (texture == 0) {
        return;
    }
    GLuint glTexture = texture;
    glDeleteTextures(1, &glTexture);
    texture = 0;
}

void destroyNeutralShadowTextureStorage() {
    if (g_rhiDevice != nullptr && g_neutralDepthRawView.isValid()) {
        g_rhiDevice->destroyTextureView(g_neutralDepthRawView);
        g_neutralDepthRawView = {};
    }
    renderer::rhi::gl::unregisterTextureAndReset(g_neutralDepthRawHandle);
    destroyTexture(g_neutralDepthRaw);
    destroyTexture(g_neutralDepthCompare);
    destroyTexture(g_neutralColor0);
    destroyTexture(g_neutralColor1);
    g_neutralTexturesInitialized = false;
}

bool clearNeutralDepthWithRhi() {
    g_neutralDepthRawHandle = renderer::rhi::gl::registerTexture({
        g_neutralDepthRaw,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Depth32Float,
        1u,
        1u,
        1u,
        1u,
        1u,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment),
        false
    });
    if (!g_neutralDepthRawHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc depthViewDesc;
    depthViewDesc.texture = g_neutralDepthRawHandle;
    depthViewDesc.viewType = RhiTextureViewType::Texture2DArray;
    depthViewDesc.format = RhiTextureFormat::Depth32Float;
    depthViewDesc.baseMip = 0;
    depthViewDesc.mipCount = 1;
    depthViewDesc.baseLayer = 0;
    depthViewDesc.layerCount = 1;
    g_neutralDepthRawView = g_rhiDevice->createTextureView(depthViewDesc);
    if (!g_neutralDepthRawView.isValid()) {
        renderer::rhi::gl::unregisterTextureAndReset(g_neutralDepthRawHandle);
        return false;
    }

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = g_neutralDepthRawView;
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "MecraftTextureContract.NeutralShadowDepthClear";
    renderingInfo.renderArea = {0, 0, 1u, 1u};
    renderingInfo.depthStencilAttachment = &depthAttachment;

    RhiCommandList& commandList = g_rhiDevice->beginFrame();
    commandList.beginRendering(renderingInfo);
    commandList.endRendering();
    g_rhiDevice->submitFrame(commandList);
    return true;
}

bool ensureNeutralShadowTextures() {
    if (g_neutralTexturesInitialized) {
        return true;
    }
    if (g_rhiDevice == nullptr) {
        return false;
    }

    g_neutralDepthRaw = createTexture2DArray();
    glTextureStorage3D(g_neutralDepthRaw, 1, GL_DEPTH_COMPONENT32F, 1, 1, 1);
    glTextureParameteri(g_neutralDepthRaw, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(g_neutralDepthRaw, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(g_neutralDepthRaw, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(g_neutralDepthRaw, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(g_neutralDepthRaw, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    if (!clearNeutralDepthWithRhi()) {
        destroyNeutralShadowTextureStorage();
        return false;
    }

    GLuint compareTexture = 0;
    glGenTextures(1, &compareTexture);
    g_neutralDepthCompare = compareTexture;
    glTextureView(g_neutralDepthCompare, GL_TEXTURE_2D_ARRAY,
                  g_neutralDepthRaw, GL_DEPTH_COMPONENT32F, 0, 1, 0, 1);
    glTextureParameteri(g_neutralDepthCompare, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(g_neutralDepthCompare, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(g_neutralDepthCompare, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(g_neutralDepthCompare, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(g_neutralDepthCompare, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(g_neutralDepthCompare, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    g_neutralColor0 = createTexture2DArray();
    glTextureStorage3D(g_neutralColor0, 1, GL_RGBA8, 1, 1, 1);
    glTextureParameteri(g_neutralColor0, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(g_neutralColor0, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(g_neutralColor0, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(g_neutralColor0, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const unsigned char black[] = {0, 0, 0, 255};
    glTextureSubImage3D(g_neutralColor0, 0, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, black);

    g_neutralColor1 = createTexture2DArray();
    glTextureStorage3D(g_neutralColor1, 1, GL_RGBA16F, 1, 1, 1);
    glTextureParameteri(g_neutralColor1, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(g_neutralColor1, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(g_neutralColor1, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(g_neutralColor1, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const float zeros[] = {0.0f, 0.0f, 0.0f, 0.0f};
    glTextureSubImage3D(g_neutralColor1, 0, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_FLOAT, zeros);

    g_neutralTexturesInitialized = true;
    return true;
}

} // namespace

bool initializeNeutralShadowTextures(RhiDevice& rhiDevice) {
    g_rhiDevice = &rhiDevice;
    return ensureNeutralShadowTextures();
}

void bindShadowSamplers(const uint32_t program,
                        const int baseUnit,
                        const ShadowTextureBundle& textures) {
    const std::array<uint32_t, 6> handles = {
        textures.csmDepthCompare,
        textures.csmDepthRaw,
        textures.csmDepthAllCompare,
        textures.csmDepthAllRaw,
        textures.csmColor0,
        textures.csmColor1,
    };

    for (int i = 0; i < 6; ++i) {
        const GLint loc = glGetUniformLocation(program, kShadowSamplerNames[static_cast<size_t>(i)]);
        if (loc >= 0) {
            glProgramUniform1i(program, loc, baseUnit + i);
        }
        if (handles[static_cast<size_t>(i)] != 0) {
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + baseUnit + i));
            glBindTexture(GL_TEXTURE_2D_ARRAY, handles[static_cast<size_t>(i)]);
        }
    }
}

uint32_t neutralDepthCompare() {
    return ensureNeutralShadowTextures() ? g_neutralDepthCompare : 0u;
}

uint32_t neutralDepthRaw() {
    return ensureNeutralShadowTextures() ? g_neutralDepthRaw : 0u;
}

uint32_t neutralColor0() {
    return ensureNeutralShadowTextures() ? g_neutralColor0 : 0u;
}

uint32_t neutralColor1() {
    return ensureNeutralShadowTextures() ? g_neutralColor1 : 0u;
}

ShadowTextureBundle neutralBundle() {
    if (!ensureNeutralShadowTextures()) {
        return {};
    }
    return {
        g_neutralDepthCompare,
        g_neutralDepthRaw,
        g_neutralDepthCompare,
        g_neutralDepthRaw,
        g_neutralColor0,
        g_neutralColor1,
    };
}

void bindNeutralShadowSamplers(const uint32_t program, const int baseUnit) {
    bindShadowSamplers(program, baseUnit, neutralBundle());
}

void destroyNeutralShadowTextures() {
    destroyNeutralShadowTextureStorage();
    g_rhiDevice = nullptr;
}

} // namespace MecraftTextureContract
