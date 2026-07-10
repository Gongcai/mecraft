#include "ShadowPass.h"
#include "../debug/RenderDebugLabels.h"
#include "../debug/RenderDebugService.h"
#include "../targets/DeferredRenderTargets.h"
#include "../shadow/ShadowRenderer.h"
#include "../shadow/ShadowMatrices.h"
#include "../shadow/ShadowCasterCuller.h"
#include "../core/RenderScene.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../mesh/TerrainRhiPipelineSet.h"
#include "../mesh/TerrainRenderer.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../core/Shader.h"
#include "../renderers/GameplaySkyRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/IWorldView.h"

#include <glad/glad.h>
#include "../renderers/BlockEntityRenderer.h"
#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../renderers/FallingBlockRenderer.h"
#include "../../world/DropSystem.h"
#include "../../ecs/GameplayRegistry.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "renderer/core/FrameContext.h"

namespace {
/// Convert FrameContext::SkyColorsData to GameplaySkyRenderer::SkyColors
/// for compatibility with ShadowRenderer::computeLightDirection().
GameplaySkyRenderer::SkyColors toLegacySkyColors(const SkyColorsData& src) {
    GameplaySkyRenderer::SkyColors dst;
    dst.sunDirection = src.sunDirection;
    dst.moonDirection = src.moonDirection;
    dst.sunLightColor = src.sunLightColor;
    dst.moonLightColor = src.moonLightColor;
    dst.skyAmbientColor = src.skyAmbientColor;
    dst.sunVisibility = src.sunVisibility;
    dst.moonVisibility = src.moonVisibility;
    return dst;
}

} // namespace

static constexpr int SHADOW_CASCADE_COUNT = shadow::ShadowRenderer::CASCADE_COUNT;
using ShadowCascadeData = shadow::ShadowRenderer::Cascade;

void ShadowPass::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_entityShadowShader = resourceMgr.getShader("entity_shadow");
}

void ShadowPass::shutdown() {
    m_entityShadowShader = nullptr;
    m_resourceMgr = nullptr;
}

void ShadowPass::renderShadowEntities(const IWorldView& worldView, const glm::mat4& shadowViewProj,
                                      const glm::vec3& cameraPos, float splitNear, float splitFar) {
    // Render humanoid/mob entities into the current shadow cascade layer.
    // Shadow FBO layer is already bound by the caller (execute).
    if (m_humanoidRenderer == nullptr || m_gameplayRegistry == nullptr ||
        m_entityShadowShader == nullptr) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_entityShadowShader->use();
    m_entityShadowShader->setInt("uTexture", 0);

    m_humanoidRenderer->renderToShadowMap(worldView, *m_gameplayRegistry,
                                          shadowViewProj, cameraPos, splitNear, splitFar,
                                          HumanoidRenderer::kRenderAll);

    glBindVertexArray(0);
}

void ShadowPass::renderShadowBlockEntities(const IWorldView& worldView,
                                           const glm::mat4& shadowViewProj,
                                           const glm::vec3& cameraPos,
                                           const float splitNear,
                                           const float splitFar) {
    if (m_blockEntityRenderer == nullptr || m_entityShadowShader == nullptr) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_entityShadowShader->use();
    m_entityShadowShader->setInt("uTexture", 0);

    m_blockEntityRenderer->renderToShadowMap(worldView, shadowViewProj, cameraPos, splitNear, splitFar);

    glBindVertexArray(0);
}

void ShadowPass::renderShadowDrops(const IWorldView& worldView, const glm::mat4& shadowViewProj,
                                    const glm::mat4& shadowView, const glm::mat4& shadowProjection,
                                    float animationTime, float shaderTime) {
    // Render dropped items/blocks into the current shadow cascade layer.
    // Shadow FBO layer is already bound by the caller (execute).
    if (m_dropRenderer == nullptr || m_dropSystem == nullptr) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    // Cross-shaped block drops emit single-sided quads — disable culling
    // so they cast shadows from both sides.
    glDisable(GL_CULL_FACE);

    m_dropRenderer->renderToShadowMap(worldView, *m_dropSystem, shadowViewProj,
                                       shadowView, shadowProjection, animationTime, shaderTime);

    glBindVertexArray(0);
}

