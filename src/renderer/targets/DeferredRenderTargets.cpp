#include "DeferredRenderTargets.h"
#include "../../Diagnostics.h"
#include "../debug/RenderDebugLabels.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"

#include <glad/glad.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <vector>

namespace {
constexpr GLenum kGAlbedoAttachment = GL_COLOR_ATTACHMENT0;
constexpr GLenum kGNormalAoAttachment = GL_COLOR_ATTACHMENT1;
constexpr GLenum kGVoxelLightAttachment = GL_COLOR_ATTACHMENT2;
constexpr GLenum kGMaterialAttachment = GL_COLOR_ATTACHMENT3;
constexpr GLenum kGMaterialAuxAttachment = GL_COLOR_ATTACHMENT4;
constexpr GLsizei kGBufferAttachmentCount = 5;

void blitFramebuffer(const GLuint readFramebuffer,
                     const GLuint drawFramebuffer,
                     const int srcWidth,
                     const int srcHeight,
                     const int dstWidth,
                     const int dstHeight,
                     const GLbitfield mask,
                     const GLenum filter) {
    glBlitNamedFramebuffer(readFramebuffer,
                           drawFramebuffer,
                           0,
                           0,
                           srcWidth,
                           srcHeight,
                           0,
                           0,
                           dstWidth,
                           dstHeight,
                           mask,
                           filter);
}
} // namespace

DeferredRenderTargets::~DeferredRenderTargets() {
    shutdown();
}

bool DeferredRenderTargets::init() {
    if (m_fullscreenVao == 0) {
        glGenVertexArrays(1, &m_fullscreenVao);
    }
    return m_fullscreenVao != 0;
}

void DeferredRenderTargets::shutdown() {
    destroyFramebuffers();
    destroyFullscreenTriangle();
    m_currentHistoryIndex = 0;
    m_ssaoHistoryIndex = 0;
    m_ssgiHistoryIndex = 0;
    m_width = 0;
    m_height = 0;
    m_shadowResolution = 0;
    m_ready = false;
}

