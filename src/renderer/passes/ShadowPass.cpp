#include "ShadowPass.h"
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
#include "../../resource/ResourceMgr.h"
#include "../../world/IWorldView.h"

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

static constexpr int SHADOW_CASCADE_COUNT = shadow::ShadowRenderer::CASCADE_COUNT;
using ShadowCascadeData = shadow::ShadowRenderer::Cascade;

namespace {
[[nodiscard]] bool sameTextureHandle(const RhiTextureHandle lhs,
                                     const RhiTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

void resetCascadeStatesForTexture(const RhiTextureHandle texture,
                                  RhiTextureHandle& trackedTexture,
                                  std::array<RhiResourceState, SHADOW_CASCADE_COUNT>& states,
                                  const RhiResourceState stableState) {
    if (sameTextureHandle(texture, trackedTexture)) {
        return;
    }
    trackedTexture = texture;
    states.fill(stableState);
}

void transitionCascadeLayer(
    RhiCommandList& commandList,
    const RhiTextureHandle texture,
    std::array<RhiResourceState, SHADOW_CASCADE_COUNT>& states,
    const int cascade,
    const RhiResourceState newState) {
    RhiResourceState& oldState = states[static_cast<size_t>(cascade)];
    if (oldState == newState) {
        return;
    }
    commandList.textureBarrier({
        texture,
        oldState,
        newState,
        0u,
        1u,
        static_cast<uint32_t>(cascade),
        1u
    });
    oldState = newState;
}
} // namespace

void ShadowPass::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
}

void ShadowPass::shutdown() {
    m_resourceMgr = nullptr;
    m_trackedCsmDepthTexture = {};
    m_trackedCsmDepthAllTexture = {};
    m_trackedCsmColor0Texture = {};
    m_trackedCsmColor1Texture = {};
    m_csmDepthStates.fill(RhiResourceState::Undefined);
    m_csmDepthAllStates.fill(RhiResourceState::Undefined);
    m_csmColor0States.fill(RhiResourceState::Undefined);
    m_csmColor1States.fill(RhiResourceState::Undefined);
}

void ShadowPass::renderShadowEntities(RhiCommandList& commandList,
                                      const glm::mat4& shadowViewProj,
                                      const glm::vec3& cameraPos,
                                      const float splitNear,
                                      const float splitFar) {
    if (m_humanoidRenderer == nullptr || m_gameplayRegistry == nullptr) {
        return;
    }
    m_humanoidRenderer->renderPreparedToShadowMap(
        commandList, shadowViewProj, cameraPos, splitNear, splitFar);
}

void ShadowPass::renderShadowBlockEntities(RhiCommandList& commandList,
                                           const glm::mat4& shadowViewProj) {
    if (m_blockEntityRenderer == nullptr) {
        return;
    }
    m_blockEntityRenderer->renderToShadowMap(commandList, shadowViewProj);
}

void ShadowPass::renderShadowDrops(RhiCommandList& commandList,
                                   const glm::mat4& shadowViewProj,
                                   const float animationTime) {
    if (m_dropRenderer == nullptr || m_dropSystem == nullptr) {
        return;
    }
    m_dropRenderer->renderToShadowMap(commandList, shadowViewProj, animationTime);
}

