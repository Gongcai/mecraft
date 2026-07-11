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
void blitTexture(RhiDevice& rhiDevice,
                 const RhiTextureHandle src,
                 const RhiTextureHandle dst) {
    RhiTextureBlit blit;
    blit.src = src;
    blit.dst = dst;

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.blitTexture(blit);
    rhiDevice.submitFrame(commandList);
}
} // namespace

DeferredRenderTargets::~DeferredRenderTargets() {
    shutdown();
}

bool DeferredRenderTargets::init(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        shutdown();
    }
    m_rhiDevice = &rhiDevice;
    return true;
}

void DeferredRenderTargets::shutdown() {
    destroyFramebuffers();
    m_currentHistoryIndex = 0;
    m_ssaoHistoryIndex = 0;
    m_ssgiHistoryIndex = 0;
    m_width = 0;
    m_height = 0;
    m_shadowResolution = 0;
    m_ready = false;
    m_rhiDevice = nullptr;
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

    if (!createGBufferTextures()) {
        shutdown();
        return false;
    }
    if (!createSceneTextures()) {
        shutdown();
        return false;
    }
    if (!createTransparentCompositeTextures()) {
        shutdown();
        return false;
    }

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
    // CSM transparent shadow: depth-all + color for water/transparent occlusion
    // (DerivativeMain shadowtex0/shadowcolor0/shadowcolor1 equivalent)
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
    m_ssaoTex = createTexture2D(GL_R8, m_width, m_height, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);

    // SSAO filtered output (bilateral filter resolves into this)
    m_ssaoFilteredTex = createTexture2D(GL_R8, m_width, m_height, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);

    // Half-res SSAO: raw and filtered at width/2 x height/2
    const int halfW = std::max(1, m_width / 2);
    const int halfH = std::max(1, m_height / 2);
    m_ssaoHalfResTex = createTexture2D(GL_R8, halfW, halfH, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);

    m_ssaoHalfResFilteredTex = createTexture2D(GL_R8, halfW, halfH, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);

    // SSAO temporal history ping-pong (R8, matches SSAO format)
    for (int i = 0; i < 2; ++i) {
        m_ssaoHistoryTex[i] = createTexture2D(GL_R8, m_width, m_height, GL_RED, GL_UNSIGNED_BYTE,
                                              GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    }
    m_ssaoHistoryIndex = 0;

    // SSAO temporal resolve output (R8, same format as SSAO)
    m_ssaoTemporalTex = createTexture2D(GL_R8, m_width, m_height, GL_RED, GL_UNSIGNED_BYTE,
                                        GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);

    m_ssgiHalfResTex = createTexture2D(GL_RGBA16F, halfW, halfH, GL_RGBA, GL_FLOAT,
                                       GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);

    m_ssgiTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);

    for (int i = 0; i < 2; ++i) {
        m_ssgiDenoiseTex[i] = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                              GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    }

    for (int i = 0; i < 2; ++i) {
        m_ssgiHistoryTex[i] = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                              GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        m_ssgiMomentsHistoryTex[i] = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                                     GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    }
    m_ssgiHistoryIndex = 0;

    m_ssgiTemporalTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                        GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    m_ssgiTemporalMomentsTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                               GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);

    // TemporalCurrent: TAA current-frame scratch buffer. Avoids reading
    // history[current] as TAA input (which conflicts with the "history
    // only written once per frame" invariant).
    m_temporalCurrentTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);

    const int halfWidth = std::max(1, m_width / 2);
    const int halfHeight = std::max(1, m_height / 2);
    m_halfResTex = createTexture2D(GL_RGBA16F, halfWidth, halfHeight, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);

    m_reflectionTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);

    // Reflection temporal scratch: holds a copy of the filtered reflection so
    // the temporal pass can read it while writing the blended result.
    m_reflectionTemporalScratchTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);

    m_cloudTex = createTexture2D(GL_RGBA16F, halfWidth, halfHeight, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);

    m_skyCaptureTex = createTexture2D(GL_RGBA16F, kSkyCaptureWidth, kSkyCaptureHeight, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);

    // History scene ping-pong (RGBA16F color + depth)
    for (int i = 0; i < 2; ++i) {
        m_historySceneTex[i] = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                               GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        m_historyDepthTex[i] = createTexture2D(GL_DEPTH_COMPONENT32F, m_width, m_height, GL_DEPTH_COMPONENT, GL_FLOAT,
                                               GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);

        m_historyReflectionTex[i] = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                                    GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);

        m_historyCloudTex[i] = createTexture2D(GL_RGBA16F, halfWidth, halfHeight, GL_RGBA, GL_FLOAT,
                                               GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);

        m_historyVolumetricTex[i] = createTexture2D(GL_RGBA16F, halfWidth, halfHeight, GL_RGBA, GL_FLOAT,
                                                    GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    }
    m_currentHistoryIndex = 0;

    // Velocity buffer (RG16F)
    m_velocityTex = createTexture2D(GL_RG16F, m_width, m_height, GL_RG, GL_FLOAT,
                                    GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);

    // Per-object velocity (RG16F) — screen-space velocity written by entity/drop
    // shaders during GBuffer fill. Temporarily attached to GBuffer FBO as
    // GL_COLOR_ATTACHMENT5 during entity/drop rendering, detached afterward.
    m_perObjectVelocityTex = createTexture2D(GL_RG16F, m_width, m_height, GL_RG, GL_FLOAT,
                                             GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);

    // Weather mask (R8) — additive-blended weather particle alpha.
    // Equivalent to DerivativeMain colortex0.b from gbuffers_weather.
    m_weatherMaskTex = createTexture2D(GL_R8, m_width, m_height, GL_RED, GL_UNSIGNED_BYTE,
                                       GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);

    if (!registerRhiTextures()) {
        shutdown();
        return false;
    }

    // Label GL objects for RenderDoc / KHR_debug inspection
    renderer::debug::labelTexture(m_shadowDepth, "DeferredTargets.ShadowDepth");
    renderer::debug::labelTexture(m_shadowDepthComparison, "DeferredTargets.ShadowDepthComparison");
    renderer::debug::labelTexture(m_shadowColor, "DeferredTargets.ShadowColor");
    renderer::debug::labelTexture(m_shadowNormal, "DeferredTargets.ShadowNormal");
    renderer::debug::labelTexture(m_csmShadowDepth, "DeferredTargets.CSMDepthArray");
    renderer::debug::labelTexture(m_csmShadowDepthComparison, "DeferredTargets.CSMDepthComparison");
    renderer::debug::labelTexture(m_csmShadowDepthAll, "DeferredTargets.CSMDepthAll");
    renderer::debug::labelTexture(m_csmShadowDepthAllComparison, "DeferredTargets.CSMDepthAllComparison");
    renderer::debug::labelTexture(m_csmShadowColor0, "DeferredTargets.CSMColor0");
    renderer::debug::labelTexture(m_csmShadowColor1, "DeferredTargets.CSMColor1");
    renderer::debug::labelTexture(m_ssaoTex, "DeferredTargets.SSAOTex");
    renderer::debug::labelTexture(m_ssaoFilteredTex, "DeferredTargets.SSAOFilteredTex");
    renderer::debug::labelTexture(m_ssaoHalfResTex, "DeferredTargets.SSAOHalfResTex");
    renderer::debug::labelTexture(m_ssaoHalfResFilteredTex, "DeferredTargets.SSAOHalfResFilteredTex");
    renderer::debug::labelTexture(m_ssaoTemporalTex, "DeferredTargets.SSAOTemporalTex");
    for (int i = 0; i < 2; ++i) {
        char texName[48];
        std::snprintf(texName, sizeof(texName), "DeferredTargets.SSAOHistoryTex[%d]", i);
        renderer::debug::labelTexture(m_ssaoHistoryTex[i], texName);
    }
    renderer::debug::labelTexture(m_ssgiTex, "DeferredTargets.SSGITex");
    renderer::debug::labelTexture(m_ssgiHalfResTex, "DeferredTargets.SSGIHalfResTex");
    renderer::debug::labelTexture(m_ssgiTemporalTex, "DeferredTargets.SSGITemporalTex");
    renderer::debug::labelTexture(m_ssgiTemporalMomentsTex, "DeferredTargets.SSGITemporalMomentsTex");
    for (int i = 0; i < 2; ++i) {
        char texName[48];
        std::snprintf(texName, sizeof(texName), "DeferredTargets.SSGIDenoiseTex[%d]", i);
        renderer::debug::labelTexture(m_ssgiDenoiseTex[i], texName);
    }
    for (int i = 0; i < 2; ++i) {
        char texName[48];
        std::snprintf(texName, sizeof(texName), "DeferredTargets.SSGIHistoryTex[%d]", i);
        renderer::debug::labelTexture(m_ssgiHistoryTex[i], texName);
        std::snprintf(texName, sizeof(texName), "DeferredTargets.SSGIMomentsHistoryTex[%d]", i);
        renderer::debug::labelTexture(m_ssgiMomentsHistoryTex[i], texName);
    }
    renderer::debug::labelTexture(m_halfResTex, "DeferredTargets.HalfResTex");
    renderer::debug::labelTexture(m_reflectionTex, "DeferredTargets.ReflectionTex");
    renderer::debug::labelTexture(m_reflectionTemporalScratchTex, "DeferredTargets.ReflectionTemporalScratchTex");
    renderer::debug::labelTexture(m_cloudTex, "DeferredTargets.CloudTex");
    renderer::debug::labelTexture(m_skyCaptureTex, "DeferredTargets.SkyCaptureTex");
    for (int i = 0; i < 2; ++i) {
        char texName[48], depthName[48];
        std::snprintf(texName, sizeof(texName), "DeferredTargets.HistorySceneTex[%d]", i);
        std::snprintf(depthName, sizeof(depthName), "DeferredTargets.HistoryDepthTex[%d]", i);
        renderer::debug::labelTexture(m_historySceneTex[i], texName);
        renderer::debug::labelTexture(m_historyDepthTex[i], depthName);
    }
    for (int i = 0; i < 2; ++i) {
        char texName[48];
        std::snprintf(texName, sizeof(texName), "DeferredTargets.HistoryReflectionTex[%d]", i);
        renderer::debug::labelTexture(m_historyReflectionTex[i], texName);
    }
    for (int i = 0; i < 2; ++i) {
        char texName[48];
        std::snprintf(texName, sizeof(texName), "DeferredTargets.HistoryCloudTex[%d]", i);
        renderer::debug::labelTexture(m_historyCloudTex[i], texName);
    }
    for (int i = 0; i < 2; ++i) {
        char texName[48];
        std::snprintf(texName, sizeof(texName), "DeferredTargets.HistoryVolumetricTex[%d]", i);
        renderer::debug::labelTexture(m_historyVolumetricTex[i], texName);
    }
    renderer::debug::labelTexture(m_temporalCurrentTex, "DeferredTargets.TemporalCurrentTex");
    renderer::debug::labelTexture(m_velocityTex, "DeferredTargets.VelocityTex");
    renderer::debug::labelTexture(m_perObjectVelocityTex, "DeferredTargets.PerObjectVelocity");
    renderer::debug::labelTexture(m_weatherMaskTex, "DeferredTargets.WeatherMaskTex");
    renderer::debug::labelTexture(m_atmosphereLut3d, "DeferredTargets.AtmosphereLUT");
    m_ready = true;
    return true;
}

