#include "DeferredRenderTargets.h"
#include "../debug/RenderDebugLabels.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <vector>

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

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

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

void DeferredRenderTargets::bindGBuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffers[] = {
        kGAlbedoAttachment,
        kGNormalAoAttachment,
        kGVoxelLightAttachment,
        kGMaterialAttachment,
        kGMaterialAuxAttachment
    };
    glDrawBuffers(kGBufferAttachmentCount, drawBuffers);
}

void DeferredRenderTargets::bindShadowMap() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glViewport(0, 0, m_shadowResolution, m_shadowResolution);
    const GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, drawBuffers);
}

void DeferredRenderTargets::bindCsmShadowLayer(const int cascadeIndex) {
    const int layer = std::clamp(cascadeIndex, 0, kShadowCascadeCount - 1);
    glNamedFramebufferTextureLayer(m_csmShadowFbo, GL_DEPTH_ATTACHMENT, m_csmShadowDepth, 0, layer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_csmShadowFbo);
    glViewport(0, 0, m_shadowResolution, m_shadowResolution);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
}

void DeferredRenderTargets::bindCsmShadowTransparentLayer(const int cascadeIndex) {
    const int layer = std::clamp(cascadeIndex, 0, kShadowCascadeCount - 1);
    glNamedFramebufferTextureLayer(m_csmShadowTransparentFbo, GL_DEPTH_ATTACHMENT, m_csmShadowDepthAll, 0, layer);
    glNamedFramebufferTextureLayer(m_csmShadowTransparentFbo, GL_COLOR_ATTACHMENT0, m_csmShadowColor0, 0, layer);
    glNamedFramebufferTextureLayer(m_csmShadowTransparentFbo, GL_COLOR_ATTACHMENT1, m_csmShadowColor1, 0, layer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_csmShadowTransparentFbo);
    glViewport(0, 0, m_shadowResolution, m_shadowResolution);
    const GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, drawBuffers);
}

void DeferredRenderTargets::bindShadowColor() {
    // Read-only binding for sampling shadow color/normal in lighting pass
    // No-op: textures are accessed via shadowColorTexture()/shadowNormalTexture()
}

void DeferredRenderTargets::bindSsao() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindSsaoFiltered() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFilteredFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindSsaoTemporal() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoTemporalFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindSsaoHalfRes() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoHalfResFbo);
    glViewport(0, 0, std::max(1, m_width / 2), std::max(1, m_height / 2));
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindSsaoHalfResFiltered() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoHalfResFilteredFbo);
    glViewport(0, 0, std::max(1, m_width / 2), std::max(1, m_height / 2));
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindSceneLighting() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneLightingFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindSceneComposite() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneCompositeFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindSceneResolved() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneResolvedFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindTransparentComposite() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindHalfRes() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_halfResFbo);
    glViewport(0, 0, std::max(1, m_width / 2), std::max(1, m_height / 2));
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindReflection() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_reflectionFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindReflectionTemporalScratch() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_reflectionTemporalScratchFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindCloud() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_cloudFbo);
    glViewport(0, 0, std::max(1, m_width / 2), std::max(1, m_height / 2));
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindVolumetricTemporal() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_historyVolumetricFbo[m_currentHistoryIndex]);
    glViewport(0, 0, std::max(1, m_width / 2), std::max(1, m_height / 2));
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindVelocity() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_velocityFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindWeatherMask() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_weatherMaskFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::clearWeatherMask() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_weatherMaskFbo);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void DeferredRenderTargets::attachPerObjectVelocityToGBuffer() {
    // Attach per-object velocity texture as GL_COLOR_ATTACHMENT5 on the GBuffer FBO.
    // Entity/drop fragment shaders write velocity as layout(location=5).
    glNamedFramebufferTexture(m_gBufferFbo, GL_COLOR_ATTACHMENT5, m_perObjectVelocityTex, 0);
    const GLenum drawBuffers[] = {
        kGAlbedoAttachment,
        kGNormalAoAttachment,
        kGVoxelLightAttachment,
        kGMaterialAttachment,
        kGMaterialAuxAttachment,
        GL_COLOR_ATTACHMENT5
    };
    glDrawBuffers(6, drawBuffers);
}

void DeferredRenderTargets::detachPerObjectVelocityFromGBuffer() {
    // Detach per-object velocity from GBuffer FBO and restore 5-target MRT.
    glNamedFramebufferTexture(m_gBufferFbo, GL_COLOR_ATTACHMENT5, 0, 0);
    const GLenum drawBuffers[] = {
        kGAlbedoAttachment,
        kGNormalAoAttachment,
        kGVoxelLightAttachment,
        kGMaterialAttachment,
        kGMaterialAuxAttachment
    };
    glDrawBuffers(kGBufferAttachmentCount, drawBuffers);
}