void ShadowPass::renderShadowFallingBlocks(const glm::mat4& shadowViewProj,
                                            const glm::mat4& shadowView, const glm::mat4& shadowProjection,
                                            float animationTime, float shaderTime) {
    // Render falling-block entities into the current shadow cascade layer.
    // Shadow FBO layer is already bound by the caller (execute).
    if (m_fallingBlockRenderer == nullptr || m_gameplayRegistry == nullptr) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    m_fallingBlockRenderer->renderToShadowMap(*m_gameplayRegistry, shadowViewProj,
                                               shadowView, shadowProjection, animationTime, shaderTime);

    glBindVertexArray(0);
}

ShadowPass::ShadowPassOutput ShadowPass::execute(
    const FrameContext& ctx, const RenderSettings& settings,
    DeferredRenderTargets& targets, const IWorldView& worldView,
    const std::vector<DrawBatchEntry>& preservedTransparentBatch,
    const TransparentPassPlan& preservedTransparentPlan) {
    ShadowPassOutput output;
    output.transparentBatch = preservedTransparentBatch;
    output.transparentPlan = preservedTransparentPlan;

    if (m_shadowRenderer == nullptr ||
        m_terrainRenderer == nullptr ||
        m_worldRenderBuffer == nullptr ||
        m_resourceMgr == nullptr ||
        ctx.shared == nullptr ||
        ctx.shared->rhiDevice == nullptr ||
        ctx.shared->terrainRhiPipelines == nullptr) {
        return output;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;

    // Update shadow cascades via ShadowRenderer.
    m_shadowRenderer->computeLightDirection(toLegacySkyColors(ctx.skyColors));

    // Build camera basis from FrameContext view matrix
    const glm::mat4& view = ctx.camera.view;
    shadow::ShadowMatrices::CameraBasis basis;
    basis.position = ctx.camera.position;
    basis.forward = -glm::vec3(view[0][2], view[1][2], view[2][2]);
    basis.right = glm::vec3(view[0][0], view[1][0], view[2][0]);
    basis.up = glm::vec3(view[0][1], view[1][1], view[2][1]);
    basis.nearPlane = ctx.camera.nearPlane;
    basis.verticalFovDegrees = ctx.camera.fovDegrees;
    basis.aspectRatio = static_cast<float>(std::max(1, targets.width())) /
                        static_cast<float>(std::max(1, targets.height()));

    shadow::ShadowMatrices::Settings smSettings;
    smSettings.shadowDistance = settings.shadow.distance;
    smSettings.shadowResolution = settings.shadow.resolution;
    m_shadowRenderer->updateFromBasis(basis, smSettings);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    const float shadowDist = std::max(64.0f, settings.shadow.distance);
    int visibleTotal = 0;
    int culledTotal = 0;
    float maxCasterDistance = 0.0f;

    // Transparent caster data is most visible in the near cascades. Far cascades
    // still update DepthAll from opaque depth and clear color layers so receivers
    // consume a coherent transparent-shadow contract without far water/glass draws.
    constexpr int kTransparentShadowCasterCascadeCount = 2;
    // Cutout casters remain important in far cascades at low sun angles because
    // tree and vegetation silhouettes project across long receiver spans.
    constexpr int kCutoutShadowCasterCascadeCount = SHADOW_CASCADE_COUNT;
    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        if (!targets.ensureCsmShadowDepthTextureView(rhiDevice, cascade) ||
            !targets.ensureCsmShadowTransparentTextureViews(rhiDevice, cascade)) {
            return output;
        }
    }

    RenderDebugService* debugService = ctx.debugService;
    const bool shadowStatsActive = debugService != nullptr &&
        debugService->beginShadowFrame(SHADOW_CASCADE_COUNT, targets.shadowResolution());
    std::array<ShadowCascadeStats, SHADOW_CASCADE_COUNT> cascadeStats{};

    // Single-pass traversal over terrain chunks and binning into cascades
    std::array<CascadeAabbCuller, 4> cascadeCullers{};
    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
        const float cascadePaddingWorld = std::max(16.0f, cascadeData.texelWorldSize * 16.0f);
        cascadeCullers[cascade] = CascadeAabbCuller{
            cascadeData.viewProj,
            cascadePaddingWorld / std::max(1.0f, cascadeData.radius),
            std::max(64.0f, cascadeData.texelWorldSize * 64.0f) /
                std::max(1.0f, cascadeData.depthExtent),
            true,
            0,
            0
        };
        const glm::mat4& cullerMatrix = cascadeCullers[cascade].viewProj;
        cascadeCullers[cascade].absClipExtentX = glm::vec3(
            std::abs(cullerMatrix[0][0]), std::abs(cullerMatrix[1][0]), std::abs(cullerMatrix[2][0]));
        cascadeCullers[cascade].absClipExtentY = glm::vec3(
            std::abs(cullerMatrix[0][1]), std::abs(cullerMatrix[1][1]), std::abs(cullerMatrix[2][1]));
        cascadeCullers[cascade].absClipExtentZ = glm::vec3(
            std::abs(cullerMatrix[0][2]), std::abs(cullerMatrix[1][2]), std::abs(cullerMatrix[2][2]));
    }

    float shadowCasterCullDistance = shadowDist;
    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
        shadowCasterCullDistance = std::max(
            shadowCasterCullDistance,
            cascadeData.radius + std::max(32.0f, cascadeData.texelWorldSize * 64.0f));
    }

    shadow::ShadowCasterCuller shadowCuller;
    shadowCuller.setup(shadowCasterCullDistance, 1.0f, ctx.camera.position);
    shadowCuller.resetCounters();

    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        m_cascadeOpaqueRanges[cascade].clear();
        m_cascadeCutoutRanges[cascade].clear();
        m_cascadeTransparentRanges[cascade].clear();
        m_cascadeOpaqueEntries[cascade].clear();
        m_cascadeCutoutEntries[cascade].clear();
        m_cascadeTransparentEntries[cascade].clear();
    }

    m_terrainRenderer->clearTransparentBatches();
    m_terrainRenderer->collectShadowChunks(
        worldView,
        ctx.camera.position,
        shadowDist,
        &shadowCuller,
        cascadeCullers,
        m_cascadeOpaqueRanges,
        m_cascadeCutoutRanges,
        m_cascadeTransparentRanges,
        m_cascadeOpaqueEntries,
        m_cascadeCutoutEntries,
        m_cascadeTransparentEntries
    );
    m_terrainRenderer->syncTransparentBatches();

    visibleTotal = shadowCuller.getVisibleCount();
    culledTotal = shadowCuller.getCulledCount();
    maxCasterDistance = shadowCuller.getMaxCasterDistance();

    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        char cascadeLabel[32];
        std::snprintf(cascadeLabel, sizeof(cascadeLabel), "Shadow.CSM.Cascade%d", cascade);
        renderer::debug::ScopedDebugGroup cascadeGroup(cascadeLabel);

        const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
        const bool renderCutoutCasters = cascade < kCutoutShadowCasterCascadeCount;
        const bool renderTransparentCasters = cascade < kTransparentShadowCasterCascadeCount;

        // Explicit GL state at cascade start — prevents leaked state from
        // entity/drop shadow sub-passes in previous cascades.
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        m_worldRenderBuffer->beginFrame();
        for (const auto& range : m_cascadeOpaqueRanges[cascade]) {
            m_worldRenderBuffer->addOpaque(range);
        }
        if (renderCutoutCasters) {
            for (const auto& range : m_cascadeCutoutRanges[cascade]) {
                m_worldRenderBuffer->addCutout(range);
            }
        }

        char cullerLabel[128];
        std::snprintf(cullerLabel, sizeof(cullerLabel),
                      "Shadow.Cascade%d.Culler boxVisible=%d boxCulled=%d zCull=%d distanceVisible=%d distanceCulled=%d",
                      cascade,
                      cascadeCullers[cascade].visibleCount,
                      cascadeCullers[cascade].culledCount,
                      cascadeCullers[cascade].useZCulling ? 1 : 0,
                      shadowCuller.getVisibleCount(),
                      shadowCuller.getCulledCount());
        renderer::debug::insertEvent(cullerLabel);

        ShadowCascadeStats stats;
        stats.boxVisible = cascadeCullers[cascade].visibleCount;
        stats.boxCulled = cascadeCullers[cascade].culledCount;
        stats.distanceVisible = shadowCuller.getVisibleCount();
        stats.distanceCulled = shadowCuller.getCulledCount();
        stats.cutoutEntries = renderCutoutCasters
            ? static_cast<int>(m_cascadeCutoutRanges[cascade].size())
            : 0;
        stats.transparentEntries = renderTransparentCasters
            ? static_cast<int>(m_cascadeTransparentRanges[cascade].size())
            : 0;
        stats.opaqueCommands = m_worldRenderBuffer->opaqueCommandCount();
        stats.cutoutCommands = m_worldRenderBuffer->cutoutCommandCount();
        stats.opaqueVertices = m_worldRenderBuffer->opaqueVertexCount();
        stats.cutoutVertices = m_worldRenderBuffer->cutoutVertexCount();
        stats.splitNear = cascadeData.splitNear;
        stats.splitFar = cascadeData.splitFar;
        stats.radius = cascadeData.radius;
        stats.texelWorldSize = cascadeData.texelWorldSize;
        const int cascadeRes = (cascade >= 2) ? settings.shadow.resolution / 2
                                               : settings.shadow.resolution;

        // Pass 1: Opaque-only → DepthOpaque (shadowtex1)
        {
            renderer::debug::ScopedDebugGroup opaqueGroup("Opaque");
            if (shadowStatsActive) {
                debugService->markShadowTimestamp(cascade, ShadowTimestampPoint::Start);
            }

            RhiDepthStencilAttachment depthAttachment;
            depthAttachment.view = targets.csmShadowDepthTextureViewHandle(cascade);
            depthAttachment.depthLoadOp = RhiLoadOp::Clear;
            depthAttachment.depthStoreOp = RhiStoreOp::Store;
            depthAttachment.clearDepth = 1.0f;

            RhiRenderingInfo renderingInfo;
            renderingInfo.debugName = "CsmShadowOpaque";
            renderingInfo.renderArea = {
                0,
                0,
                static_cast<uint32_t>(std::max(1, cascadeRes)),
                static_cast<uint32_t>(std::max(1, cascadeRes))
            };
            renderingInfo.depthStencilAttachment = &depthAttachment;

            RhiCommandList& commandList = rhiDevice.beginFrame();
            TerrainShadowFrameData shadowFrame;
            shadowFrame.modelView = cascadeData.view;
            shadowFrame.projection = cascadeData.projection;
            shadowFrame.lightDirection = m_shadowRenderer->lightDirection();
            shadowFrame.animationTime = ctx.animationTime;
            shadowFrame.shaderTime = ctx.shaderTime;
            shadowFrame.passMode = 0;
            if (!ctx.shared->terrainRhiPipelines->prepareShadow(
                    commandList,
                    *m_resourceMgr,
                    shadowFrame) ||
                !m_worldRenderBuffer->prepareRhiOpaqueAndCutout(
                    commandList,
                    ctx.shared->terrainRhiPipelines->shadowMetadataLayout())) {
                return output;
            }
            commandList.beginRendering(renderingInfo);

            m_worldRenderBuffer->recordRhiOpaque(
                commandList,
                ctx.shared->terrainRhiPipelines->shadowOpaquePipeline(),
                ctx.shared->terrainRhiPipelines->shadowBindGroup());
            if (renderCutoutCasters) {
                m_worldRenderBuffer->recordRhiCutout(
                    commandList,
                    ctx.shared->terrainRhiPipelines->shadowCutoutPipeline(),
                    ctx.shared->terrainRhiPipelines->shadowBindGroup());
                commandList.setGraphicsPipeline(
                    ctx.shared->terrainRhiPipelines->shadowOpaquePipeline());
            }
            // Block entity shadow: render chest-style entity models into this cascade.
            renderShadowBlockEntities(worldView, cascadeData.viewProj, ctx.camera.position,
                                      cascadeData.splitNear, cascadeData.splitFar);
            // Entity shadow: render humanoid/mob depth into this cascade with distance/split culling.
            renderShadowEntities(worldView, cascadeData.viewProj, ctx.camera.position, cascadeData.splitNear, cascadeData.splitFar);
            // Drop shadow: render dropped items/blocks depth into this cascade.
            renderShadowDrops(worldView, cascadeData.viewProj, cascadeData.view, cascadeData.projection,
                              ctx.animationTime, ctx.shaderTime);
            // Falling-block shadow: render falling sand/gravel depth into this cascade.
            renderShadowFallingBlocks(cascadeData.viewProj, cascadeData.view, cascadeData.projection,
                                      ctx.animationTime, ctx.shaderTime);
            if (shadowStatsActive) {
                debugService->markShadowTimestamp(cascade, ShadowTimestampPoint::OpaqueEnd);
            }
            commandList.endRendering();
            rhiDevice.submitFrame(commandList);
        }

        // Pass 2: Transparent/all.
        // Copy DepthOpaque -> DepthAll, then draw near water + stained glass casters.
        {
            renderer::debug::ScopedDebugGroup transparentGroup("Transparent");
            // Copy opaque depth to DepthAll as baseline (avoids re-rendering opaque)
            RhiCommandList& commandList = rhiDevice.beginFrame();
            RhiTextureCopy depthCopy;
            depthCopy.src = targets.csmShadowDepthTextureHandle();
            depthCopy.srcSubresource.baseArrayLayer = static_cast<uint32_t>(cascade);
            depthCopy.dst = targets.csmShadowDepthAllTextureHandle();
            depthCopy.dstSubresource.baseArrayLayer = static_cast<uint32_t>(cascade);
            depthCopy.extent = {
                static_cast<uint32_t>(std::max(1, cascadeRes)),
                static_cast<uint32_t>(std::max(1, cascadeRes)),
                1u
            };
            commandList.copyTexture(depthCopy);

            RhiColorAttachment colorAttachments[2];
            colorAttachments[0].view = targets.csmShadowColor0TextureViewHandle(cascade);
            colorAttachments[0].loadOp = RhiLoadOp::Clear;
            colorAttachments[0].storeOp = RhiStoreOp::Store;
            colorAttachments[0].clearColor[0] = 0.0f;
            colorAttachments[0].clearColor[1] = 0.0f;
            colorAttachments[0].clearColor[2] = 0.0f;
            colorAttachments[0].clearColor[3] = 1.0f;
            colorAttachments[1].view = targets.csmShadowColor1TextureViewHandle(cascade);
            colorAttachments[1].loadOp = RhiLoadOp::Clear;
            colorAttachments[1].storeOp = RhiStoreOp::Store;
            colorAttachments[1].clearColor[0] = 0.0f;
            colorAttachments[1].clearColor[1] = 0.0f;
            colorAttachments[1].clearColor[2] = 0.0f;
            colorAttachments[1].clearColor[3] = 0.0f;

            RhiDepthStencilAttachment depthAttachment;
            depthAttachment.view = targets.csmShadowDepthAllTextureViewHandle(cascade);
            depthAttachment.depthLoadOp = RhiLoadOp::Load;
            depthAttachment.depthStoreOp = RhiStoreOp::Store;

            RhiRenderingInfo renderingInfo;
            renderingInfo.debugName = "CsmShadowTransparent";
            renderingInfo.renderArea = {
                0,
                0,
                static_cast<uint32_t>(std::max(1, cascadeRes)),
                static_cast<uint32_t>(std::max(1, cascadeRes))
            };
            renderingInfo.colorAttachments = colorAttachments;
            renderingInfo.colorAttachmentCount = 2u;
            renderingInfo.depthStencilAttachment = &depthAttachment;

            if (renderTransparentCasters) {
                m_worldRenderBuffer->beginFrame();
                for (const GpuMeshRange& range : m_cascadeTransparentRanges[cascade]) {
                    m_worldRenderBuffer->addTransparent(range);
                }
                stats.transparentCommands = m_worldRenderBuffer->transparentCommandCount();
                stats.transparentVertices = m_worldRenderBuffer->transparentVertexCount();

                TerrainShadowFrameData shadowFrame;
                shadowFrame.modelView = cascadeData.view;
                shadowFrame.projection = cascadeData.projection;
                shadowFrame.lightDirection = m_shadowRenderer->lightDirection();
                shadowFrame.animationTime = ctx.animationTime;
                shadowFrame.shaderTime = ctx.shaderTime;
                shadowFrame.passMode = 1;
                if (!ctx.shared->terrainRhiPipelines->prepareShadow(
                        commandList,
                        *m_resourceMgr,
                        shadowFrame) ||
                    !m_worldRenderBuffer->prepareRhiTransparent(
                        commandList,
                        ctx.shared->terrainRhiPipelines->shadowMetadataLayout())) {
                    return output;
                }
            }
            commandList.beginRendering(renderingInfo);

            if (renderTransparentCasters) {
                m_worldRenderBuffer->recordRhiTransparent(
                    commandList,
                    ctx.shared->terrainRhiPipelines->shadowTransparentPipeline(),
                    ctx.shared->terrainRhiPipelines->shadowBindGroup());
                stats.transparentRendered = true;
            }
            commandList.endRendering();
            rhiDevice.submitFrame(commandList);
        }
        if (shadowStatsActive) {
            debugService->markShadowTimestamp(cascade, ShadowTimestampPoint::End);
        }
        cascadeStats[static_cast<size_t>(cascade)] = stats;
        if (shadowStatsActive) {
            debugService->recordShadowCascadeStats(cascade, stats);
        }
    }

    if (shadowStatsActive) {
        debugService->recordShadowFrameTotals(visibleTotal, culledTotal, maxCasterDistance);
        debugService->endShadowFrame();
    }

    // Transitional compatibility for historical debug modes that still inspect
    // the legacy single-map projection: expose cascade 0 there.
    if (settings.debug.deferredLightDebugMode > 0 || settings.debug.lightDebugMode > 0) {
        RhiCommandList& commandList = rhiDevice.beginFrame();
        RhiTextureCopy debugDepthCopy;
        debugDepthCopy.src = targets.csmShadowDepthTextureHandle();
        debugDepthCopy.dst = targets.shadowDepthTextureHandle();
        debugDepthCopy.extent = {
            static_cast<uint32_t>(std::max(1, targets.shadowResolution())),
            static_cast<uint32_t>(std::max(1, targets.shadowResolution())),
            1u
        };
        commandList.copyTexture(debugDepthCopy);
        rhiDevice.submitFrame(commandList);
    }

    m_worldRenderBuffer->beginFrame();
    glBindVertexArray(0);

    return output;
}