void DeferredRenderTargets::copyTextureColorToSceneLighting(RhiDevice& rhiDevice, const RhiTextureHandle source) const {
    if (!m_ready || !source.isValid()) {
        return;
    }
    blitTexture(rhiDevice, source, m_sceneLightingHandle);
}

void DeferredRenderTargets::copySceneLightingToTransparentComposite(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_sceneLightingHandle, m_transparentCompositeHandle);
}

void DeferredRenderTargets::copySceneLightingToSceneComposite(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_sceneLightingHandle, m_sceneCompositeHandle);
}

void DeferredRenderTargets::copySceneCompositeToSceneResolved(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_sceneCompositeHandle, m_sceneResolvedHandle);
}

void DeferredRenderTargets::copySceneCompositeToTransparentComposite(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_sceneCompositeHandle, m_transparentCompositeHandle);
}

void DeferredRenderTargets::copySceneResolvedToTransparentComposite(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_sceneResolvedHandle, m_transparentCompositeHandle);
}

void DeferredRenderTargets::copyTransparentCompositeToSceneComposite(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_transparentCompositeHandle, m_sceneCompositeHandle);
}

void DeferredRenderTargets::copyTransparentCompositeToSceneResolved(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_transparentCompositeHandle, m_sceneResolvedHandle);
}