bool DeferredRenderTargets::ensureSize(const int width, const int height, const int shadowResolution) {
    const int targetWidth = std::max(1, width);
    const int targetHeight = std::max(1, height);
    const int targetShadow = std::max(256, shadowResolution);
    if (m_ready && m_width == targetWidth && m_height == targetHeight && m_shadowResolution == targetShadow) {
        return true;
    }

    destroyFramebuffers();
    m_rebuiltSinceCheck = true;

    m_width = targetWidth;
    m_height = targetHeight;
    m_shadowResolution = targetShadow;

    glCreateFramebuffers(1, &m_gBufferFbo);

    m_gAlbedo = createTexture2D(GL_RGBA8, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    m_gNormalAo = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    m_gVoxelLight = createTexture2D(GL_RG8, m_width, m_height, GL_RG, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    m_gMaterial = createTexture2D(GL_RGBA8, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    m_gMaterialAux = createTexture2D(GL_RGBA8, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    m_gDepth = createTexture2D(GL_DEPTH_COMPONENT32F, m_width, m_height, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);

    glNamedFramebufferTexture(m_gBufferFbo, kGAlbedoAttachment, m_gAlbedo, 0);
    glNamedFramebufferTexture(m_gBufferFbo, kGNormalAoAttachment, m_gNormalAo, 0);
    glNamedFramebufferTexture(m_gBufferFbo, kGVoxelLightAttachment, m_gVoxelLight, 0);
    glNamedFramebufferTexture(m_gBufferFbo, kGMaterialAttachment, m_gMaterial, 0);
    glNamedFramebufferTexture(m_gBufferFbo, kGMaterialAuxAttachment, m_gMaterialAux, 0);
    glNamedFramebufferTexture(m_gBufferFbo, GL_DEPTH_ATTACHMENT, m_gDepth, 0);
    const GLenum gBufferDrawBuffers[] = {
        kGAlbedoAttachment,
        kGNormalAoAttachment,
        kGVoxelLightAttachment,
        kGMaterialAttachment,
        kGMaterialAuxAttachment
    };
    glNamedFramebufferDrawBuffers(m_gBufferFbo, kGBufferAttachmentCount, gBufferDrawBuffers);
    if (!checkFramebufferComplete(m_gBufferFbo, "GBuffer")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_shadowFbo);
    m_shadowDepth = createTexture2D(GL_DEPTH_COMPONENT32F, m_shadowResolution, m_shadowResolution,
                                   GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    constexpr float kBorderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTextureParameterfv(m_shadowDepth, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    // DerivativeMain shadowtex1 equivalent: zero-copy view of m_shadowDepth with
    // hardware depth comparison (sampler2DShadow). Used for PCF via texture() —
    // replaces manual compareShadowBilinear (~15 instructions/sample → 1 instruction).
    // BlockerSearch still uses the raw m_shadowDepth via texelFetch (uShadowMapRaw).
    glGenTextures(1, &m_shadowDepthComparison);
    glTextureView(m_shadowDepthComparison, GL_TEXTURE_2D,
                  m_shadowDepth, GL_DEPTH_COMPONENT32F, 0, 1, 0, 1);
    glTextureParameteri(m_shadowDepthComparison, GL_TEXTURE_COMPARE_MODE,  GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(m_shadowDepthComparison, GL_TEXTURE_COMPARE_FUNC,  GL_LEQUAL);
    glTextureParameteri(m_shadowDepthComparison, GL_TEXTURE_MIN_FILTER,    GL_LINEAR);
    glTextureParameteri(m_shadowDepthComparison, GL_TEXTURE_MAG_FILTER,    GL_LINEAR);
    glTextureParameteri(m_shadowDepthComparison, GL_TEXTURE_WRAP_S,        GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_shadowDepthComparison, GL_TEXTURE_WRAP_T,        GL_CLAMP_TO_BORDER);
    glTextureParameterfv(m_shadowDepthComparison, GL_TEXTURE_BORDER_COLOR, kBorderColor);
    // Shadow color: albedo for colored shadows / caustics (RGBA8)
    m_shadowColor = createTexture2D(GL_RGBA8, m_shadowResolution, m_shadowResolution,
                                    GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(m_shadowColor, GL_TEXTURE_BORDER_COLOR, kBorderColor);
    // Shadow normal: encoded normal + skylight/aux, matching DerivativeMain shadowcolor1.
    m_shadowNormal = createTexture2D(GL_RGBA16F, m_shadowResolution, m_shadowResolution,
                                     GL_RGBA, GL_FLOAT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(m_shadowNormal, GL_TEXTURE_BORDER_COLOR, kBorderColor);
    glNamedFramebufferTexture(m_shadowFbo, GL_DEPTH_ATTACHMENT, m_shadowDepth, 0);
    glNamedFramebufferTexture(m_shadowFbo, GL_COLOR_ATTACHMENT0, m_shadowColor, 0);
    glNamedFramebufferTexture(m_shadowFbo, GL_COLOR_ATTACHMENT1, m_shadowNormal, 0);
    const GLenum shadowDrawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glNamedFramebufferDrawBuffers(m_shadowFbo, 2, shadowDrawBuffers);
    if (!checkFramebufferComplete(m_shadowFbo, "ShadowMap")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_csmShadowFbo);
    m_csmShadowDepth = createTexture2DArray(GL_DEPTH_COMPONENT32F,
                                            m_shadowResolution,
                                            m_shadowResolution,
                                            kShadowCascadeCount,
                                            GL_NEAREST,
                                            GL_NEAREST,
                                            GL_CLAMP_TO_BORDER);
    glTextureParameterfv(m_csmShadowDepth, GL_TEXTURE_BORDER_COLOR, kBorderColor);
    glGenTextures(1, &m_csmShadowDepthComparison);
    glTextureView(m_csmShadowDepthComparison, GL_TEXTURE_2D_ARRAY,
                  m_csmShadowDepth, GL_DEPTH_COMPONENT32F,
                  0, 1, 0, kShadowCascadeCount);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureParameterfv(m_csmShadowDepthComparison, GL_TEXTURE_BORDER_COLOR, kBorderColor);
    glNamedFramebufferTextureLayer(m_csmShadowFbo, GL_DEPTH_ATTACHMENT, m_csmShadowDepth, 0, 0);
    glNamedFramebufferDrawBuffer(m_csmShadowFbo, GL_NONE);
    glNamedFramebufferReadBuffer(m_csmShadowFbo, GL_NONE);
    if (!checkFramebufferComplete(m_csmShadowFbo, "CsmShadowMap")) {
        shutdown();
        return false;
    }

    // CSM transparent shadow: depth-all + color for water/transparent occlusion
    // (DerivativeMain shadowtex0/shadowcolor0/shadowcolor1 equivalent)
    glCreateFramebuffers(1, &m_csmShadowTransparentFbo);
    m_csmShadowDepthAll = createTexture2DArray(GL_DEPTH_COMPONENT32F,
                                               m_shadowResolution, m_shadowResolution,
                                               kShadowCascadeCount,
                                               GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(m_csmShadowDepthAll, GL_TEXTURE_BORDER_COLOR, kBorderColor);
    glGenTextures(1, &m_csmShadowDepthAllComparison);
    glTextureView(m_csmShadowDepthAllComparison, GL_TEXTURE_2D_ARRAY,
                  m_csmShadowDepthAll, GL_DEPTH_COMPONENT32F,
                  0, 1, 0, kShadowCascadeCount);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureParameterfv(m_csmShadowDepthAllComparison, GL_TEXTURE_BORDER_COLOR, kBorderColor);
    m_csmShadowColor0 = createTexture2DArray(GL_RGBA8,
                                             m_shadowResolution, m_shadowResolution,
                                             kShadowCascadeCount,
                                             GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(m_csmShadowColor0, GL_TEXTURE_BORDER_COLOR, kBorderColor);
    m_csmShadowColor1 = createTexture2DArray(GL_RGBA16F,
                                             m_shadowResolution, m_shadowResolution,
                                             kShadowCascadeCount,
                                             GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(m_csmShadowColor1, GL_TEXTURE_BORDER_COLOR, kBorderColor);
    glNamedFramebufferTextureLayer(m_csmShadowTransparentFbo, GL_DEPTH_ATTACHMENT, m_csmShadowDepthAll, 0, 0);
    glNamedFramebufferTextureLayer(m_csmShadowTransparentFbo, GL_COLOR_ATTACHMENT0, m_csmShadowColor0, 0, 0);
    glNamedFramebufferTextureLayer(m_csmShadowTransparentFbo, GL_COLOR_ATTACHMENT1, m_csmShadowColor1, 0, 0);
    const GLenum transparentDrawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glNamedFramebufferDrawBuffers(m_csmShadowTransparentFbo, 2, transparentDrawBuffers);
    if (!checkFramebufferComplete(m_csmShadowTransparentFbo, "CsmShadowTransparent")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_ssaoFbo);
    m_ssaoTex = createTexture2D(GL_R8, m_width, m_height, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_ssaoFbo, GL_COLOR_ATTACHMENT0, m_ssaoTex, 0);
    const GLenum ssaoDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_ssaoFbo, 1, &ssaoDrawBuffer);
    if (!checkFramebufferComplete(m_ssaoFbo, "SSAO")) {
        shutdown();
        return false;
    }

    // SSAO filtered output (bilateral filter resolves into this)
    glCreateFramebuffers(1, &m_ssaoFilteredFbo);
    m_ssaoFilteredTex = createTexture2D(GL_R8, m_width, m_height, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_ssaoFilteredFbo, GL_COLOR_ATTACHMENT0, m_ssaoFilteredTex, 0);
    const GLenum ssaoFilteredDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_ssaoFilteredFbo, 1, &ssaoFilteredDrawBuffer);
    if (!checkFramebufferComplete(m_ssaoFilteredFbo, "SSAOFiltered")) {
        shutdown();
        return false;
    }

    // Half-res SSAO: raw and filtered at width/2 x height/2
    const int halfW = std::max(1, m_width / 2);
    const int halfH = std::max(1, m_height / 2);
    glCreateFramebuffers(1, &m_ssaoHalfResFbo);
    m_ssaoHalfResTex = createTexture2D(GL_R8, halfW, halfH, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_ssaoHalfResFbo, GL_COLOR_ATTACHMENT0, m_ssaoHalfResTex, 0);
    const GLenum ssaoHalfResDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_ssaoHalfResFbo, 1, &ssaoHalfResDrawBuffer);
    if (!checkFramebufferComplete(m_ssaoHalfResFbo, "SSAOHalfRes")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_ssaoHalfResFilteredFbo);
    m_ssaoHalfResFilteredTex = createTexture2D(GL_R8, halfW, halfH, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_ssaoHalfResFilteredFbo, GL_COLOR_ATTACHMENT0, m_ssaoHalfResFilteredTex, 0);
    const GLenum ssaoHalfResFilteredDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_ssaoHalfResFilteredFbo, 1, &ssaoHalfResFilteredDrawBuffer);
    if (!checkFramebufferComplete(m_ssaoHalfResFilteredFbo, "SSAOHalfResFiltered")) {
        shutdown();
        return false;
    }

    // SSAO temporal history ping-pong (R8, matches SSAO format)
    for (int i = 0; i < 2; ++i) {
        glCreateFramebuffers(1, &m_ssaoHistoryFbo[i]);
        m_ssaoHistoryTex[i] = createTexture2D(GL_R8, m_width, m_height, GL_RED, GL_UNSIGNED_BYTE,
                                              GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(m_ssaoHistoryFbo[i], GL_COLOR_ATTACHMENT0, m_ssaoHistoryTex[i], 0);
        const GLenum ssaoHistoryDrawBuffer = GL_COLOR_ATTACHMENT0;
        glNamedFramebufferDrawBuffers(m_ssaoHistoryFbo[i], 1, &ssaoHistoryDrawBuffer);
        if (!checkFramebufferComplete(m_ssaoHistoryFbo[i], "SSAOHistory")) {
            shutdown();
            return false;
        }
        // Clear to 1.0 (no occlusion) so first frame reads valid history
        const float clearWhite = 1.0f;
        glClearNamedFramebufferfv(m_ssaoHistoryFbo[i], GL_COLOR, 0, &clearWhite);
    }
    m_ssaoHistoryIndex = 0;

    // SSAO temporal resolve output (R8, same format as SSAO)
    glCreateFramebuffers(1, &m_ssaoTemporalFbo);
    m_ssaoTemporalTex = createTexture2D(GL_R8, m_width, m_height, GL_RED, GL_UNSIGNED_BYTE,
                                        GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_ssaoTemporalFbo, GL_COLOR_ATTACHMENT0, m_ssaoTemporalTex, 0);
    const GLenum ssaoTemporalDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_ssaoTemporalFbo, 1, &ssaoTemporalDrawBuffer);
    if (!checkFramebufferComplete(m_ssaoTemporalFbo, "SSAOTemporal")) {
        shutdown();
        return false;
    }
    // Clear temporal output to 1.0 (no occlusion)
    const float clearWhiteTemporal = 1.0f;
    glClearNamedFramebufferfv(m_ssaoTemporalFbo, GL_COLOR, 0, &clearWhiteTemporal);

    glCreateFramebuffers(1, &m_ssgiHalfResFbo);
    m_ssgiHalfResTex = createTexture2D(GL_RGBA16F, halfW, halfH, GL_RGBA, GL_FLOAT,
                                       GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_ssgiHalfResFbo, GL_COLOR_ATTACHMENT0, m_ssgiHalfResTex, 0);
    const GLenum ssgiHalfResDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_ssgiHalfResFbo, 1, &ssgiHalfResDrawBuffer);
    if (!checkFramebufferComplete(m_ssgiHalfResFbo, "SSGIHalfRes")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_ssgiFbo);
    m_ssgiTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_ssgiFbo, GL_COLOR_ATTACHMENT0, m_ssgiTex, 0);
    const GLenum ssgiDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_ssgiFbo, 1, &ssgiDrawBuffer);
    if (!checkFramebufferComplete(m_ssgiFbo, "SSGI")) {
        shutdown();
        return false;
    }

    for (int i = 0; i < 2; ++i) {
        glCreateFramebuffers(1, &m_ssgiDenoiseFbo[i]);
        m_ssgiDenoiseTex[i] = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                              GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(m_ssgiDenoiseFbo[i], GL_COLOR_ATTACHMENT0, m_ssgiDenoiseTex[i], 0);
        glNamedFramebufferDrawBuffers(m_ssgiDenoiseFbo[i], 1, &ssgiDrawBuffer);
        if (!checkFramebufferComplete(m_ssgiDenoiseFbo[i], "SSGIDenoise")) {
            shutdown();
            return false;
        }
        constexpr float clearBlack[] = {0.0f, 0.0f, 0.0f, 0.0f};
        glClearNamedFramebufferfv(m_ssgiDenoiseFbo[i], GL_COLOR, 0, clearBlack);
    }

    for (int i = 0; i < 2; ++i) {
        glCreateFramebuffers(1, &m_ssgiHistoryFbo[i]);
        m_ssgiHistoryTex[i] = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                              GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        m_ssgiMomentsHistoryTex[i] = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                                     GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(m_ssgiHistoryFbo[i], GL_COLOR_ATTACHMENT0, m_ssgiHistoryTex[i], 0);
        glNamedFramebufferTexture(m_ssgiHistoryFbo[i], GL_COLOR_ATTACHMENT1, m_ssgiMomentsHistoryTex[i], 0);
        const GLenum ssgiHistoryDrawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glNamedFramebufferDrawBuffers(m_ssgiHistoryFbo[i], 2, ssgiHistoryDrawBuffers);
        if (!checkFramebufferComplete(m_ssgiHistoryFbo[i], "SSGIHistory")) {
            shutdown();
            return false;
        }
        constexpr float clearBlack[] = {0.0f, 0.0f, 0.0f, 0.0f};
        glClearNamedFramebufferfv(m_ssgiHistoryFbo[i], GL_COLOR, 0, clearBlack);
        glClearNamedFramebufferfv(m_ssgiHistoryFbo[i], GL_COLOR, 1, clearBlack);
    }
    m_ssgiHistoryIndex = 0;

    glCreateFramebuffers(1, &m_ssgiTemporalFbo);
    m_ssgiTemporalTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                        GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    m_ssgiTemporalMomentsTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                               GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_ssgiTemporalFbo, GL_COLOR_ATTACHMENT0, m_ssgiTemporalTex, 0);
    glNamedFramebufferTexture(m_ssgiTemporalFbo, GL_COLOR_ATTACHMENT1, m_ssgiTemporalMomentsTex, 0);
    const GLenum ssgiTemporalDrawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glNamedFramebufferDrawBuffers(m_ssgiTemporalFbo, 2, ssgiTemporalDrawBuffers);
    if (!checkFramebufferComplete(m_ssgiTemporalFbo, "SSGITemporal")) {
        shutdown();
        return false;
    }
    constexpr float clearSsgiTemporal[] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearNamedFramebufferfv(m_ssgiTemporalFbo, GL_COLOR, 0, clearSsgiTemporal);
    glClearNamedFramebufferfv(m_ssgiTemporalFbo, GL_COLOR, 1, clearSsgiTemporal);

    glCreateFramebuffers(1, &m_sceneLightingFbo);
    m_sceneLightingTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_sceneLightingFbo, GL_COLOR_ATTACHMENT0, m_sceneLightingTex, 0);
    const GLenum sceneLightingDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_sceneLightingFbo, 1, &sceneLightingDrawBuffer);
    if (!checkFramebufferComplete(m_sceneLightingFbo, "SceneLighting")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_sceneCompositeFbo);
    m_sceneCompositeTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_sceneCompositeFbo, GL_COLOR_ATTACHMENT0, m_sceneCompositeTex, 0);
    const GLenum sceneCompositeDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_sceneCompositeFbo, 1, &sceneCompositeDrawBuffer);
    if (!checkFramebufferComplete(m_sceneCompositeFbo, "SceneComposite")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_sceneResolvedFbo);
    m_sceneResolvedTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_sceneResolvedFbo, GL_COLOR_ATTACHMENT0, m_sceneResolvedTex, 0);
    const GLenum sceneResolvedDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_sceneResolvedFbo, 1, &sceneResolvedDrawBuffer);
    if (!checkFramebufferComplete(m_sceneResolvedFbo, "SceneResolved")) {
        shutdown();
        return false;
    }

    // TemporalCurrent: TAA current-frame scratch buffer. Avoids reading
    // history[current] as TAA input (which conflicts with the "history
    // only written once per frame" invariant).
    glCreateFramebuffers(1, &m_temporalCurrentFbo);
    m_temporalCurrentTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_temporalCurrentFbo, GL_COLOR_ATTACHMENT0, m_temporalCurrentTex, 0);
    const GLenum temporalCurrentDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_temporalCurrentFbo, 1, &temporalCurrentDrawBuffer);
    if (!checkFramebufferComplete(m_temporalCurrentFbo, "TemporalCurrent")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_transparentCompositeFbo);
    m_transparentCompositeTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    // Keep transparent depth separate from the sampled G-buffer depth to avoid feedback while drawing water/transparent materials.
    m_transparentCompositeDepth = createTexture2D(GL_DEPTH_COMPONENT32F, m_width, m_height, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_transparentCompositeFbo, GL_COLOR_ATTACHMENT0, m_transparentCompositeTex, 0);
    glNamedFramebufferTexture(m_transparentCompositeFbo, GL_DEPTH_ATTACHMENT, m_transparentCompositeDepth, 0);
    const GLenum transparentCompositeDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_transparentCompositeFbo, 1, &transparentCompositeDrawBuffer);
    if (!checkFramebufferComplete(m_transparentCompositeFbo, "TransparentComposite")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_halfResFbo);
    const int halfWidth = std::max(1, m_width / 2);
    const int halfHeight = std::max(1, m_height / 2);
    m_halfResTex = createTexture2D(GL_RGBA16F, halfWidth, halfHeight, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_halfResFbo, GL_COLOR_ATTACHMENT0, m_halfResTex, 0);
    const GLenum halfResDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_halfResFbo, 1, &halfResDrawBuffer);
    if (!checkFramebufferComplete(m_halfResFbo, "HalfRes")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_reflectionFbo);
    m_reflectionTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_reflectionFbo, GL_COLOR_ATTACHMENT0, m_reflectionTex, 0);
    const GLenum reflectionDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_reflectionFbo, 1, &reflectionDrawBuffer);
    if (!checkFramebufferComplete(m_reflectionFbo, "Reflection")) {
        shutdown();
        return false;
    }

    // Reflection temporal scratch: holds a copy of the filtered reflection so
    // the temporal pass can read it while writing the blended result to m_reflectionFbo.
    glCreateFramebuffers(1, &m_reflectionTemporalScratchFbo);
    m_reflectionTemporalScratchTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_reflectionTemporalScratchFbo, GL_COLOR_ATTACHMENT0, m_reflectionTemporalScratchTex, 0);
    const GLenum reflectionTemporalScratchDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_reflectionTemporalScratchFbo, 1, &reflectionTemporalScratchDrawBuffer);
    if (!checkFramebufferComplete(m_reflectionTemporalScratchFbo, "ReflectionTemporalScratch")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_cloudFbo);
    m_cloudTex = createTexture2D(GL_RGBA16F, halfWidth, halfHeight, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_cloudFbo, GL_COLOR_ATTACHMENT0, m_cloudTex, 0);
    const GLenum cloudDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_cloudFbo, 1, &cloudDrawBuffer);
    if (!checkFramebufferComplete(m_cloudFbo, "Cloud")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_skyCaptureFbo);
    m_skyCaptureTex = createTexture2D(GL_RGBA16F, kSkyCaptureWidth, kSkyCaptureHeight, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_skyCaptureFbo, GL_COLOR_ATTACHMENT0, m_skyCaptureTex, 0);
    const GLenum skyCaptureDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_skyCaptureFbo, 1, &skyCaptureDrawBuffer);
    if (!checkFramebufferComplete(m_skyCaptureFbo, "SkyCapture")) {
        shutdown();
        return false;
    }

    // History scene FBO ping-pong (RGBA16F color + depth)
    for (int i = 0; i < 2; ++i) {
        glCreateFramebuffers(1, &m_historySceneFbo[i]);
        m_historySceneTex[i] = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                               GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        m_historyDepthTex[i] = createTexture2D(GL_DEPTH_COMPONENT32F, m_width, m_height, GL_DEPTH_COMPONENT, GL_FLOAT,
                                               GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(m_historySceneFbo[i], GL_COLOR_ATTACHMENT0, m_historySceneTex[i], 0);
        glNamedFramebufferTexture(m_historySceneFbo[i], GL_DEPTH_ATTACHMENT, m_historyDepthTex[i], 0);
        const GLenum historyDrawBuffer = GL_COLOR_ATTACHMENT0;
        glNamedFramebufferDrawBuffers(m_historySceneFbo[i], 1, &historyDrawBuffer);
        if (!checkFramebufferComplete(m_historySceneFbo[i], "HistoryScene")) {
            shutdown();
            return false;
        }
        constexpr float clearHistoryDepth = 1.0f;
        glClearNamedFramebufferfv(m_historySceneFbo[i], GL_DEPTH, 0, &clearHistoryDepth);

        glCreateFramebuffers(1, &m_historyReflectionFbo[i]);
        m_historyReflectionTex[i] = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                                    GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(m_historyReflectionFbo[i], GL_COLOR_ATTACHMENT0, m_historyReflectionTex[i], 0);
        glNamedFramebufferDrawBuffers(m_historyReflectionFbo[i], 1, &historyDrawBuffer);
        if (!checkFramebufferComplete(m_historyReflectionFbo[i], "HistoryReflection")) {
            shutdown();
            return false;
        }

        glCreateFramebuffers(1, &m_historyCloudFbo[i]);
        m_historyCloudTex[i] = createTexture2D(GL_RGBA16F, halfWidth, halfHeight, GL_RGBA, GL_FLOAT,
                                               GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(m_historyCloudFbo[i], GL_COLOR_ATTACHMENT0, m_historyCloudTex[i], 0);
        glNamedFramebufferDrawBuffers(m_historyCloudFbo[i], 1, &historyDrawBuffer);
        if (!checkFramebufferComplete(m_historyCloudFbo[i], "HistoryCloud")) {
            shutdown();
            return false;
        }

        glCreateFramebuffers(1, &m_historyVolumetricFbo[i]);
        m_historyVolumetricTex[i] = createTexture2D(GL_RGBA16F, halfWidth, halfHeight, GL_RGBA, GL_FLOAT,
                                                    GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(m_historyVolumetricFbo[i], GL_COLOR_ATTACHMENT0, m_historyVolumetricTex[i], 0);
        glNamedFramebufferDrawBuffers(m_historyVolumetricFbo[i], 1, &historyDrawBuffer);
        if (!checkFramebufferComplete(m_historyVolumetricFbo[i], "HistoryVolumetric")) {
            shutdown();
            return false;
        }
    }
    m_currentHistoryIndex = 0;

    // Velocity buffer (RG16F)
    glCreateFramebuffers(1, &m_velocityFbo);
    m_velocityTex = createTexture2D(GL_RG16F, m_width, m_height, GL_RG, GL_FLOAT,
                                    GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_velocityFbo, GL_COLOR_ATTACHMENT0, m_velocityTex, 0);
    const GLenum velocityDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_velocityFbo, 1, &velocityDrawBuffer);
    if (!checkFramebufferComplete(m_velocityFbo, "Velocity")) {
        shutdown();
        return false;
    }

    // Per-object velocity (RG16F) — screen-space velocity written by entity/drop
    // shaders during GBuffer fill. Temporarily attached to GBuffer FBO as
    // GL_COLOR_ATTACHMENT5 during entity/drop rendering, detached afterward.
    m_perObjectVelocityTex = createTexture2D(GL_RG16F, m_width, m_height, GL_RG, GL_FLOAT,
                                             GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);

    // Weather mask (R8) — additive-blended weather particle alpha.
    // Equivalent to DerivativeMain colortex0.b from gbuffers_weather.
    glCreateFramebuffers(1, &m_weatherMaskFbo);
    m_weatherMaskTex = createTexture2D(GL_R8, m_width, m_height, GL_RED, GL_UNSIGNED_BYTE,
                                       GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_weatherMaskFbo, GL_COLOR_ATTACHMENT0, m_weatherMaskTex, 0);
    const GLenum weatherMaskDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_weatherMaskFbo, 1, &weatherMaskDrawBuffer);
    if (!checkFramebufferComplete(m_weatherMaskFbo, "WeatherMask")) {
        shutdown();
        return false;
    }

    if (!registerRhiTextures()) {
        shutdown();
        return false;
    }

    // Label GL objects for RenderDoc / KHR_debug inspection
    renderer::debug::labelFramebuffer(m_gBufferFbo, "DeferredTargets.GBuffer");
    renderer::debug::labelTexture(m_gAlbedo, "DeferredTargets.GBufferAlbedo");
    renderer::debug::labelTexture(m_gNormalAo, "DeferredTargets.GBufferNormalAo");
    renderer::debug::labelTexture(m_gVoxelLight, "DeferredTargets.GBufferVoxelLight");
    renderer::debug::labelTexture(m_gMaterial, "DeferredTargets.GBufferMaterial");
    renderer::debug::labelTexture(m_gMaterialAux, "DeferredTargets.GBufferMaterialAux");
    renderer::debug::labelTexture(m_gDepth, "DeferredTargets.GBufferDepth");
    renderer::debug::labelFramebuffer(m_shadowFbo, "DeferredTargets.ShadowMap");
    renderer::debug::labelTexture(m_shadowDepth, "DeferredTargets.ShadowDepth");
    renderer::debug::labelTexture(m_shadowDepthComparison, "DeferredTargets.ShadowDepthComparison");
    renderer::debug::labelTexture(m_shadowColor, "DeferredTargets.ShadowColor");
    renderer::debug::labelTexture(m_shadowNormal, "DeferredTargets.ShadowNormal");
    renderer::debug::labelFramebuffer(m_csmShadowFbo, "DeferredTargets.CSMDepth");
    renderer::debug::labelTexture(m_csmShadowDepth, "DeferredTargets.CSMDepthArray");
    renderer::debug::labelTexture(m_csmShadowDepthComparison, "DeferredTargets.CSMDepthComparison");
    renderer::debug::labelFramebuffer(m_csmShadowTransparentFbo, "DeferredTargets.CSMTransparent");
    renderer::debug::labelTexture(m_csmShadowDepthAll, "DeferredTargets.CSMDepthAll");
    renderer::debug::labelTexture(m_csmShadowDepthAllComparison, "DeferredTargets.CSMDepthAllComparison");
    renderer::debug::labelTexture(m_csmShadowColor0, "DeferredTargets.CSMColor0");
    renderer::debug::labelTexture(m_csmShadowColor1, "DeferredTargets.CSMColor1");
    renderer::debug::labelFramebuffer(m_ssaoFbo, "DeferredTargets.SSAO");
    renderer::debug::labelTexture(m_ssaoTex, "DeferredTargets.SSAOTex");
    renderer::debug::labelFramebuffer(m_ssaoFilteredFbo, "DeferredTargets.SSAOFiltered");
    renderer::debug::labelTexture(m_ssaoFilteredTex, "DeferredTargets.SSAOFilteredTex");
    renderer::debug::labelFramebuffer(m_ssaoHalfResFbo, "DeferredTargets.SSAOHalfRes");
    renderer::debug::labelTexture(m_ssaoHalfResTex, "DeferredTargets.SSAOHalfResTex");
    renderer::debug::labelFramebuffer(m_ssaoHalfResFilteredFbo, "DeferredTargets.SSAOHalfResFiltered");
    renderer::debug::labelTexture(m_ssaoHalfResFilteredTex, "DeferredTargets.SSAOHalfResFilteredTex");
    renderer::debug::labelFramebuffer(m_ssaoTemporalFbo, "DeferredTargets.SSAOTemporal");
    renderer::debug::labelTexture(m_ssaoTemporalTex, "DeferredTargets.SSAOTemporalTex");
    for (int i = 0; i < 2; ++i) {
        char fboName[48], texName[48];
        std::snprintf(fboName, sizeof(fboName), "DeferredTargets.SSAOHistory[%d]", i);
        std::snprintf(texName, sizeof(texName), "DeferredTargets.SSAOHistoryTex[%d]", i);
        renderer::debug::labelFramebuffer(m_ssaoHistoryFbo[i], fboName);
        renderer::debug::labelTexture(m_ssaoHistoryTex[i], texName);
    }
    renderer::debug::labelFramebuffer(m_ssgiFbo, "DeferredTargets.SSGI");
    renderer::debug::labelTexture(m_ssgiTex, "DeferredTargets.SSGITex");
    renderer::debug::labelFramebuffer(m_ssgiHalfResFbo, "DeferredTargets.SSGIHalfRes");
    renderer::debug::labelTexture(m_ssgiHalfResTex, "DeferredTargets.SSGIHalfResTex");
    renderer::debug::labelFramebuffer(m_ssgiTemporalFbo, "DeferredTargets.SSGITemporal");
    renderer::debug::labelTexture(m_ssgiTemporalTex, "DeferredTargets.SSGITemporalTex");
    renderer::debug::labelTexture(m_ssgiTemporalMomentsTex, "DeferredTargets.SSGITemporalMomentsTex");
    for (int i = 0; i < 2; ++i) {
        char fboName[48], texName[48];
        std::snprintf(fboName, sizeof(fboName), "DeferredTargets.SSGIDenoise[%d]", i);
        std::snprintf(texName, sizeof(texName), "DeferredTargets.SSGIDenoiseTex[%d]", i);
        renderer::debug::labelFramebuffer(m_ssgiDenoiseFbo[i], fboName);
        renderer::debug::labelTexture(m_ssgiDenoiseTex[i], texName);
    }
    for (int i = 0; i < 2; ++i) {
        char fboName[48], texName[48];
        std::snprintf(fboName, sizeof(fboName), "DeferredTargets.SSGIHistory[%d]", i);
        std::snprintf(texName, sizeof(texName), "DeferredTargets.SSGIHistoryTex[%d]", i);
        renderer::debug::labelFramebuffer(m_ssgiHistoryFbo[i], fboName);
        renderer::debug::labelTexture(m_ssgiHistoryTex[i], texName);
        std::snprintf(texName, sizeof(texName), "DeferredTargets.SSGIMomentsHistoryTex[%d]", i);
        renderer::debug::labelTexture(m_ssgiMomentsHistoryTex[i], texName);
    }
    renderer::debug::labelFramebuffer(m_sceneLightingFbo, "DeferredTargets.SceneLighting");
    renderer::debug::labelTexture(m_sceneLightingTex, "DeferredTargets.SceneLightingTex");
    renderer::debug::labelFramebuffer(m_sceneCompositeFbo, "DeferredTargets.SceneComposite");
    renderer::debug::labelTexture(m_sceneCompositeTex, "DeferredTargets.SceneCompositeTex");
    renderer::debug::labelFramebuffer(m_sceneResolvedFbo, "DeferredTargets.SceneResolved");
    renderer::debug::labelTexture(m_sceneResolvedTex, "DeferredTargets.SceneResolvedTex");
    renderer::debug::labelFramebuffer(m_transparentCompositeFbo, "DeferredTargets.TransparentComposite");
    renderer::debug::labelTexture(m_transparentCompositeTex, "DeferredTargets.TransparentCompositeTex");
    renderer::debug::labelTexture(m_transparentCompositeDepth, "DeferredTargets.TransparentCompositeDepth");
    renderer::debug::labelFramebuffer(m_halfResFbo, "DeferredTargets.HalfRes");
    renderer::debug::labelTexture(m_halfResTex, "DeferredTargets.HalfResTex");
    renderer::debug::labelFramebuffer(m_reflectionFbo, "DeferredTargets.Reflection");
    renderer::debug::labelTexture(m_reflectionTex, "DeferredTargets.ReflectionTex");
    renderer::debug::labelFramebuffer(m_reflectionTemporalScratchFbo, "DeferredTargets.ReflectionTemporalScratch");
    renderer::debug::labelTexture(m_reflectionTemporalScratchTex, "DeferredTargets.ReflectionTemporalScratchTex");
    renderer::debug::labelFramebuffer(m_cloudFbo, "DeferredTargets.Cloud");
    renderer::debug::labelTexture(m_cloudTex, "DeferredTargets.CloudTex");
    renderer::debug::labelFramebuffer(m_skyCaptureFbo, "DeferredTargets.SkyCapture");
    renderer::debug::labelTexture(m_skyCaptureTex, "DeferredTargets.SkyCaptureTex");
    for (int i = 0; i < 2; ++i) {
        char fboName[48], texName[48], depthName[48];
        std::snprintf(fboName, sizeof(fboName), "DeferredTargets.HistoryScene[%d]", i);
        std::snprintf(texName, sizeof(texName), "DeferredTargets.HistorySceneTex[%d]", i);
        std::snprintf(depthName, sizeof(depthName), "DeferredTargets.HistoryDepthTex[%d]", i);
        renderer::debug::labelFramebuffer(m_historySceneFbo[i], fboName);
        renderer::debug::labelTexture(m_historySceneTex[i], texName);
        renderer::debug::labelTexture(m_historyDepthTex[i], depthName);
    }
    for (int i = 0; i < 2; ++i) {
        char fboName[48], texName[48];
        std::snprintf(fboName, sizeof(fboName), "DeferredTargets.HistoryReflection[%d]", i);
        std::snprintf(texName, sizeof(texName), "DeferredTargets.HistoryReflectionTex[%d]", i);
        renderer::debug::labelFramebuffer(m_historyReflectionFbo[i], fboName);
        renderer::debug::labelTexture(m_historyReflectionTex[i], texName);
    }
    for (int i = 0; i < 2; ++i) {
        char fboName[48], texName[48];
        std::snprintf(fboName, sizeof(fboName), "DeferredTargets.HistoryCloud[%d]", i);
        std::snprintf(texName, sizeof(texName), "DeferredTargets.HistoryCloudTex[%d]", i);
        renderer::debug::labelFramebuffer(m_historyCloudFbo[i], fboName);
        renderer::debug::labelTexture(m_historyCloudTex[i], texName);
    }
    for (int i = 0; i < 2; ++i) {
        char fboName[48], texName[48];
        std::snprintf(fboName, sizeof(fboName), "DeferredTargets.HistoryVolumetric[%d]", i);
        std::snprintf(texName, sizeof(texName), "DeferredTargets.HistoryVolumetricTex[%d]", i);
        renderer::debug::labelFramebuffer(m_historyVolumetricFbo[i], fboName);
        renderer::debug::labelTexture(m_historyVolumetricTex[i], texName);
    }
    renderer::debug::labelFramebuffer(m_temporalCurrentFbo, "DeferredTargets.TemporalCurrent");
    renderer::debug::labelTexture(m_temporalCurrentTex, "DeferredTargets.TemporalCurrentTex");
    renderer::debug::labelFramebuffer(m_velocityFbo, "DeferredTargets.Velocity");
    renderer::debug::labelTexture(m_velocityTex, "DeferredTargets.VelocityTex");
    renderer::debug::labelTexture(m_perObjectVelocityTex, "DeferredTargets.PerObjectVelocity");
    renderer::debug::labelFramebuffer(m_weatherMaskFbo, "DeferredTargets.WeatherMask");
    renderer::debug::labelTexture(m_weatherMaskTex, "DeferredTargets.WeatherMaskTex");
    renderer::debug::labelTexture(m_atmosphereLut3d, "DeferredTargets.AtmosphereLUT");
    renderer::debug::labelVertexArray(m_fullscreenVao, "DeferredTargets.FullscreenVAO");

    m_ready = true;
    return true;
}