void DeferredRenderTargets::clearPerObjectVelocity() {
    // Clear per-object velocity to zero. Attaches the texture to the GBuffer FBO
    // as COLOR_ATTACHMENT5, clears it, then detaches. This avoids creating a
    // temporary FBO every frame.
    glNamedFramebufferTexture(m_gBufferFbo, GL_COLOR_ATTACHMENT5, m_perObjectVelocityTex, 0);
    const float zero[] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearNamedFramebufferfv(m_gBufferFbo, GL_COLOR, 5, zero);
    glNamedFramebufferTexture(m_gBufferFbo, GL_COLOR_ATTACHMENT5, 0, 0);
}

void DeferredRenderTargets::bindDefaultLike(const GLint framebuffer, const int width, const int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glViewport(0, 0, std::max(1, width), std::max(1, height));
}

void DeferredRenderTargets::copyFramebufferColorToSceneLighting(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneLightingFbo);
    glBlitFramebuffer(0, 0, std::max(1, width), std::max(1, height),
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneLightingFbo);
}

void DeferredRenderTargets::copyFramebufferColorToSceneResolved(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneResolvedFbo);
    glBlitFramebuffer(0, 0, std::max(1, width), std::max(1, height),
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneResolvedFbo);
}

void DeferredRenderTargets::copyFramebufferColorToTransparentComposite(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_transparentCompositeFbo);
    glBlitFramebuffer(0, 0, std::max(1, width), std::max(1, height),
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copySceneLightingToTransparentComposite() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneLightingFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_transparentCompositeFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copySceneLightingToSceneComposite() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneLightingFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneCompositeFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneCompositeFbo);
}

void DeferredRenderTargets::copySceneCompositeToSceneResolved() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneCompositeFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneResolvedFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneResolvedFbo);
}

void DeferredRenderTargets::copySceneCompositeToTransparentComposite() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneCompositeFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_transparentCompositeFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copySceneResolvedToTransparentComposite() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneResolvedFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_transparentCompositeFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copyTransparentCompositeToSceneComposite() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_transparentCompositeFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneCompositeFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneCompositeFbo);
}

void DeferredRenderTargets::copyTransparentCompositeToSceneResolved() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_transparentCompositeFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneResolvedFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneResolvedFbo);
}

void DeferredRenderTargets::copyDepthToTransparentComposite() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBufferFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_transparentCompositeFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copySceneResolvedToHistory() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneResolvedFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_historySceneFbo[m_currentHistoryIndex]);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_historySceneFbo[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copySceneResolvedToTemporalCurrent() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneResolvedFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_temporalCurrentFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_temporalCurrentFbo);
}

void DeferredRenderTargets::copyDepthToHistory() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBufferFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_historySceneFbo[m_currentHistoryIndex]);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_historySceneFbo[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyReflectionToHistory() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_reflectionFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_historyReflectionFbo[m_currentHistoryIndex]);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_historyReflectionFbo[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyReflectionToTemporalScratch() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_reflectionFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_reflectionTemporalScratchFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_reflectionTemporalScratchFbo);
}

void DeferredRenderTargets::copyCloudToHistory() const {
    if (!m_ready) {
        return;
    }
    const int halfWidth = std::max(1, m_width / 2);
    const int halfHeight = std::max(1, m_height / 2);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_cloudFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_historyCloudFbo[m_currentHistoryIndex]);
    glBlitFramebuffer(0, 0, halfWidth, halfHeight,
                      0, 0, halfWidth, halfHeight,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_historyCloudFbo[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyHistoryCloudToCloud() const {
    if (!m_ready) {
        return;
    }
    const int halfWidth = std::max(1, m_width / 2);
    const int halfHeight = std::max(1, m_height / 2);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_historyCloudFbo[1 - m_currentHistoryIndex]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_cloudFbo);
    glBlitFramebuffer(0, 0, halfWidth, halfHeight,
                      0, 0, halfWidth, halfHeight,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_cloudFbo);
}

void DeferredRenderTargets::copyVolumetricToHistory() const {
    if (!m_ready) {
        return;
    }
    const int halfWidth = std::max(1, m_width / 2);
    const int halfHeight = std::max(1, m_height / 2);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_halfResFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_historyVolumetricFbo[m_currentHistoryIndex]);
    glBlitFramebuffer(0, 0, halfWidth, halfHeight,
                      0, 0, halfWidth, halfHeight,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_historyVolumetricFbo[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyHistoryVolumetricToHalfRes() const {
    if (!m_ready) {
        return;
    }
    const int halfWidth = std::max(1, m_width / 2);
    const int halfHeight = std::max(1, m_height / 2);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_historyVolumetricFbo[1 - m_currentHistoryIndex]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_halfResFbo);
    glBlitFramebuffer(0, 0, halfWidth, halfHeight,
                      0, 0, halfWidth, halfHeight,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_historyVolumetricFbo[m_currentHistoryIndex]);
    glBlitFramebuffer(0, 0, halfWidth, halfHeight,
                      0, 0, halfWidth, halfHeight,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_halfResFbo);
}

void DeferredRenderTargets::copySsaoTemporalToHistory() {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_ssaoTemporalFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ssaoHistoryFbo[m_ssaoHistoryIndex]);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoHistoryFbo[m_ssaoHistoryIndex]);
}