void DeferredRenderTargets::copyDepthToTransparentComposite(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_gDepthHandle, m_transparentCompositeDepthHandle);
}

void DeferredRenderTargets::copySceneResolvedToHistory(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_sceneResolvedHandle, m_historySceneHandle[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copySceneResolvedToTemporalCurrent(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_sceneResolvedHandle, m_temporalCurrentHandle);
}

void DeferredRenderTargets::copyDepthToHistory(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_gDepthHandle, m_historyDepthHandle[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyReflectionToHistory(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_reflectionHandle, m_historyReflectionHandle[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyReflectionToTemporalScratch(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_reflectionHandle, m_reflectionTemporalScratchHandle);
}

void DeferredRenderTargets::copyCloudToHistory(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_cloudHandle, m_historyCloudHandle[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyHistoryCloudToCloud(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_historyCloudHandle[1 - m_currentHistoryIndex], m_cloudHandle);
}

void DeferredRenderTargets::copyVolumetricToHistory(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_halfResHandle, m_historyVolumetricHandle[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyHistoryVolumetricToHalfRes(RhiDevice& rhiDevice) const {
    if (!m_ready) {
        return;
    }
    const RhiTextureHandle previous = m_historyVolumetricHandle[1 - m_currentHistoryIndex];
    blitTexture(rhiDevice, previous, m_halfResHandle);
    blitTexture(rhiDevice, previous, m_historyVolumetricHandle[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copySceneResolvedToTexture(RhiDevice& rhiDevice, const RhiTextureHandle destination) const {
    if (!m_ready || !destination.isValid()) {
        return;
    }
    blitTexture(rhiDevice, m_sceneResolvedHandle, destination);
}

void DeferredRenderTargets::copyDepthToTexture(RhiDevice& rhiDevice, const RhiTextureHandle destination) const {
    if (!m_ready || !destination.isValid()) {
        return;
    }
    blitTexture(rhiDevice, m_gDepthHandle, destination);
}

void DeferredRenderTargets::copyTransparentCompositeToTexture(RhiDevice& rhiDevice, const RhiTextureHandle destination) const {
    if (!m_ready || !destination.isValid()) {
        return;
    }
    blitTexture(rhiDevice, m_transparentCompositeHandle, destination);
}

void DeferredRenderTargets::copySsaoTemporalToHistory(RhiDevice& rhiDevice) {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_ssaoTemporalHandle, m_ssaoHistoryHandle[m_ssaoHistoryIndex]);
}

void DeferredRenderTargets::copySsgiTemporalToHistory(RhiDevice& rhiDevice) {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_ssgiTemporalHandle, m_ssgiHistoryHandle[m_ssgiHistoryIndex]);
    blitTexture(rhiDevice, m_ssgiTemporalMomentsHandle, m_ssgiMomentsHistoryHandle[m_ssgiHistoryIndex]);
}

RhiTextureHandle DeferredRenderTargets::ssgiDenoiseTextureHandle(const int slot) const {
    assert(slot >= 0 && slot < 2);
    return m_ssgiDenoiseHandle[slot];
}

RhiTextureViewHandle DeferredRenderTargets::ssgiDenoiseTextureViewHandle(const int slot) const {
    assert(slot >= 0 && slot < 2);
    return m_ssgiDenoiseView[slot];
}

void DeferredRenderTargets::copySsgiDenoiseToSsgi(RhiDevice& rhiDevice, const int slot) {
    assert(slot >= 0 && slot < 2);
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_ssgiDenoiseHandle[slot], m_ssgiHandle);
}

void DeferredRenderTargets::copySsgiTemporalToSsgi(RhiDevice& rhiDevice) {
    if (!m_ready) {
        return;
    }
    blitTexture(rhiDevice, m_ssgiTemporalHandle, m_ssgiHandle);
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

bool DeferredRenderTargets::createGBufferTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    const auto createTexture = [this](const char* debugName,
                                      const RhiTextureFormat format,
                                      const RhiTextureUsageFlags usage,
                                      RhiTextureHandle& handle) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.dimension = RhiTextureDimension::Texture2D;
        desc.format = format;
        desc.width = static_cast<uint32_t>(m_width);
        desc.height = static_cast<uint32_t>(m_height);
        desc.depthOrLayers = 1u;
        desc.mipLevels = 1u;
        desc.sampleCount = 1u;
        desc.usage = usage;
        handle = m_rhiDevice->createTexture(desc, nullptr);
        return handle.isValid();
    };

    const RhiTextureUsageFlags colorUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::ColorAttachment) |
        rhiFlag(RhiTextureUsage::TransferSrc) |
        rhiFlag(RhiTextureUsage::TransferDst);
    const RhiTextureUsageFlags depthUsage =
        rhiFlag(RhiTextureUsage::Sampled) |
        rhiFlag(RhiTextureUsage::DepthStencilAttachment) |
        rhiFlag(RhiTextureUsage::TransferSrc) |
        rhiFlag(RhiTextureUsage::TransferDst);

    if (!createTexture("DeferredTargets.GBufferAlbedo", RhiTextureFormat::Rgba8Unorm, colorUsage, m_gAlbedoHandle) ||
        !createTexture("DeferredTargets.GBufferNormalAo", RhiTextureFormat::Rgba16Float, colorUsage, m_gNormalAoHandle) ||
        !createTexture("DeferredTargets.GBufferVoxelLight", RhiTextureFormat::Rg8Unorm, colorUsage, m_gVoxelLightHandle) ||
        !createTexture("DeferredTargets.GBufferMaterial", RhiTextureFormat::Rgba8Unorm, colorUsage, m_gMaterialHandle) ||
        !createTexture("DeferredTargets.GBufferMaterialAux", RhiTextureFormat::Rgba8Unorm, colorUsage, m_gMaterialAuxHandle) ||
        !createTexture("DeferredTargets.GBufferDepth", RhiTextureFormat::Depth32Float, depthUsage, m_gDepthHandle)) {
        destroyGBufferTextures();
        return false;
    }
    return true;
}

void DeferredRenderTargets::destroyGBufferTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }

    RhiTextureHandle* textures[] = {
        &m_gAlbedoHandle,
        &m_gNormalAoHandle,
        &m_gVoxelLightHandle,
        &m_gMaterialHandle,
        &m_gMaterialAuxHandle,
        &m_gDepthHandle
    };
    for (RhiTextureHandle* texture : textures) {
        if (texture->isValid()) {
            m_rhiDevice->destroyTexture(*texture);
            *texture = {};
        }
    }
}

bool DeferredRenderTargets::createSceneTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    const auto createTexture = [this](const char* debugName, RhiTextureHandle& handle) {
        RhiTextureDesc desc;
        desc.debugName = debugName;
        desc.dimension = RhiTextureDimension::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.width = static_cast<uint32_t>(m_width);
        desc.height = static_cast<uint32_t>(m_height);
        desc.depthOrLayers = 1u;
        desc.mipLevels = 1u;
        desc.sampleCount = 1u;
        desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                     rhiFlag(RhiTextureUsage::ColorAttachment) |
                     rhiFlag(RhiTextureUsage::TransferSrc) |
                     rhiFlag(RhiTextureUsage::TransferDst);
        handle = m_rhiDevice->createTexture(desc, nullptr);
        return handle.isValid();
    };

    if (!createTexture("DeferredTargets.SceneLighting", m_sceneLightingHandle) ||
        !createTexture("DeferredTargets.SceneComposite", m_sceneCompositeHandle) ||
        !createTexture("DeferredTargets.SceneResolved", m_sceneResolvedHandle)) {
        destroySceneTextures();
        return false;
    }
    return true;
}

void DeferredRenderTargets::destroySceneTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }

    RhiTextureHandle* textures[] = {
        &m_sceneLightingHandle,
        &m_sceneCompositeHandle,
        &m_sceneResolvedHandle
    };
    for (RhiTextureHandle* texture : textures) {
        if (texture->isValid()) {
            m_rhiDevice->destroyTexture(*texture);
            *texture = {};
        }
    }
}