void DeferredRenderTargets::bindDefaultLike(const int32_t framebuffer, const int width, const int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glViewport(0, 0, std::max(1, width), std::max(1, height));
}

void DeferredRenderTargets::copyFramebufferColorToSceneLighting(const int32_t framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(static_cast<GLuint>(framebuffer), m_sceneLightingFbo,
                    std::max(1, width), std::max(1, height), m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneLightingFbo);
}

void DeferredRenderTargets::copyFramebufferColorToSceneResolved(const int32_t framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(static_cast<GLuint>(framebuffer), m_sceneResolvedFbo,
                    std::max(1, width), std::max(1, height), m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneResolvedFbo);
}

void DeferredRenderTargets::copyFramebufferColorToTransparentComposite(const int32_t framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(static_cast<GLuint>(framebuffer), m_transparentCompositeFbo,
                    std::max(1, width), std::max(1, height), m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copySceneLightingToTransparentComposite() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_sceneLightingFbo, m_transparentCompositeFbo,
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copySceneLightingToSceneComposite() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_sceneLightingFbo, m_sceneCompositeFbo,
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneCompositeFbo);
}

void DeferredRenderTargets::copySceneCompositeToSceneResolved() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_sceneCompositeFbo, m_sceneResolvedFbo,
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneResolvedFbo);
}