void DeferredRenderTargets::blitSceneLightingTo(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneLightingFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, std::max(1, width), std::max(1, height),
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

void DeferredRenderTargets::blitSceneCompositeTo(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneCompositeFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, std::max(1, width), std::max(1, height),
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

void DeferredRenderTargets::blitSceneResolvedTo(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneResolvedFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, std::max(1, width), std::max(1, height),
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

void DeferredRenderTargets::blitTransparentCompositeTo(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_transparentCompositeFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, std::max(1, width), std::max(1, height),
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

void DeferredRenderTargets::blitDepthTo(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBufferFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, std::max(1, width), std::max(1, height),
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

GLuint DeferredRenderTargets::createTexture2D(const GLenum internalFormat,
                                              const int width,
                                              const int height,
                                              const GLenum format,
                                              const GLenum type,
                                              const GLenum minFilter,
                                              const GLenum magFilter,
                                              const GLenum wrap,
                                              const int levels) {
    GLuint texture = 0;
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

GLuint DeferredRenderTargets::createTexture2DArray(const GLenum internalFormat,
                                                   const int width,
                                                   const int height,
                                                   const int layers,
                                                   const GLenum minFilter,
                                                   const GLenum magFilter,
                                                   const GLenum wrap) {
    GLuint texture = 0;
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

void DeferredRenderTargets::generateMipmaps(const GLuint texture) {
    if (texture != 0) {
        glGenerateTextureMipmap(texture);
    }
}

bool DeferredRenderTargets::checkFramebufferComplete(const GLuint framebuffer, const char* label) {
    const GLenum status = glCheckNamedFramebufferStatus(framebuffer, GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
        return true;
    }
    std::cerr << "DeferredRenderTargets: incomplete " << label << " framebuffer, status=0x"
              << std::hex << status << std::dec << "\n";
    return false;
}

void DeferredRenderTargets::destroyFramebuffers() {
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
    m_velocityTex = 0;
    m_perObjectVelocityTex = 0;
    m_weatherMaskTex = 0;

    const GLuint framebuffers[] = {m_gBufferFbo, m_shadowFbo, m_csmShadowFbo, m_csmShadowTransparentFbo, m_ssaoFbo, m_ssaoFilteredFbo, m_sceneLightingFbo, m_sceneCompositeFbo, m_sceneResolvedFbo, m_temporalCurrentFbo, m_transparentCompositeFbo, m_halfResFbo, m_reflectionFbo, m_reflectionTemporalScratchFbo, m_cloudFbo, m_skyCaptureFbo, m_historySceneFbo[0], m_historySceneFbo[1], m_historyReflectionFbo[0], m_historyReflectionFbo[1], m_historyCloudFbo[0], m_historyCloudFbo[1], m_historyVolumetricFbo[0], m_historyVolumetricFbo[1], m_ssaoHalfResFbo, m_ssaoHalfResFilteredFbo, m_ssaoHistoryFbo[0], m_ssaoHistoryFbo[1], m_ssaoTemporalFbo, m_velocityFbo, m_weatherMaskFbo};
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
    m_velocityFbo = 0;
    m_weatherMaskFbo = 0;
    m_currentHistoryIndex = 0;
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
        glDeleteTextures(1, &m_atmosphereLut3d);
        m_atmosphereLut3d = 0;
    }

    // Final.lut layout: 256 x 128 x 33, RGBA32F
    constexpr int kLutWidth = 256;
    constexpr int kLutHeight = 128;
    constexpr int kLutDepth = 33;
    constexpr size_t kExpectedSize = size_t(kLutWidth) * kLutHeight * kLutDepth * 4 * sizeof(float);

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "AtmosphereLUT: failed to open " << path << "\n";
        return false;
    }

    const auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize != kExpectedSize) {
        std::cerr << "AtmosphereLUT: unexpected file size " << fileSize
                  << " (expected " << kExpectedSize << ")\n";
        return false;
    }

    file.seekg(0, std::ios::beg);
    std::vector<float> data(kLutWidth * kLutHeight * kLutDepth * 4);
    if (!file.read(reinterpret_cast<char*>(data.data()), kExpectedSize)) {
        std::cerr << "AtmosphereLUT: failed to read data\n";
        return false;
    }

    glCreateTextures(GL_TEXTURE_3D, 1, &m_atmosphereLut3d);
    glTextureStorage3D(m_atmosphereLut3d, 1, GL_RGBA32F, kLutWidth, kLutHeight, kLutDepth);
    glTextureSubImage3D(m_atmosphereLut3d, 0, 0, 0, 0,
                        kLutWidth, kLutHeight, kLutDepth,
                        GL_RGBA, GL_FLOAT, data.data());
    glTextureParameteri(m_atmosphereLut3d, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_atmosphereLut3d, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_atmosphereLut3d, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_atmosphereLut3d, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_atmosphereLut3d, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // z=32 layer is the sky output (rendered at runtime); make sure it's writable
    // by NOT marking the texture as immutable after upload. Storage is already allocated.

    std::cout << "AtmosphereLUT: loaded " << path << " (256x128x33 RGBA32F)\n";
    return true;
}