void ShadowPass::renderShadowFallingBlocks(RhiCommandList& commandList,
                                            const glm::mat4& shadowViewProj,
                                            float animationTime) {
    // Render falling-block entities into the current shadow cascade layer.
    // The caller has already begun rendering for the selected cascade layer.
    if (m_fallingBlockRenderer == nullptr || m_gameplayRegistry == nullptr) {
        return;
    }

    m_fallingBlockRenderer->renderToShadowMap(commandList, shadowViewProj, animationTime);

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
    if (m_blockEntityRenderer != nullptr) {
        m_blockEntityRenderer->prepareFrame(worldView);
    }
    if (m_dropRenderer != nullptr && m_dropSystem != nullptr) {
        m_dropRenderer->prepareFrame(worldView, *m_dropSystem);
    }
    if (m_humanoidRenderer != nullptr && m_gameplayRegistry != nullptr) {
        m_humanoidRenderer->prepareFrame(
            worldView, *m_gameplayRegistry, HumanoidRenderer::kRenderAll);
    }

    // Update shadow cascades via ShadowRenderer.
    m_shadowRenderer->computeLightDirection(ctx.skyColors.sunDirection,
                                            ctx.skyColors.sunVisibility,
                                            ctx.skyColors.moonDirection,
                                            ctx.skyColors.moonVisibility);

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
    resetCascadeStatesForTexture(targets.csmShadowDepthTextureHandle(),
                                 m_trackedCsmDepthTexture,
                                 m_csmDepthStates,
                                 RhiResourceState::DepthRead);
    resetCascadeStatesForTexture(targets.csmShadowDepthAllTextureHandle(),
                                 m_trackedCsmDepthAllTexture,
                                 m_csmDepthAllStates,
                                 RhiResourceState::DepthRead);
    resetCascadeStatesForTexture(targets.csmShadowColor0TextureHandle(),
                                 m_trackedCsmColor0Texture,
                                 m_csmColor0States,
                                 RhiResourceState::ShaderRead);
    resetCascadeStatesForTexture(targets.csmShadowColor1TextureHandle(),
                                 m_trackedCsmColor1Texture,
                                 m_csmColor1States,
                                 RhiResourceState::ShaderRead);

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
        m_cascadeTransparentRanges
    );
    m_terrainRenderer->syncTransparentBatches();

    visibleTotal = shadowCuller.getVisibleCount();
    culledTotal = shadowCuller.getCulledCount();
    maxCasterDistance = shadowCuller.getMaxCasterDistance();

    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
        const bool renderCutoutCasters = cascade < kCutoutShadowCasterCascadeCount;
        const bool renderTransparentCasters = cascade < kTransparentShadowCasterCascadeCount;

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

            RhiCommandList* commandListStorage = ctx.shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"RenderPass.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
            const GpuTimerSegmentToken gpuTimer = debugService != nullptr
                ? debugService->beginGpuTimer(commandList, GpuTimerPass::Shadow)
                : GpuTimerSegmentToken{};
            if (shadowStatsActive) {
                debugService->markShadowTimestamp(
                    commandList, cascade, ShadowTimestampPoint::Start);
            }
            char opaqueLabel[48];
            std::snprintf(opaqueLabel, sizeof(opaqueLabel),
                          "Shadow.Cascade%d.Opaque", cascade);
            commandList.beginDebugLabel(opaqueLabel, glm::vec4(0.70f, 0.78f, 1.0f, 1.0f));
            commandList.insertDebugMarker(cullerLabel, glm::vec4(0.45f, 0.65f, 1.0f, 1.0f));
            if (m_blockEntityRenderer != nullptr &&
                !m_blockEntityRenderer->prepareShadow(commandList, ctx.camera.position,
                                                      cascadeData.splitNear,
                                                      cascadeData.splitFar)) {
                if (debugService != nullptr) {
                    debugService->cancelGpuTimer(gpuTimer);
                }
                commandList.endDebugLabel();
                return output;
            }
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
                if (debugService != nullptr) {
                    debugService->cancelGpuTimer(gpuTimer);
                }
                commandList.endDebugLabel();
                return output;
            }
            transitionCascadeLayer(commandList,
                                   targets.csmShadowDepthTextureHandle(),
                                   m_csmDepthStates,
                                   cascade,
                                   RhiResourceState::DepthWrite);
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
            renderShadowBlockEntities(commandList, cascadeData.viewProj);
            // Entity shadow: render humanoid/mob depth into this cascade with distance/split culling.
            renderShadowEntities(commandList, cascadeData.viewProj, ctx.camera.position,
                                 cascadeData.splitNear, cascadeData.splitFar);
            // Drop shadow: render dropped items/blocks depth into this cascade.
            renderShadowDrops(commandList, cascadeData.viewProj, ctx.animationTime);
            // Falling-block shadow: render falling sand/gravel depth into this cascade.
            renderShadowFallingBlocks(commandList, cascadeData.viewProj, ctx.animationTime);
            if (shadowStatsActive) {
                debugService->markShadowTimestamp(
                    commandList, cascade, ShadowTimestampPoint::OpaqueEnd);
            }
            commandList.endRendering();
            transitionCascadeLayer(commandList,
                                   targets.csmShadowDepthTextureHandle(),
                                   m_csmDepthStates,
                                   cascade,
                                   RhiResourceState::DepthRead);
            if (debugService != nullptr) {
                debugService->endGpuTimer(commandList, gpuTimer);
            }
            commandList.endDebugLabel();
            if (!commandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&commandList};
        if (!rhiDevice.submit({"RenderPass.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
        }

        // Pass 2: Transparent/all.
        // Copy DepthOpaque -> DepthAll, then draw near water + stained glass casters.
        {
            // Copy opaque depth to DepthAll as baseline (avoids re-rendering opaque)
            RhiCommandList* commandListStorage = ctx.shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"RenderPass.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
            const GpuTimerSegmentToken gpuTimer = debugService != nullptr
                ? debugService->beginGpuTimer(commandList, GpuTimerPass::Shadow)
                : GpuTimerSegmentToken{};
            char transparentLabel[48];
            std::snprintf(transparentLabel, sizeof(transparentLabel),
                          "Shadow.Cascade%d.Transparent", cascade);
            commandList.beginDebugLabel(transparentLabel, glm::vec4(0.55f, 0.85f, 1.0f, 1.0f));
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
                    if (debugService != nullptr) {
                        debugService->cancelGpuTimer(gpuTimer);
                    }
                    commandList.endDebugLabel();
                    return output;
                }
            }
            transitionCascadeLayer(commandList,
                                   targets.csmShadowDepthTextureHandle(),
                                   m_csmDepthStates,
                                   cascade,
                                   RhiResourceState::TransferSrc);
            transitionCascadeLayer(commandList,
                                   targets.csmShadowDepthAllTextureHandle(),
                                   m_csmDepthAllStates,
                                   cascade,
                                   RhiResourceState::TransferDst);
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
            transitionCascadeLayer(commandList,
                                   targets.csmShadowDepthTextureHandle(),
                                   m_csmDepthStates,
                                   cascade,
                                   RhiResourceState::DepthRead);
            transitionCascadeLayer(commandList,
                                   targets.csmShadowDepthAllTextureHandle(),
                                   m_csmDepthAllStates,
                                   cascade,
                                   RhiResourceState::DepthWrite);
            transitionCascadeLayer(commandList,
                                   targets.csmShadowColor0TextureHandle(),
                                   m_csmColor0States,
                                   cascade,
                                   RhiResourceState::RenderTarget);
            transitionCascadeLayer(commandList,
                                   targets.csmShadowColor1TextureHandle(),
                                   m_csmColor1States,
                                   cascade,
                                   RhiResourceState::RenderTarget);
            commandList.beginRendering(renderingInfo);

            if (renderTransparentCasters) {
                m_worldRenderBuffer->recordRhiTransparent(
                    commandList,
                    ctx.shared->terrainRhiPipelines->shadowTransparentPipeline(),
                    ctx.shared->terrainRhiPipelines->shadowBindGroup());
                stats.transparentRendered = true;
            }
            commandList.endRendering();
            transitionCascadeLayer(commandList,
                                   targets.csmShadowDepthAllTextureHandle(),
                                   m_csmDepthAllStates,
                                   cascade,
                                   RhiResourceState::DepthRead);
            transitionCascadeLayer(commandList,
                                   targets.csmShadowColor0TextureHandle(),
                                   m_csmColor0States,
                                   cascade,
                                   RhiResourceState::ShaderRead);
            transitionCascadeLayer(commandList,
                                   targets.csmShadowColor1TextureHandle(),
                                   m_csmColor1States,
                                   cascade,
                                   RhiResourceState::ShaderRead);
            if (shadowStatsActive) {
                debugService->markShadowTimestamp(
                    commandList, cascade, ShadowTimestampPoint::End);
            }
            if (debugService != nullptr) {
                debugService->endGpuTimer(commandList, gpuTimer);
            }
            commandList.endDebugLabel();
            if (!commandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&commandList};
        if (!rhiDevice.submit({"RenderPass.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
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

    m_worldRenderBuffer->beginFrame();

    return output;
}