void DeferredRenderTargets::copySceneCompositeToTransparentComposite() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_sceneCompositeFbo, m_transparentCompositeFbo,
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copySceneResolvedToTransparentComposite() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_sceneResolvedFbo, m_transparentCompositeFbo,
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copyTransparentCompositeToSceneComposite() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_transparentCompositeFbo, m_sceneCompositeFbo,
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneCompositeFbo);
}

void DeferredRenderTargets::copyTransparentCompositeToSceneResolved() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_transparentCompositeFbo, m_sceneResolvedFbo,
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneResolvedFbo);
}

void DeferredRenderTargets::copyDepthToTransparentComposite() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_gBufferFbo, m_transparentCompositeFbo,
                    m_width, m_height, m_width, m_height,
                    GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copySceneResolvedToHistory() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_sceneResolvedFbo, m_historySceneFbo[m_currentHistoryIndex],
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_historySceneFbo[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copySceneResolvedToTemporalCurrent() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_sceneResolvedFbo, m_temporalCurrentFbo,
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_temporalCurrentFbo);
}

void DeferredRenderTargets::copyDepthToHistory() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_gBufferFbo, m_historySceneFbo[m_currentHistoryIndex],
                    m_width, m_height, m_width, m_height,
                    GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_historySceneFbo[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyReflectionToHistory() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_reflectionFbo, m_historyReflectionFbo[m_currentHistoryIndex],
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_historyReflectionFbo[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyReflectionToTemporalScratch() const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_reflectionFbo, m_reflectionTemporalScratchFbo,
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_reflectionTemporalScratchFbo);
}

void DeferredRenderTargets::copyCloudToHistory() const {
    if (!m_ready) {
        return;
    }
    const int halfWidth = std::max(1, m_width / 2);
    const int halfHeight = std::max(1, m_height / 2);
    blitFramebuffer(m_cloudFbo, m_historyCloudFbo[m_currentHistoryIndex],
                    halfWidth, halfHeight, halfWidth, halfHeight,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_historyCloudFbo[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyHistoryCloudToCloud() const {
    if (!m_ready) {
        return;
    }
    const int halfWidth = std::max(1, m_width / 2);
    const int halfHeight = std::max(1, m_height / 2);
    blitFramebuffer(m_historyCloudFbo[1 - m_currentHistoryIndex], m_cloudFbo,
                    halfWidth, halfHeight, halfWidth, halfHeight,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_cloudFbo);
}

void DeferredRenderTargets::copyVolumetricToHistory() const {
    if (!m_ready) {
        return;
    }
    const int halfWidth = std::max(1, m_width / 2);
    const int halfHeight = std::max(1, m_height / 2);
    blitFramebuffer(m_halfResFbo, m_historyVolumetricFbo[m_currentHistoryIndex],
                    halfWidth, halfHeight, halfWidth, halfHeight,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_historyVolumetricFbo[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyHistoryVolumetricToHalfRes() const {
    if (!m_ready) {
        return;
    }
    const int halfWidth = std::max(1, m_width / 2);
    const int halfHeight = std::max(1, m_height / 2);
    blitFramebuffer(m_historyVolumetricFbo[1 - m_currentHistoryIndex], m_halfResFbo,
                    halfWidth, halfHeight, halfWidth, halfHeight,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
    blitFramebuffer(m_historyVolumetricFbo[1 - m_currentHistoryIndex], m_historyVolumetricFbo[m_currentHistoryIndex],
                    halfWidth, halfHeight, halfWidth, halfHeight,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_halfResFbo);
}

void DeferredRenderTargets::copySsaoTemporalToHistory() {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_ssaoTemporalFbo, m_ssaoHistoryFbo[m_ssaoHistoryIndex],
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoHistoryFbo[m_ssaoHistoryIndex]);
}

void DeferredRenderTargets::copySsgiTemporalToHistory() {
    if (!m_ready) {
        return;
    }
    glNamedFramebufferReadBuffer(m_ssgiTemporalFbo, GL_COLOR_ATTACHMENT0);
    glNamedFramebufferDrawBuffer(m_ssgiHistoryFbo[m_ssgiHistoryIndex], GL_COLOR_ATTACHMENT0);
    blitFramebuffer(m_ssgiTemporalFbo, m_ssgiHistoryFbo[m_ssgiHistoryIndex],
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glNamedFramebufferReadBuffer(m_ssgiTemporalFbo, GL_COLOR_ATTACHMENT1);
    glNamedFramebufferDrawBuffer(m_ssgiHistoryFbo[m_ssgiHistoryIndex], GL_COLOR_ATTACHMENT1);
    blitFramebuffer(m_ssgiTemporalFbo, m_ssgiHistoryFbo[m_ssgiHistoryIndex],
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glNamedFramebufferReadBuffer(m_ssgiTemporalFbo, GL_COLOR_ATTACHMENT0);
    glNamedFramebufferDrawBuffer(m_ssgiHistoryFbo[m_ssgiHistoryIndex], GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssgiHistoryFbo[m_ssgiHistoryIndex]);
}

RhiTextureHandle DeferredRenderTargets::ssgiDenoiseTextureHandle(const int slot) const {
    assert(slot >= 0 && slot < 2);
    return m_ssgiDenoiseHandle[slot];
}

RhiTextureViewHandle DeferredRenderTargets::ssgiDenoiseTextureViewHandle(const int slot) const {
    assert(slot >= 0 && slot < 2);
    return m_ssgiDenoiseView[slot];
}

void DeferredRenderTargets::copySsgiDenoiseToSsgi(const int slot) {
    assert(slot >= 0 && slot < 2);
    if (!m_ready) {
        return;
    }
    glNamedFramebufferReadBuffer(m_ssgiDenoiseFbo[slot], GL_COLOR_ATTACHMENT0);
    glNamedFramebufferDrawBuffer(m_ssgiFbo, GL_COLOR_ATTACHMENT0);
    blitFramebuffer(m_ssgiDenoiseFbo[slot], m_ssgiFbo,
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssgiFbo);
}

void DeferredRenderTargets::copySsgiTemporalToSsgi() {
    if (!m_ready) {
        return;
    }
    glNamedFramebufferReadBuffer(m_ssgiTemporalFbo, GL_COLOR_ATTACHMENT0);
    glNamedFramebufferDrawBuffer(m_ssgiFbo, GL_COLOR_ATTACHMENT0);
    blitFramebuffer(m_ssgiTemporalFbo, m_ssgiFbo,
                    m_width, m_height, m_width, m_height,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssgiFbo);
}

void DeferredRenderTargets::blitSceneLightingTo(const int32_t framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_sceneLightingFbo, static_cast<GLuint>(framebuffer),
                    m_width, m_height, std::max(1, width), std::max(1, height),
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

void DeferredRenderTargets::blitSceneCompositeTo(const int32_t framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_sceneCompositeFbo, static_cast<GLuint>(framebuffer),
                    m_width, m_height, std::max(1, width), std::max(1, height),
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

void DeferredRenderTargets::blitSceneResolvedTo(const int32_t framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_sceneResolvedFbo, static_cast<GLuint>(framebuffer),
                    m_width, m_height, std::max(1, width), std::max(1, height),
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

void DeferredRenderTargets::blitTransparentCompositeTo(const int32_t framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_transparentCompositeFbo, static_cast<GLuint>(framebuffer),
                    m_width, m_height, std::max(1, width), std::max(1, height),
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

void DeferredRenderTargets::blitDepthTo(const int32_t framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    blitFramebuffer(m_gBufferFbo, static_cast<GLuint>(framebuffer),
                    m_width, m_height, std::max(1, width), std::max(1, height),
                    GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

uint32_t DeferredRenderTargets::createTexture2D(const uint32_t internalFormat,
                                                const int width,
                                                const int height,
                                                const uint32_t format,
                                                const uint32_t type,
                                                const uint32_t minFilter,
                                                const uint32_t magFilter,
                                                const uint32_t wrap,
                                                const int levels) {
    uint32_t texture = 0;
    (void)format;
    (void)type;
    glCreateTextures(GL_TEXTURE_2D, 1, &texture);
    const GLsizei mipLevels = std::max(1, levels);
    glTextureStorage2D(texture, mipLevels, internalFormat, width, height);
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minFilter));
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(magFilter));
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrap));
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrap));
    glTextureParameteri(texture, GL_TEXTURE_BASE_LEVEL, 0);
    glTextureParameteri(texture, GL_TEXTURE_MAX_LEVEL, mipLevels - 1);
    return texture;
}

uint32_t DeferredRenderTargets::createTexture2DArray(const uint32_t internalFormat,
                                                     const int width,
                                                     const int height,
                                                     const int layers,
                                                     const uint32_t minFilter,
                                                     const uint32_t magFilter,
                                                     const uint32_t wrap) {
    uint32_t texture = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &texture);
    glTextureStorage3D(texture, 1, internalFormat, width, height, layers);
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minFilter));
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(magFilter));
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrap));
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrap));
    glTextureParameteri(texture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texture, GL_TEXTURE_BASE_LEVEL, 0);
    glTextureParameteri(texture, GL_TEXTURE_MAX_LEVEL, 0);
    return texture;
}