bool DeferredRenderTargets::createTransparentCompositeTextures() {
    if (m_rhiDevice == nullptr) {
        return false;
    }

    RhiTextureDesc desc;
    desc.dimension = RhiTextureDimension::Texture2D;
    desc.width = static_cast<uint32_t>(m_width);
    desc.height = static_cast<uint32_t>(m_height);
    desc.depthOrLayers = 1u;
    desc.mipLevels = 1u;
    desc.sampleCount = 1u;

    desc.debugName = "DeferredTargets.TransparentComposite";
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                 rhiFlag(RhiTextureUsage::ColorAttachment) |
                 rhiFlag(RhiTextureUsage::TransferSrc) |
                 rhiFlag(RhiTextureUsage::TransferDst);
    m_transparentCompositeHandle = m_rhiDevice->createTexture(desc, nullptr);
    if (!m_transparentCompositeHandle.isValid()) {
        return false;
    }

    // A separate depth attachment prevents feedback while the G-buffer depth is sampled.
    desc.debugName = "DeferredTargets.TransparentCompositeDepth";
    desc.format = RhiTextureFormat::Depth32Float;
    desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                 rhiFlag(RhiTextureUsage::DepthStencilAttachment) |
                 rhiFlag(RhiTextureUsage::TransferSrc) |
                 rhiFlag(RhiTextureUsage::TransferDst);
    m_transparentCompositeDepthHandle = m_rhiDevice->createTexture(desc, nullptr);
    if (!m_transparentCompositeDepthHandle.isValid()) {
        destroyTransparentCompositeTextures();
        return false;
    }
    return true;
}