void DeferredRenderTargets::generateMipmaps(const uint32_t texture) {
    if (texture != 0) {
        glGenerateTextureMipmap(texture);
    }
}

bool DeferredRenderTargets::checkFramebufferComplete(const uint32_t framebuffer, const char* label) {
    const GLenum status = glCheckNamedFramebufferStatus(framebuffer, GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
        return true;
    }
    MECRAFT_LOG_STREAM(std::cerr << "DeferredRenderTargets: incomplete " << label << " framebuffer, status=0x"
                                 << std::hex << status << std::dec << "\n");
    return false;
}

bool DeferredRenderTargets::registerRhiTextures() {
    m_gAlbedoHandle = renderer::rhi::gl::registerTexture({
        m_gAlbedo,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba8Unorm,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_gNormalAoHandle = renderer::rhi::gl::registerTexture({
        m_gNormalAo,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_gVoxelLightHandle = renderer::rhi::gl::registerTexture({
        m_gVoxelLight,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rg8Unorm,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_gMaterialHandle = renderer::rhi::gl::registerTexture({
        m_gMaterial,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba8Unorm,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_gMaterialAuxHandle = renderer::rhi::gl::registerTexture({
        m_gMaterialAux,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba8Unorm,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_gDepthHandle = renderer::rhi::gl::registerTexture({
        m_gDepth,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Depth32Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment),
        false
    });
    const uint32_t halfWidth = static_cast<uint32_t>(std::max(1, m_width / 2));
    const uint32_t halfHeight = static_cast<uint32_t>(std::max(1, m_height / 2));
    m_sceneLightingHandle = renderer::rhi::gl::registerTexture({
        m_sceneLightingTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_sceneCompositeHandle = renderer::rhi::gl::registerTexture({
        m_sceneCompositeTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_sceneResolvedHandle = renderer::rhi::gl::registerTexture({
        m_sceneResolvedTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_transparentCompositeHandle = renderer::rhi::gl::registerTexture({
        m_transparentCompositeTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_transparentCompositeDepthHandle = renderer::rhi::gl::registerTexture({
        m_transparentCompositeDepth,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Depth32Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment),
        false
    });
    m_halfResHandle = renderer::rhi::gl::registerTexture({
        m_halfResTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        halfWidth,
        halfHeight,
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_reflectionHandle = renderer::rhi::gl::registerTexture({
        m_reflectionTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_reflectionTemporalScratchHandle = renderer::rhi::gl::registerTexture({
        m_reflectionTemporalScratchTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_cloudHandle = renderer::rhi::gl::registerTexture({
        m_cloudTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        halfWidth,
        halfHeight,
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    for (int i = 0; i < 2; ++i) {
        m_historySceneHandle[i] = renderer::rhi::gl::registerTexture({
            m_historySceneTex[i],
            RhiTextureDimension::Texture2D,
            RhiTextureFormat::Rgba16Float,
            static_cast<uint32_t>(m_width),
            static_cast<uint32_t>(m_height),
            1,
            1,
            1,
            rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
            false
        });
        m_historyDepthHandle[i] = renderer::rhi::gl::registerTexture({
            m_historyDepthTex[i],
            RhiTextureDimension::Texture2D,
            RhiTextureFormat::Depth32Float,
            static_cast<uint32_t>(m_width),
            static_cast<uint32_t>(m_height),
            1,
            1,
            1,
            rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment),
            false
        });
        m_historyReflectionHandle[i] = renderer::rhi::gl::registerTexture({
            m_historyReflectionTex[i],
            RhiTextureDimension::Texture2D,
            RhiTextureFormat::Rgba16Float,
            static_cast<uint32_t>(m_width),
            static_cast<uint32_t>(m_height),
            1,
            1,
            1,
            rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
            false
        });
        m_historyCloudHandle[i] = renderer::rhi::gl::registerTexture({
            m_historyCloudTex[i],
            RhiTextureDimension::Texture2D,
            RhiTextureFormat::Rgba16Float,
            halfWidth,
            halfHeight,
            1,
            1,
            1,
            rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
            false
        });
        m_historyVolumetricHandle[i] = renderer::rhi::gl::registerTexture({
            m_historyVolumetricTex[i],
            RhiTextureDimension::Texture2D,
            RhiTextureFormat::Rgba16Float,
            halfWidth,
            halfHeight,
            1,
            1,
            1,
            rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
            false
        });
    }
    m_temporalCurrentHandle = renderer::rhi::gl::registerTexture({
        m_temporalCurrentTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_velocityHandle = renderer::rhi::gl::registerTexture({
        m_velocityTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rg16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_perObjectVelocityHandle = renderer::rhi::gl::registerTexture({
        m_perObjectVelocityTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rg16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_weatherMaskHandle = renderer::rhi::gl::registerTexture({
        m_weatherMaskTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::R8Unorm,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_skyCaptureHandle = renderer::rhi::gl::registerTexture({
        m_skyCaptureTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(kSkyCaptureWidth),
        static_cast<uint32_t>(kSkyCaptureHeight),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_shadowDepthHandle = renderer::rhi::gl::registerTexture({
        m_shadowDepth,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Depth32Float,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment),
        false
    });
    m_shadowDepthComparisonHandle = renderer::rhi::gl::registerTexture({
        m_shadowDepthComparison,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Depth32Float,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled),
        true
    });
    m_shadowColorHandle = renderer::rhi::gl::registerTexture({
        m_shadowColor,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba8Unorm,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_shadowNormalHandle = renderer::rhi::gl::registerTexture({
        m_shadowNormal,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_ssaoHandle = renderer::rhi::gl::registerTexture({
        m_ssaoTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::R8Unorm,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_ssaoFilteredHandle = renderer::rhi::gl::registerTexture({
        m_ssaoFilteredTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::R8Unorm,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_ssaoHalfResHandle = renderer::rhi::gl::registerTexture({
        m_ssaoHalfResTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::R8Unorm,
        halfWidth,
        halfHeight,
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_ssaoHalfResFilteredHandle = renderer::rhi::gl::registerTexture({
        m_ssaoHalfResFilteredTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::R8Unorm,
        halfWidth,
        halfHeight,
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    for (int i = 0; i < 2; ++i) {
        m_ssaoHistoryHandle[i] = renderer::rhi::gl::registerTexture({
            m_ssaoHistoryTex[i],
            RhiTextureDimension::Texture2D,
            RhiTextureFormat::R8Unorm,
            static_cast<uint32_t>(m_width),
            static_cast<uint32_t>(m_height),
            1,
            1,
            1,
            rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
            false
        });
    }
    m_ssaoTemporalHandle = renderer::rhi::gl::registerTexture({
        m_ssaoTemporalTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::R8Unorm,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_ssgiHandle = renderer::rhi::gl::registerTexture({
        m_ssgiTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_ssgiHalfResHandle = renderer::rhi::gl::registerTexture({
        m_ssgiHalfResTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        halfWidth,
        halfHeight,
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    for (int i = 0; i < 2; ++i) {
        m_ssgiDenoiseHandle[i] = renderer::rhi::gl::registerTexture({
            m_ssgiDenoiseTex[i],
            RhiTextureDimension::Texture2D,
            RhiTextureFormat::Rgba16Float,
            static_cast<uint32_t>(m_width),
            static_cast<uint32_t>(m_height),
            1,
            1,
            1,
            rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
            false
        });
        m_ssgiHistoryHandle[i] = renderer::rhi::gl::registerTexture({
            m_ssgiHistoryTex[i],
            RhiTextureDimension::Texture2D,
            RhiTextureFormat::Rgba16Float,
            static_cast<uint32_t>(m_width),
            static_cast<uint32_t>(m_height),
            1,
            1,
            1,
            rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
            false
        });
        m_ssgiMomentsHistoryHandle[i] = renderer::rhi::gl::registerTexture({
            m_ssgiMomentsHistoryTex[i],
            RhiTextureDimension::Texture2D,
            RhiTextureFormat::Rgba16Float,
            static_cast<uint32_t>(m_width),
            static_cast<uint32_t>(m_height),
            1,
            1,
            1,
            rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
            false
        });
    }
    m_ssgiTemporalHandle = renderer::rhi::gl::registerTexture({
        m_ssgiTemporalTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_ssgiTemporalMomentsHandle = renderer::rhi::gl::registerTexture({
        m_ssgiTemporalMomentsTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_width),
        static_cast<uint32_t>(m_height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_csmShadowDepthHandle = renderer::rhi::gl::registerTexture({
        m_csmShadowDepth,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Depth24,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(kShadowCascadeCount),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment),
        false
    });
    m_csmShadowDepthComparisonHandle = renderer::rhi::gl::registerTexture({
        m_csmShadowDepthComparison,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Depth24,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(kShadowCascadeCount),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled),
        true
    });
    m_csmShadowDepthAllHandle = renderer::rhi::gl::registerTexture({
        m_csmShadowDepthAll,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Depth24,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(kShadowCascadeCount),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment),
        false
    });
    m_csmShadowDepthAllComparisonHandle = renderer::rhi::gl::registerTexture({
        m_csmShadowDepthAllComparison,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Depth24,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(kShadowCascadeCount),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled),
        true
    });
    m_csmShadowColor0Handle = renderer::rhi::gl::registerTexture({
        m_csmShadowColor0,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Rgba8Unorm,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(kShadowCascadeCount),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    m_csmShadowColor1Handle = renderer::rhi::gl::registerTexture({
        m_csmShadowColor1,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(kShadowCascadeCount),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });

    if (!registerAtmosphereLutTexture()) {
        MECRAFT_LOG_STREAM(std::cerr << "DeferredRenderTargets: failed to register atmosphere LUT texture handle\n");
        unregisterRhiTextures();
        return false;
    }

    const bool registered = m_gAlbedoHandle.isValid() &&
                            m_gNormalAoHandle.isValid() &&
                            m_gVoxelLightHandle.isValid() &&
                            m_gMaterialHandle.isValid() &&
                            m_gMaterialAuxHandle.isValid() &&
                            m_gDepthHandle.isValid() &&
                            m_sceneLightingHandle.isValid() &&
                            m_sceneCompositeHandle.isValid() &&
                            m_sceneResolvedHandle.isValid() &&
                            m_transparentCompositeHandle.isValid() &&
                            m_transparentCompositeDepthHandle.isValid() &&
                            m_halfResHandle.isValid() &&
                            m_reflectionHandle.isValid() &&
                            m_reflectionTemporalScratchHandle.isValid() &&
                            m_cloudHandle.isValid() &&
                            m_historySceneHandle[0].isValid() &&
                            m_historySceneHandle[1].isValid() &&
                            m_historyDepthHandle[0].isValid() &&
                            m_historyDepthHandle[1].isValid() &&
                            m_historyReflectionHandle[0].isValid() &&
                            m_historyReflectionHandle[1].isValid() &&
                            m_historyCloudHandle[0].isValid() &&
                            m_historyCloudHandle[1].isValid() &&
                            m_historyVolumetricHandle[0].isValid() &&
                            m_historyVolumetricHandle[1].isValid() &&
                            m_temporalCurrentHandle.isValid() &&
                            m_velocityHandle.isValid() &&
                            m_perObjectVelocityHandle.isValid() &&
                            m_weatherMaskHandle.isValid() &&
                            m_skyCaptureHandle.isValid() &&
                            m_shadowDepthHandle.isValid() &&
                            m_shadowDepthComparisonHandle.isValid() &&
                            m_shadowColorHandle.isValid() &&
                            m_shadowNormalHandle.isValid() &&
                            m_ssaoHandle.isValid() &&
                            m_ssaoFilteredHandle.isValid() &&
                            m_ssaoHalfResHandle.isValid() &&
                            m_ssaoHalfResFilteredHandle.isValid() &&
                            m_ssaoHistoryHandle[0].isValid() &&
                            m_ssaoHistoryHandle[1].isValid() &&
                            m_ssaoTemporalHandle.isValid() &&
                            m_ssgiHandle.isValid() &&
                            m_ssgiHalfResHandle.isValid() &&
                            m_ssgiDenoiseHandle[0].isValid() &&
                            m_ssgiDenoiseHandle[1].isValid() &&
                            m_ssgiHistoryHandle[0].isValid() &&
                            m_ssgiHistoryHandle[1].isValid() &&
                            m_ssgiMomentsHistoryHandle[0].isValid() &&
                            m_ssgiMomentsHistoryHandle[1].isValid() &&
                            m_ssgiTemporalHandle.isValid() &&
                            m_ssgiTemporalMomentsHandle.isValid() &&
                            m_csmShadowDepthHandle.isValid() &&
                            m_csmShadowDepthComparisonHandle.isValid() &&
                            m_csmShadowDepthAllHandle.isValid() &&
                            m_csmShadowDepthAllComparisonHandle.isValid() &&
                            m_csmShadowColor0Handle.isValid() &&
                            m_csmShadowColor1Handle.isValid();
    if (!registered) {
        MECRAFT_LOG_STREAM(std::cerr << "DeferredRenderTargets: failed to register RHI texture handles\n");
        unregisterRhiTextures();
        return false;
    }
    return true;
}

bool DeferredRenderTargets::registerAtmosphereLutTexture() {
    if (m_atmosphereLutHandle.isValid()) {
        return true;
    }

    m_atmosphereLutHandle = renderer::rhi::gl::registerTexture({
        m_atmosphereLut3d,
        RhiTextureDimension::Texture3D,
        RhiTextureFormat::Rgba32Float,
        static_cast<uint32_t>(kAtmosphereLutWidth),
        static_cast<uint32_t>(kAtmosphereLutHeight),
        static_cast<uint32_t>(kAtmosphereLutDepth),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled),
        false
    });
    return m_atmosphereLutHandle.isValid();
}

RhiTextureViewHandle DeferredRenderTargets::csmShadowDepthTextureViewHandle(const int cascadeIndex) const {
    assert(cascadeIndex >= 0 && cascadeIndex < kShadowCascadeCount);
    return m_csmShadowDepthView[cascadeIndex];
}

RhiTextureViewHandle DeferredRenderTargets::csmShadowDepthAllTextureViewHandle(const int cascadeIndex) const {
    assert(cascadeIndex >= 0 && cascadeIndex < kShadowCascadeCount);
    return m_csmShadowDepthAllView[cascadeIndex];
}

RhiTextureViewHandle DeferredRenderTargets::csmShadowColor0TextureViewHandle(const int cascadeIndex) const {
    assert(cascadeIndex >= 0 && cascadeIndex < kShadowCascadeCount);
    return m_csmShadowColor0View[cascadeIndex];
}

RhiTextureViewHandle DeferredRenderTargets::csmShadowColor1TextureViewHandle(const int cascadeIndex) const {
    assert(cascadeIndex >= 0 && cascadeIndex < kShadowCascadeCount);
    return m_csmShadowColor1View[cascadeIndex];
}

bool DeferredRenderTargets::ensureCsmShadowDepthTextureView(RhiDevice& rhiDevice, const int cascadeIndex) {
    assert(cascadeIndex >= 0 && cascadeIndex < kShadowCascadeCount);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }

    RhiTextureViewHandle& view = m_csmShadowDepthView[cascadeIndex];
    if (view.isValid()) {
        return true;
    }

    if (!m_csmShadowDepthHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_csmShadowDepthHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Depth32Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = static_cast<uint32_t>(cascadeIndex);
    desc.layerCount = 1;

    view = rhiDevice.createTextureView(desc);
    if (!view.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureCsmShadowTransparentTextureViews(RhiDevice& rhiDevice,
                                                                   const int cascadeIndex) {
    assert(cascadeIndex >= 0 && cascadeIndex < kShadowCascadeCount);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }

    if (m_csmShadowDepthAllView[cascadeIndex].isValid() &&
        m_csmShadowColor0View[cascadeIndex].isValid() &&
        m_csmShadowColor1View[cascadeIndex].isValid()) {
        return true;
    }

    if (!m_csmShadowDepthAllHandle.isValid() ||
        !m_csmShadowColor0Handle.isValid() ||
        !m_csmShadowColor1Handle.isValid()) {
        return false;
    }

    const auto destroyCascadeViews = [&rhiDevice, this, cascadeIndex]() {
        RhiTextureViewHandle& depthView = m_csmShadowDepthAllView[cascadeIndex];
        RhiTextureViewHandle& color0View = m_csmShadowColor0View[cascadeIndex];
        RhiTextureViewHandle& color1View = m_csmShadowColor1View[cascadeIndex];
        if (depthView.isValid()) {
            rhiDevice.destroyTextureView(depthView);
        }
        if (color0View.isValid()) {
            rhiDevice.destroyTextureView(color0View);
        }
        if (color1View.isValid()) {
            rhiDevice.destroyTextureView(color1View);
        }
        depthView = {};
        color0View = {};
        color1View = {};
    };

    if (m_csmShadowDepthAllView[cascadeIndex].isValid() ||
        m_csmShadowColor0View[cascadeIndex].isValid() ||
        m_csmShadowColor1View[cascadeIndex].isValid()) {
        destroyCascadeViews();
    }

    RhiTextureViewDesc desc;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = static_cast<uint32_t>(cascadeIndex);
    desc.layerCount = 1;

    desc.texture = m_csmShadowDepthAllHandle;
    desc.format = RhiTextureFormat::Depth32Float;
    m_csmShadowDepthAllView[cascadeIndex] = rhiDevice.createTextureView(desc);

    desc.texture = m_csmShadowColor0Handle;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    m_csmShadowColor0View[cascadeIndex] = rhiDevice.createTextureView(desc);

    desc.texture = m_csmShadowColor1Handle;
    desc.format = RhiTextureFormat::Rgba16Float;
    m_csmShadowColor1View[cascadeIndex] = rhiDevice.createTextureView(desc);

    if (!m_csmShadowDepthAllView[cascadeIndex].isValid() ||
        !m_csmShadowColor0View[cascadeIndex].isValid() ||
        !m_csmShadowColor1View[cascadeIndex].isValid()) {
        destroyCascadeViews();
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureGBufferTextureViews(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }

    const bool allViewsValid = m_gAlbedoView.isValid() &&
                               m_gNormalAoView.isValid() &&
                               m_gVoxelLightView.isValid() &&
                               m_gMaterialView.isValid() &&
                               m_gMaterialAuxView.isValid() &&
                               m_gDepthView.isValid();
    if (allViewsValid) {
        return true;
    }

    if (!m_gAlbedoHandle.isValid() ||
        !m_gNormalAoHandle.isValid() ||
        !m_gVoxelLightHandle.isValid() ||
        !m_gMaterialHandle.isValid() ||
        !m_gMaterialAuxHandle.isValid() ||
        !m_gDepthHandle.isValid()) {
        return false;
    }

    const auto destroyGBufferViews = [&rhiDevice, this]() {
        if (m_gAlbedoView.isValid()) {
            rhiDevice.destroyTextureView(m_gAlbedoView);
        }
        if (m_gNormalAoView.isValid()) {
            rhiDevice.destroyTextureView(m_gNormalAoView);
        }
        if (m_gVoxelLightView.isValid()) {
            rhiDevice.destroyTextureView(m_gVoxelLightView);
        }
        if (m_gMaterialView.isValid()) {
            rhiDevice.destroyTextureView(m_gMaterialView);
        }
        if (m_gMaterialAuxView.isValid()) {
            rhiDevice.destroyTextureView(m_gMaterialAuxView);
        }
        if (m_gDepthView.isValid()) {
            rhiDevice.destroyTextureView(m_gDepthView);
        }
        m_gAlbedoView = {};
        m_gNormalAoView = {};
        m_gVoxelLightView = {};
        m_gMaterialView = {};
        m_gMaterialAuxView = {};
        m_gDepthView = {};
    };

    const bool anyViewValid = m_gAlbedoView.isValid() ||
                              m_gNormalAoView.isValid() ||
                              m_gVoxelLightView.isValid() ||
                              m_gMaterialView.isValid() ||
                              m_gMaterialAuxView.isValid() ||
                              m_gDepthView.isValid();
    if (anyViewValid) {
        destroyGBufferViews();
    }

    RhiTextureViewDesc desc;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    desc.texture = m_gAlbedoHandle;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    m_gAlbedoView = rhiDevice.createTextureView(desc);

    desc.texture = m_gNormalAoHandle;
    desc.format = RhiTextureFormat::Rgba16Float;
    m_gNormalAoView = rhiDevice.createTextureView(desc);

    desc.texture = m_gVoxelLightHandle;
    desc.format = RhiTextureFormat::Rg8Unorm;
    m_gVoxelLightView = rhiDevice.createTextureView(desc);

    desc.texture = m_gMaterialHandle;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    m_gMaterialView = rhiDevice.createTextureView(desc);

    desc.texture = m_gMaterialAuxHandle;
    desc.format = RhiTextureFormat::Rgba8Unorm;
    m_gMaterialAuxView = rhiDevice.createTextureView(desc);

    desc.texture = m_gDepthHandle;
    desc.format = RhiTextureFormat::Depth32Float;
    m_gDepthView = rhiDevice.createTextureView(desc);

    if (!m_gAlbedoView.isValid() ||
        !m_gNormalAoView.isValid() ||
        !m_gVoxelLightView.isValid() ||
        !m_gMaterialView.isValid() ||
        !m_gMaterialAuxView.isValid() ||
        !m_gDepthView.isValid()) {
        destroyGBufferViews();
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureVelocityTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_velocityView.isValid()) {
        return true;
    }

    if (!m_velocityHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_velocityHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rg16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_velocityView = rhiDevice.createTextureView(desc);
    if (!m_velocityView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensurePerObjectVelocityTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_perObjectVelocityView.isValid()) {
        return true;
    }

    if (!m_perObjectVelocityHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_perObjectVelocityHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rg16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_perObjectVelocityView = rhiDevice.createTextureView(desc);
    if (!m_perObjectVelocityView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsaoFilteredTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssaoFilteredView.isValid()) {
        return true;
    }

    if (!m_ssaoFilteredHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssaoFilteredHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::R8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssaoFilteredView = rhiDevice.createTextureView(desc);
    if (!m_ssaoFilteredView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsaoHalfResTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssaoHalfResView.isValid()) {
        return true;
    }

    if (!m_ssaoHalfResHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssaoHalfResHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::R8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssaoHalfResView = rhiDevice.createTextureView(desc);
    if (!m_ssaoHalfResView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsaoHalfResFilteredTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssaoHalfResFilteredView.isValid()) {
        return true;
    }

    if (!m_ssaoHalfResFilteredHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssaoHalfResFilteredHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::R8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssaoHalfResFilteredView = rhiDevice.createTextureView(desc);
    if (!m_ssaoHalfResFilteredView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsaoTemporalTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssaoTemporalView.isValid()) {
        return true;
    }

    if (!m_ssaoTemporalHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssaoTemporalHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::R8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssaoTemporalView = rhiDevice.createTextureView(desc);
    if (!m_ssaoTemporalView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSceneLightingTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_sceneLightingView.isValid()) {
        return true;
    }

    if (!m_sceneLightingHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_sceneLightingHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_sceneLightingView = rhiDevice.createTextureView(desc);
    if (!m_sceneLightingView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsgiTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssgiView.isValid()) {
        return true;
    }

    if (!m_ssgiHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssgiHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssgiView = rhiDevice.createTextureView(desc);
    if (!m_ssgiView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsgiHalfResTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssgiHalfResView.isValid()) {
        return true;
    }

    if (!m_ssgiHalfResHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssgiHalfResHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssgiHalfResView = rhiDevice.createTextureView(desc);
    if (!m_ssgiHalfResView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsgiDenoiseTextureView(RhiDevice& rhiDevice, const int slot) {
    assert(slot >= 0 && slot < 2);
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssgiDenoiseView[slot].isValid()) {
        return true;
    }

    if (!m_ssgiDenoiseHandle[slot].isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_ssgiDenoiseHandle[slot];
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_ssgiDenoiseView[slot] = rhiDevice.createTextureView(desc);
    if (!m_ssgiDenoiseView[slot].isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSsgiTemporalTextureViews(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssgiTemporalView.isValid() && m_ssgiTemporalMomentsView.isValid()) {
        return true;
    }

    if (!m_ssgiTemporalHandle.isValid() || !m_ssgiTemporalMomentsHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc temporalDesc;
    temporalDesc.texture = m_ssgiTemporalHandle;
    temporalDesc.viewType = RhiTextureViewType::Texture2D;
    temporalDesc.format = RhiTextureFormat::Rgba16Float;
    temporalDesc.baseMip = 0;
    temporalDesc.mipCount = 1;
    temporalDesc.baseLayer = 0;
    temporalDesc.layerCount = 1;

    RhiTextureViewDesc momentsDesc = temporalDesc;
    momentsDesc.texture = m_ssgiTemporalMomentsHandle;

    m_ssgiTemporalView = rhiDevice.createTextureView(temporalDesc);
    m_ssgiTemporalMomentsView = rhiDevice.createTextureView(momentsDesc);
    if (!m_ssgiTemporalView.isValid() || !m_ssgiTemporalMomentsView.isValid()) {
        if (m_ssgiTemporalView.isValid()) {
            rhiDevice.destroyTextureView(m_ssgiTemporalView);
        }
        if (m_ssgiTemporalMomentsView.isValid()) {
            rhiDevice.destroyTextureView(m_ssgiTemporalMomentsView);
        }
        m_ssgiTemporalView = {};
        m_ssgiTemporalMomentsView = {};
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureCloudTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_cloudView.isValid()) {
        return true;
    }

    if (!m_cloudHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_cloudHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_cloudView = rhiDevice.createTextureView(desc);
    if (!m_cloudView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSkyCaptureTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_skyCaptureView.isValid()) {
        return true;
    }

    if (!m_skyCaptureHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_skyCaptureHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_skyCaptureView = rhiDevice.createTextureView(desc);
    if (!m_skyCaptureView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureReflectionTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_reflectionView.isValid()) {
        return true;
    }

    if (!m_reflectionHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_reflectionHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_reflectionView = rhiDevice.createTextureView(desc);
    if (!m_reflectionView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSceneCompositeTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_sceneCompositeView.isValid()) {
        return true;
    }

    if (!m_sceneCompositeHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_sceneCompositeHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_sceneCompositeView = rhiDevice.createTextureView(desc);
    if (!m_sceneCompositeView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureSceneResolvedTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_sceneResolvedView.isValid()) {
        return true;
    }

    if (!m_sceneResolvedHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_sceneResolvedHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_sceneResolvedView = rhiDevice.createTextureView(desc);
    if (!m_sceneResolvedView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureTransparentCompositeTextureViews(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_transparentCompositeView.isValid() && m_transparentCompositeDepthView.isValid()) {
        return true;
    }

    if (!m_transparentCompositeHandle.isValid() || !m_transparentCompositeDepthHandle.isValid()) {
        return false;
    }

    if (m_transparentCompositeView.isValid()) {
        rhiDevice.destroyTextureView(m_transparentCompositeView);
        m_transparentCompositeView = {};
    }
    if (m_transparentCompositeDepthView.isValid()) {
        rhiDevice.destroyTextureView(m_transparentCompositeDepthView);
        m_transparentCompositeDepthView = {};
    }

    RhiTextureViewDesc colorDesc;
    colorDesc.texture = m_transparentCompositeHandle;
    colorDesc.viewType = RhiTextureViewType::Texture2D;
    colorDesc.format = RhiTextureFormat::Rgba16Float;
    colorDesc.baseMip = 0;
    colorDesc.mipCount = 1;
    colorDesc.baseLayer = 0;
    colorDesc.layerCount = 1;

    RhiTextureViewDesc depthDesc;
    depthDesc.texture = m_transparentCompositeDepthHandle;
    depthDesc.viewType = RhiTextureViewType::Texture2D;
    depthDesc.format = RhiTextureFormat::Depth32Float;
    depthDesc.baseMip = 0;
    depthDesc.mipCount = 1;
    depthDesc.baseLayer = 0;
    depthDesc.layerCount = 1;

    m_transparentCompositeView = rhiDevice.createTextureView(colorDesc);
    m_transparentCompositeDepthView = rhiDevice.createTextureView(depthDesc);
    if (!m_transparentCompositeView.isValid() || !m_transparentCompositeDepthView.isValid()) {
        if (m_transparentCompositeView.isValid()) {
            rhiDevice.destroyTextureView(m_transparentCompositeView);
        }
        if (m_transparentCompositeDepthView.isValid()) {
            rhiDevice.destroyTextureView(m_transparentCompositeDepthView);
        }
        m_transparentCompositeView = {};
        m_transparentCompositeDepthView = {};
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHalfResTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_halfResView.isValid()) {
        return true;
    }

    if (!m_halfResHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_halfResHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_halfResView = rhiDevice.createTextureView(desc);
    if (!m_halfResView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHistoryVolumetricTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }

    RhiTextureViewHandle& view = m_historyVolumetricView[m_currentHistoryIndex];
    if (view.isValid()) {
        return true;
    }

    const RhiTextureHandle texture = m_historyVolumetricHandle[m_currentHistoryIndex];
    if (!texture.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = texture;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    view = rhiDevice.createTextureView(desc);
    if (!view.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureWeatherMaskTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_weatherMaskView.isValid()) {
        return true;
    }

    if (!m_weatherMaskHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_weatherMaskHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::R8Unorm;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_weatherMaskView = rhiDevice.createTextureView(desc);
    if (!m_weatherMaskView.isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

void DeferredRenderTargets::destroyRhiTextureViews() {
    if (m_rhiViewDevice != nullptr && m_gAlbedoView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_gAlbedoView);
    }
    if (m_rhiViewDevice != nullptr && m_gNormalAoView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_gNormalAoView);
    }
    if (m_rhiViewDevice != nullptr && m_gVoxelLightView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_gVoxelLightView);
    }
    if (m_rhiViewDevice != nullptr && m_gMaterialView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_gMaterialView);
    }
    if (m_rhiViewDevice != nullptr && m_gMaterialAuxView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_gMaterialAuxView);
    }
    if (m_rhiViewDevice != nullptr && m_gDepthView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_gDepthView);
    }
    for (RhiTextureViewHandle& view : m_csmShadowDepthView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_csmShadowDepthAllView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_csmShadowColor0View) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_csmShadowColor1View) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    if (m_rhiViewDevice != nullptr && m_velocityView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_velocityView);
    }
    if (m_rhiViewDevice != nullptr && m_perObjectVelocityView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_perObjectVelocityView);
    }
    if (m_rhiViewDevice != nullptr && m_ssaoFilteredView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssaoFilteredView);
    }
    if (m_rhiViewDevice != nullptr && m_ssaoHalfResView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssaoHalfResView);
    }
    if (m_rhiViewDevice != nullptr && m_ssaoHalfResFilteredView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssaoHalfResFilteredView);
    }
    if (m_rhiViewDevice != nullptr && m_ssaoTemporalView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssaoTemporalView);
    }
    if (m_rhiViewDevice != nullptr && m_sceneLightingView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_sceneLightingView);
    }
    if (m_rhiViewDevice != nullptr && m_ssgiView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssgiView);
    }
    if (m_rhiViewDevice != nullptr && m_ssgiHalfResView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssgiHalfResView);
    }
    for (RhiTextureViewHandle& view : m_ssgiDenoiseView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    if (m_rhiViewDevice != nullptr && m_ssgiTemporalView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssgiTemporalView);
    }
    if (m_rhiViewDevice != nullptr && m_ssgiTemporalMomentsView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_ssgiTemporalMomentsView);
    }
    if (m_rhiViewDevice != nullptr && m_sceneCompositeView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_sceneCompositeView);
    }
    if (m_rhiViewDevice != nullptr && m_sceneResolvedView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_sceneResolvedView);
    }
    if (m_rhiViewDevice != nullptr && m_transparentCompositeView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_transparentCompositeView);
    }
    if (m_rhiViewDevice != nullptr && m_transparentCompositeDepthView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_transparentCompositeDepthView);
    }
    if (m_rhiViewDevice != nullptr && m_halfResView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_halfResView);
    }
    if (m_rhiViewDevice != nullptr && m_reflectionView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_reflectionView);
    }
    if (m_rhiViewDevice != nullptr && m_cloudView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_cloudView);
    }
    if (m_rhiViewDevice != nullptr && m_skyCaptureView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_skyCaptureView);
    }
    for (RhiTextureViewHandle& view : m_historyVolumetricView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    if (m_rhiViewDevice != nullptr && m_weatherMaskView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_weatherMaskView);
    }
    m_gAlbedoView = {};
    m_gNormalAoView = {};
    m_gVoxelLightView = {};
    m_gMaterialView = {};
    m_gMaterialAuxView = {};
    m_gDepthView = {};
    m_velocityView = {};
    m_perObjectVelocityView = {};
    m_ssaoFilteredView = {};
    m_ssaoHalfResView = {};
    m_ssaoHalfResFilteredView = {};
    m_ssaoTemporalView = {};
    m_sceneLightingView = {};
    m_ssgiView = {};
    m_ssgiHalfResView = {};
    m_ssgiTemporalView = {};
    m_ssgiTemporalMomentsView = {};
    m_sceneCompositeView = {};
    m_sceneResolvedView = {};
    m_transparentCompositeView = {};
    m_transparentCompositeDepthView = {};
    m_halfResView = {};
    m_reflectionView = {};
    m_cloudView = {};
    m_skyCaptureView = {};
    m_weatherMaskView = {};
    m_rhiViewDevice = nullptr;
}

void DeferredRenderTargets::unregisterRhiTextures() {
    destroyRhiTextureViews();
    renderer::rhi::gl::unregisterTextureAndReset(m_gAlbedoHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_gNormalAoHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_gVoxelLightHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_gMaterialHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_gMaterialAuxHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_gDepthHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_sceneLightingHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_sceneCompositeHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_sceneResolvedHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_transparentCompositeHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_transparentCompositeDepthHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_halfResHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_reflectionHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_reflectionTemporalScratchHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_cloudHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_historySceneHandle[0]);
    renderer::rhi::gl::unregisterTextureAndReset(m_historySceneHandle[1]);
    renderer::rhi::gl::unregisterTextureAndReset(m_historyDepthHandle[0]);
    renderer::rhi::gl::unregisterTextureAndReset(m_historyDepthHandle[1]);
    renderer::rhi::gl::unregisterTextureAndReset(m_historyReflectionHandle[0]);
    renderer::rhi::gl::unregisterTextureAndReset(m_historyReflectionHandle[1]);
    renderer::rhi::gl::unregisterTextureAndReset(m_historyCloudHandle[0]);
    renderer::rhi::gl::unregisterTextureAndReset(m_historyCloudHandle[1]);
    renderer::rhi::gl::unregisterTextureAndReset(m_historyVolumetricHandle[0]);
    renderer::rhi::gl::unregisterTextureAndReset(m_historyVolumetricHandle[1]);
    renderer::rhi::gl::unregisterTextureAndReset(m_temporalCurrentHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_velocityHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_perObjectVelocityHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_weatherMaskHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_skyCaptureHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_atmosphereLutHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_shadowDepthHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_shadowDepthComparisonHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_shadowColorHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_shadowNormalHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssaoHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssaoFilteredHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssaoHalfResHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssaoHalfResFilteredHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssaoHistoryHandle[0]);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssaoHistoryHandle[1]);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssaoTemporalHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssgiHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssgiHalfResHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssgiDenoiseHandle[0]);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssgiDenoiseHandle[1]);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssgiHistoryHandle[0]);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssgiHistoryHandle[1]);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssgiMomentsHistoryHandle[0]);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssgiMomentsHistoryHandle[1]);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssgiTemporalHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_ssgiTemporalMomentsHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_csmShadowDepthHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_csmShadowDepthComparisonHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_csmShadowDepthAllHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_csmShadowDepthAllComparisonHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_csmShadowColor0Handle);
    renderer::rhi::gl::unregisterTextureAndReset(m_csmShadowColor1Handle);
}

void DeferredRenderTargets::destroyFramebuffers() {
    unregisterRhiTextures();

    const GLuint textures[] = {
        m_gAlbedo,
        m_gNormalAo,
        m_gVoxelLight,
        m_gMaterial,
        m_gMaterialAux,
        m_gDepth,
        m_shadowDepth,
        m_shadowDepthComparison,
        m_shadowColor,
        m_shadowNormal,
        m_csmShadowDepth,
        m_csmShadowDepthComparison,
        m_csmShadowDepthAll,
        m_csmShadowDepthAllComparison,
        m_csmShadowColor0,
        m_csmShadowColor1,
        m_ssaoTex,
        m_ssaoFilteredTex,
        m_sceneLightingTex,
        m_sceneCompositeTex,
        m_sceneResolvedTex,
        m_temporalCurrentTex,
        m_transparentCompositeTex,
        m_transparentCompositeDepth,
        m_halfResTex,
        m_reflectionTex,
        m_reflectionTemporalScratchTex,
        m_cloudTex,
        m_skyCaptureTex,
        m_historySceneTex[0], m_historySceneTex[1],
        m_historyDepthTex[0], m_historyDepthTex[1],
        m_historyReflectionTex[0], m_historyReflectionTex[1],
        m_historyCloudTex[0], m_historyCloudTex[1],
        m_historyVolumetricTex[0], m_historyVolumetricTex[1],
        m_ssaoHalfResTex, m_ssaoHalfResFilteredTex,
        m_ssaoHistoryTex[0], m_ssaoHistoryTex[1],
        m_ssaoTemporalTex,
        m_ssgiTex,
        m_ssgiHalfResTex,
        m_ssgiDenoiseTex[0], m_ssgiDenoiseTex[1],
        m_ssgiHistoryTex[0], m_ssgiHistoryTex[1],
        m_ssgiMomentsHistoryTex[0], m_ssgiMomentsHistoryTex[1],
        m_ssgiTemporalTex,
        m_ssgiTemporalMomentsTex,
        m_velocityTex,
        m_perObjectVelocityTex,
        m_weatherMaskTex
    };
    for (const GLuint texture : textures) {
        if (texture != 0) {
            GLuint mutableTexture = texture;
            glDeleteTextures(1, &mutableTexture);
        }
    }
    m_gAlbedo = 0;
    m_gNormalAo = 0;
    m_gVoxelLight = 0;
    m_gMaterial = 0;
    m_gMaterialAux = 0;
    m_gDepth = 0;
    m_shadowDepth = 0;
    m_shadowDepthComparison = 0;
    m_shadowColor = 0;
    m_shadowNormal = 0;
    m_csmShadowDepth = 0;
    m_csmShadowDepthComparison = 0;
    m_csmShadowDepthAll = 0;
    m_csmShadowDepthAllComparison = 0;
    m_csmShadowColor0 = 0;
    m_csmShadowColor1 = 0;
    m_ssaoTex = 0;
    m_ssaoFilteredTex = 0;
    m_ssaoHalfResTex = 0;
    m_ssaoHalfResFilteredTex = 0;
    m_sceneLightingTex = 0;
    m_sceneCompositeTex = 0;
    m_sceneResolvedTex = 0;
    m_temporalCurrentTex = 0;
    m_transparentCompositeTex = 0;
    m_transparentCompositeDepth = 0;
    m_halfResTex = 0;
    m_reflectionTex = 0;
    m_reflectionTemporalScratchTex = 0;
    m_cloudTex = 0;
    m_skyCaptureTex = 0;
    m_historySceneTex[0] = 0; m_historySceneTex[1] = 0;
    m_historyDepthTex[0] = 0; m_historyDepthTex[1] = 0;
    m_historyReflectionTex[0] = 0; m_historyReflectionTex[1] = 0;
    m_historyCloudTex[0] = 0; m_historyCloudTex[1] = 0;
    m_historyVolumetricTex[0] = 0; m_historyVolumetricTex[1] = 0;
    m_ssaoHistoryTex[0] = 0; m_ssaoHistoryTex[1] = 0;
    m_ssaoTemporalTex = 0;
    m_ssgiTex = 0;
    m_ssgiHalfResTex = 0;
    m_ssgiDenoiseTex[0] = 0; m_ssgiDenoiseTex[1] = 0;
    m_ssgiHistoryTex[0] = 0; m_ssgiHistoryTex[1] = 0;
    m_ssgiMomentsHistoryTex[0] = 0; m_ssgiMomentsHistoryTex[1] = 0;
    m_ssgiTemporalTex = 0;
    m_ssgiTemporalMomentsTex = 0;
    m_velocityTex = 0;
    m_perObjectVelocityTex = 0;
    m_weatherMaskTex = 0;

    const GLuint framebuffers[] = {m_gBufferFbo, m_shadowFbo, m_csmShadowFbo, m_csmShadowTransparentFbo, m_ssaoFbo, m_ssaoFilteredFbo, m_sceneLightingFbo, m_sceneCompositeFbo, m_sceneResolvedFbo, m_temporalCurrentFbo, m_transparentCompositeFbo, m_halfResFbo, m_reflectionFbo, m_reflectionTemporalScratchFbo, m_cloudFbo, m_skyCaptureFbo, m_historySceneFbo[0], m_historySceneFbo[1], m_historyReflectionFbo[0], m_historyReflectionFbo[1], m_historyCloudFbo[0], m_historyCloudFbo[1], m_historyVolumetricFbo[0], m_historyVolumetricFbo[1], m_ssaoHalfResFbo, m_ssaoHalfResFilteredFbo, m_ssaoHistoryFbo[0], m_ssaoHistoryFbo[1], m_ssaoTemporalFbo, m_ssgiFbo, m_ssgiHalfResFbo, m_ssgiDenoiseFbo[0], m_ssgiDenoiseFbo[1], m_ssgiHistoryFbo[0], m_ssgiHistoryFbo[1], m_ssgiTemporalFbo, m_velocityFbo, m_weatherMaskFbo};
    for (const GLuint framebuffer : framebuffers) {
        if (framebuffer != 0) {
            GLuint mutableFramebuffer = framebuffer;
            glDeleteFramebuffers(1, &mutableFramebuffer);
        }
    }
    m_gBufferFbo = 0;
    m_shadowFbo = 0;
    m_csmShadowFbo = 0;
    m_csmShadowTransparentFbo = 0;
    m_ssaoFbo = 0;
    m_ssaoFilteredFbo = 0;
    m_ssaoHalfResFbo = 0;
    m_ssaoHalfResFilteredFbo = 0;
    m_sceneLightingFbo = 0;
    m_sceneCompositeFbo = 0;
    m_sceneResolvedFbo = 0;
    m_temporalCurrentFbo = 0;
    m_transparentCompositeFbo = 0;
    m_halfResFbo = 0;
    m_reflectionFbo = 0;
    m_reflectionTemporalScratchFbo = 0;
    m_cloudFbo = 0;
    m_skyCaptureFbo = 0;
    m_historySceneFbo[0] = 0; m_historySceneFbo[1] = 0;
    m_historyReflectionFbo[0] = 0; m_historyReflectionFbo[1] = 0;
    m_historyCloudFbo[0] = 0; m_historyCloudFbo[1] = 0;
    m_historyVolumetricFbo[0] = 0; m_historyVolumetricFbo[1] = 0;
    m_ssaoHistoryFbo[0] = 0; m_ssaoHistoryFbo[1] = 0;
    m_ssaoTemporalFbo = 0;
    m_ssgiFbo = 0;
    m_ssgiHalfResFbo = 0;
    m_ssgiDenoiseFbo[0] = 0; m_ssgiDenoiseFbo[1] = 0;
    m_ssgiHistoryFbo[0] = 0; m_ssgiHistoryFbo[1] = 0;
    m_ssgiTemporalFbo = 0;
    m_velocityFbo = 0;
    m_weatherMaskFbo = 0;
    m_currentHistoryIndex = 0;
    m_ssaoHistoryIndex = 0;
    m_ssgiHistoryIndex = 0;
    m_ready = false;
}

void DeferredRenderTargets::destroyFullscreenTriangle() {
    if (m_fullscreenVao != 0) {
        glDeleteVertexArrays(1, &m_fullscreenVao);
        m_fullscreenVao = 0;
    }
}

bool DeferredRenderTargets::loadAtmosphereLut(const char* path) {
    if (m_atmosphereLut3d != 0) {
        renderer::rhi::gl::unregisterTextureAndReset(m_atmosphereLutHandle);
        glDeleteTextures(1, &m_atmosphereLut3d);
        m_atmosphereLut3d = 0;
    }

    // Final.lut layout: 256 x 128 x 33, RGBA32F
    constexpr size_t kExpectedSize =
        size_t(kAtmosphereLutWidth) * kAtmosphereLutHeight * kAtmosphereLutDepth * 4 * sizeof(float);

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        MECRAFT_LOG_STREAM(std::cerr << "AtmosphereLUT: failed to open " << path << "\n");
        return false;
    }

    const auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize != kExpectedSize) {
        MECRAFT_LOG_STREAM(std::cerr << "AtmosphereLUT: unexpected file size " << fileSize
                                     << " (expected " << kExpectedSize << ")\n");
        return false;
    }

    file.seekg(0, std::ios::beg);
    std::vector<float> data(kAtmosphereLutWidth * kAtmosphereLutHeight * kAtmosphereLutDepth * 4);
    if (!file.read(reinterpret_cast<char*>(data.data()), kExpectedSize)) {
        MECRAFT_LOG_STREAM(std::cerr << "AtmosphereLUT: failed to read data\n");
        return false;
    }

    glCreateTextures(GL_TEXTURE_3D, 1, &m_atmosphereLut3d);
    glTextureStorage3D(m_atmosphereLut3d, 1, GL_RGBA32F,
                       kAtmosphereLutWidth, kAtmosphereLutHeight, kAtmosphereLutDepth);
    glTextureSubImage3D(m_atmosphereLut3d, 0, 0, 0, 0,
                        kAtmosphereLutWidth, kAtmosphereLutHeight, kAtmosphereLutDepth,
                        GL_RGBA, GL_FLOAT, data.data());
    glTextureParameteri(m_atmosphereLut3d, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_atmosphereLut3d, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_atmosphereLut3d, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_atmosphereLut3d, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_atmosphereLut3d, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // z=32 layer is the sky output (rendered at runtime); make sure it's writable
    // by NOT marking the texture as immutable after upload. Storage is already allocated.

    if (!registerAtmosphereLutTexture()) {
        MECRAFT_LOG_STREAM(std::cerr << "AtmosphereLUT: failed to register RHI texture handle\n");
        glDeleteTextures(1, &m_atmosphereLut3d);
        m_atmosphereLut3d = 0;
        return false;
    }

    MECRAFT_LOG_STREAM(std::cout << "AtmosphereLUT: loaded " << path << " (256x128x33 RGBA32F)\n");
    return true;
}