void DeferredRenderTargets::destroyTransparentCompositeTextures() {
    if (m_rhiDevice == nullptr) {
        return;
    }

    if (m_transparentCompositeHandle.isValid()) {
        m_rhiDevice->destroyTexture(m_transparentCompositeHandle);
        m_transparentCompositeHandle = {};
    }
    if (m_transparentCompositeDepthHandle.isValid()) {
        m_rhiDevice->destroyTexture(m_transparentCompositeDepthHandle);
        m_transparentCompositeDepthHandle = {};
    }
}

bool DeferredRenderTargets::registerRhiTextures() {
    const uint32_t halfWidth = static_cast<uint32_t>(std::max(1, m_width / 2));
    const uint32_t halfHeight = static_cast<uint32_t>(std::max(1, m_height / 2));
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

bool DeferredRenderTargets::ensureVolumetricFogTextureViews(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_shadowDepthView.isValid() && m_shadowColorView.isValid() &&
        m_atmosphereLutView.isValid() && m_csmShadowDepthArrayView.isValid() &&
        m_csmShadowDepthComparisonArrayView.isValid() &&
        m_csmShadowDepthAllArrayView.isValid() &&
        m_csmShadowDepthAllComparisonArrayView.isValid() &&
        m_csmShadowColor0ArrayView.isValid() && m_csmShadowColor1ArrayView.isValid()) {
        return true;
    }
    if (!m_shadowDepthHandle.isValid() || !m_shadowColorHandle.isValid() ||
        !m_atmosphereLutHandle.isValid() || !m_csmShadowDepthHandle.isValid() ||
        !m_csmShadowDepthAllHandle.isValid() || !m_csmShadowColor0Handle.isValid() ||
        !m_csmShadowColor1Handle.isValid()) {
        return false;
    }

    auto destroyCreatedViews = [&]() {
        RhiTextureViewHandle* views[] = {
            &m_shadowDepthView,
            &m_shadowColorView,
            &m_atmosphereLutView,
            &m_csmShadowDepthArrayView,
            &m_csmShadowDepthComparisonArrayView,
            &m_csmShadowDepthAllArrayView,
            &m_csmShadowDepthAllComparisonArrayView,
            &m_csmShadowColor0ArrayView,
            &m_csmShadowColor1ArrayView
        };
        for (RhiTextureViewHandle* view : views) {
            if (view->isValid()) {
                rhiDevice.destroyTextureView(*view);
                *view = {};
            }
        }
    };

    RhiTextureViewDesc viewDesc;
    viewDesc.viewType = RhiTextureViewType::Texture2D;
    viewDesc.baseMip = 0u;
    viewDesc.mipCount = 1u;
    viewDesc.baseLayer = 0u;
    viewDesc.layerCount = 1u;

    viewDesc.texture = m_shadowDepthHandle;
    viewDesc.format = RhiTextureFormat::Depth32Float;
    m_shadowDepthView = rhiDevice.createTextureView(viewDesc);

    viewDesc.texture = m_shadowColorHandle;
    viewDesc.format = RhiTextureFormat::Rgba8Unorm;
    m_shadowColorView = rhiDevice.createTextureView(viewDesc);

    viewDesc.texture = m_atmosphereLutHandle;
    viewDesc.viewType = RhiTextureViewType::Texture3D;
    viewDesc.format = RhiTextureFormat::Rgba32Float;
    viewDesc.layerCount = static_cast<uint32_t>(kAtmosphereLutDepth);
    m_atmosphereLutView = rhiDevice.createTextureView(viewDesc);

    viewDesc.viewType = RhiTextureViewType::Texture2DArray;
    viewDesc.format = RhiTextureFormat::Depth24;
    viewDesc.layerCount = static_cast<uint32_t>(kShadowCascadeCount);
    viewDesc.texture = m_csmShadowDepthHandle;
    m_csmShadowDepthArrayView = rhiDevice.createTextureView(viewDesc);
    m_csmShadowDepthComparisonArrayView = rhiDevice.createTextureView(viewDesc);

    viewDesc.texture = m_csmShadowDepthAllHandle;
    m_csmShadowDepthAllArrayView = rhiDevice.createTextureView(viewDesc);
    m_csmShadowDepthAllComparisonArrayView = rhiDevice.createTextureView(viewDesc);

    viewDesc.texture = m_csmShadowColor0Handle;
    viewDesc.format = RhiTextureFormat::Rgba8Unorm;
    m_csmShadowColor0ArrayView = rhiDevice.createTextureView(viewDesc);

    viewDesc.texture = m_csmShadowColor1Handle;
    viewDesc.format = RhiTextureFormat::Rgba16Float;
    m_csmShadowColor1ArrayView = rhiDevice.createTextureView(viewDesc);

    if (!m_shadowDepthView.isValid() || !m_shadowColorView.isValid() ||
        !m_atmosphereLutView.isValid() || !m_csmShadowDepthArrayView.isValid() ||
        !m_csmShadowDepthComparisonArrayView.isValid() ||
        !m_csmShadowDepthAllArrayView.isValid() ||
        !m_csmShadowDepthAllComparisonArrayView.isValid() ||
        !m_csmShadowColor0ArrayView.isValid() || !m_csmShadowColor1ArrayView.isValid()) {
        destroyCreatedViews();
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

bool DeferredRenderTargets::ensureSsaoHistoryTextureViews(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssaoHistoryView[0].isValid() && m_ssaoHistoryView[1].isValid()) {
        return true;
    }
    if (!m_ssaoHistoryHandle[0].isValid() || !m_ssaoHistoryHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_ssaoHistoryView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_ssaoHistoryHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::R8Unorm;
        desc.baseMip = 0;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;

        m_ssaoHistoryView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_ssaoHistoryView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_ssaoHistoryView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
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

bool DeferredRenderTargets::ensureSsgiHistoryTextureViews(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_ssgiHistoryView[0].isValid() && m_ssgiHistoryView[1].isValid() &&
        m_ssgiMomentsHistoryView[0].isValid() && m_ssgiMomentsHistoryView[1].isValid()) {
        return true;
    }
    if (!m_ssgiHistoryHandle[0].isValid() || !m_ssgiHistoryHandle[1].isValid() ||
        !m_ssgiMomentsHistoryHandle[0].isValid() || !m_ssgiMomentsHistoryHandle[1].isValid()) {
        return false;
    }

    auto destroyCreatedViews = [&]() {
        for (RhiTextureViewHandle& view : m_ssgiHistoryView) {
            if (view.isValid()) {
                rhiDevice.destroyTextureView(view);
            }
            view = {};
        }
        for (RhiTextureViewHandle& view : m_ssgiMomentsHistoryView) {
            if (view.isValid()) {
                rhiDevice.destroyTextureView(view);
            }
            view = {};
        }
    };

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        RhiTextureViewDesc desc;
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.baseMip = 0u;
        desc.mipCount = 1u;
        desc.baseLayer = 0u;
        desc.layerCount = 1u;

        if (!m_ssgiHistoryView[historyIndex].isValid()) {
            desc.texture = m_ssgiHistoryHandle[historyIndex];
            m_ssgiHistoryView[historyIndex] = rhiDevice.createTextureView(desc);
        }
        if (!m_ssgiMomentsHistoryView[historyIndex].isValid()) {
            desc.texture = m_ssgiMomentsHistoryHandle[historyIndex];
            m_ssgiMomentsHistoryView[historyIndex] = rhiDevice.createTextureView(desc);
        }
        if (!m_ssgiHistoryView[historyIndex].isValid() ||
            !m_ssgiMomentsHistoryView[historyIndex].isValid()) {
            destroyCreatedViews();
            return false;
        }
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

bool DeferredRenderTargets::ensureReflectionTemporalScratchTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_reflectionTemporalScratchView.isValid()) {
        return true;
    }

    if (!m_reflectionTemporalScratchHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_reflectionTemporalScratchHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_reflectionTemporalScratchView = rhiDevice.createTextureView(desc);
    if (!m_reflectionTemporalScratchView.isValid()) {
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

bool DeferredRenderTargets::ensureHistorySceneTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }

    const int historyIndex = m_currentHistoryIndex;
    if (m_historySceneView[historyIndex].isValid()) {
        return true;
    }

    if (!m_historySceneHandle[historyIndex].isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_historySceneHandle[historyIndex];
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_historySceneView[historyIndex] = rhiDevice.createTextureView(desc);
    if (!m_historySceneView[historyIndex].isValid()) {
        return false;
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHistorySceneTextureViews(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_historySceneView[0].isValid() && m_historySceneView[1].isValid()) {
        return true;
    }
    if (!m_historySceneHandle[0].isValid() || !m_historySceneHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_historySceneView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_historySceneHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.baseMip = 0;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;

        m_historySceneView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_historySceneView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_historySceneView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHistoryDepthTextureViews(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_historyDepthView[0].isValid() && m_historyDepthView[1].isValid()) {
        return true;
    }
    if (!m_historyDepthHandle[0].isValid() || !m_historyDepthHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_historyDepthView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_historyDepthHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Depth32Float;
        desc.baseMip = 0;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;

        m_historyDepthView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_historyDepthView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_historyDepthView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHistoryReflectionTextureViews(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_historyReflectionView[0].isValid() && m_historyReflectionView[1].isValid()) {
        return true;
    }
    if (!m_historyReflectionHandle[0].isValid() || !m_historyReflectionHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_historyReflectionView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_historyReflectionHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.baseMip = 0;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;

        m_historyReflectionView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_historyReflectionView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_historyReflectionView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
    }

    m_rhiViewDevice = &rhiDevice;
    return true;
}

bool DeferredRenderTargets::ensureHistoryCloudTextureViews(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_historyCloudView[0].isValid() && m_historyCloudView[1].isValid()) {
        return true;
    }
    if (!m_historyCloudHandle[0].isValid() || !m_historyCloudHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_historyCloudView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_historyCloudHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.baseMip = 0u;
        desc.mipCount = 1u;
        desc.baseLayer = 0u;
        desc.layerCount = 1u;

        m_historyCloudView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_historyCloudView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_historyCloudView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
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

bool DeferredRenderTargets::ensureTemporalCurrentTextureView(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_temporalCurrentView.isValid()) {
        return true;
    }

    if (!m_temporalCurrentHandle.isValid()) {
        return false;
    }

    RhiTextureViewDesc desc;
    desc.texture = m_temporalCurrentHandle;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;

    m_temporalCurrentView = rhiDevice.createTextureView(desc);
    if (!m_temporalCurrentView.isValid()) {
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

bool DeferredRenderTargets::ensureHistoryVolumetricTextureViews(RhiDevice& rhiDevice) {
    if (m_rhiViewDevice != nullptr && m_rhiViewDevice != &rhiDevice) {
        destroyRhiTextureViews();
    }
    if (m_historyVolumetricView[0].isValid() && m_historyVolumetricView[1].isValid()) {
        return true;
    }
    if (!m_historyVolumetricHandle[0].isValid() || !m_historyVolumetricHandle[1].isValid()) {
        return false;
    }

    for (int historyIndex = 0; historyIndex < 2; ++historyIndex) {
        if (m_historyVolumetricView[historyIndex].isValid()) {
            continue;
        }

        RhiTextureViewDesc desc;
        desc.texture = m_historyVolumetricHandle[historyIndex];
        desc.viewType = RhiTextureViewType::Texture2D;
        desc.format = RhiTextureFormat::Rgba16Float;
        desc.baseMip = 0;
        desc.mipCount = 1;
        desc.baseLayer = 0;
        desc.layerCount = 1;

        m_historyVolumetricView[historyIndex] = rhiDevice.createTextureView(desc);
        if (!m_historyVolumetricView[historyIndex].isValid()) {
            for (RhiTextureViewHandle& view : m_historyVolumetricView) {
                if (view.isValid()) {
                    rhiDevice.destroyTextureView(view);
                }
                view = {};
            }
            return false;
        }
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
    RhiTextureViewHandle* volumetricFogViews[] = {
        &m_shadowDepthView,
        &m_shadowColorView,
        &m_atmosphereLutView,
        &m_csmShadowDepthArrayView,
        &m_csmShadowDepthComparisonArrayView,
        &m_csmShadowDepthAllArrayView,
        &m_csmShadowDepthAllComparisonArrayView,
        &m_csmShadowColor0ArrayView,
        &m_csmShadowColor1ArrayView
    };
    for (RhiTextureViewHandle* view : volumetricFogViews) {
        if (m_rhiViewDevice != nullptr && view->isValid()) {
            m_rhiViewDevice->destroyTextureView(*view);
        }
        *view = {};
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
    for (RhiTextureViewHandle& view : m_ssaoHistoryView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
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
    for (RhiTextureViewHandle& view : m_ssgiHistoryView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_ssgiMomentsHistoryView) {
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
    if (m_rhiViewDevice != nullptr && m_reflectionTemporalScratchView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_reflectionTemporalScratchView);
    }
    if (m_rhiViewDevice != nullptr && m_cloudView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_cloudView);
    }
    if (m_rhiViewDevice != nullptr && m_skyCaptureView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_skyCaptureView);
    }
    for (RhiTextureViewHandle& view : m_historySceneView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_historyDepthView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_historyVolumetricView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_historyReflectionView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    for (RhiTextureViewHandle& view : m_historyCloudView) {
        if (m_rhiViewDevice != nullptr && view.isValid()) {
            m_rhiViewDevice->destroyTextureView(view);
        }
        view = {};
    }
    if (m_rhiViewDevice != nullptr && m_temporalCurrentView.isValid()) {
        m_rhiViewDevice->destroyTextureView(m_temporalCurrentView);
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
    m_shadowDepthView = {};
    m_shadowColorView = {};
    m_atmosphereLutView = {};
    m_csmShadowDepthArrayView = {};
    m_csmShadowDepthComparisonArrayView = {};
    m_csmShadowDepthAllArrayView = {};
    m_csmShadowDepthAllComparisonArrayView = {};
    m_csmShadowColor0ArrayView = {};
    m_csmShadowColor1ArrayView = {};
    m_velocityView = {};
    m_perObjectVelocityView = {};
    m_ssaoFilteredView = {};
    m_ssaoHalfResView = {};
    m_ssaoHalfResFilteredView = {};
    m_ssaoTemporalView = {};
    m_ssaoHistoryView[0] = {};
    m_ssaoHistoryView[1] = {};
    m_sceneLightingView = {};
    m_ssgiView = {};
    m_ssgiHalfResView = {};
    m_ssgiTemporalView = {};
    m_ssgiTemporalMomentsView = {};
    m_ssgiHistoryView[0] = {};
    m_ssgiHistoryView[1] = {};
    m_ssgiMomentsHistoryView[0] = {};
    m_ssgiMomentsHistoryView[1] = {};
    m_sceneCompositeView = {};
    m_sceneResolvedView = {};
    m_transparentCompositeView = {};
    m_transparentCompositeDepthView = {};
    m_halfResView = {};
    m_reflectionView = {};
    m_reflectionTemporalScratchView = {};
    m_cloudView = {};
    m_skyCaptureView = {};
    m_historySceneView[0] = {};
    m_historySceneView[1] = {};
    m_historyDepthView[0] = {};
    m_historyDepthView[1] = {};
    m_historyReflectionView[0] = {};
    m_historyReflectionView[1] = {};
    m_historyCloudView[0] = {};
    m_historyCloudView[1] = {};
    m_temporalCurrentView = {};
    m_weatherMaskView = {};
    m_rhiViewDevice = nullptr;
}

void DeferredRenderTargets::unregisterRhiTextures() {
    destroyRhiTextureViews();
    destroyGBufferTextures();
    destroySceneTextures();
    destroyTransparentCompositeTextures();
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
        m_temporalCurrentTex,
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
    m_temporalCurrentTex = 0;
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

    m_currentHistoryIndex = 0;
    m_ssaoHistoryIndex = 0;
    m_ssgiHistoryIndex = 0;
    m_ready = false;
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
