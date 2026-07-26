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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>

#include "renderer/core/FrameContext.h"

static constexpr int SHADOW_CASCADE_COUNT = shadow::ShadowRenderer::CASCADE_COUNT;
using ShadowCascadeData = shadow::ShadowRenderer::Cascade;

namespace {
constexpr int kTransparentShadowCasterCascadeCount = 2;
constexpr int kCutoutShadowCasterCascadeCount = SHADOW_CASCADE_COUNT;

[[nodiscard]] RgTextureSubresourceRange cascadeRange(const int cascade) {
    return {0u, 1u, static_cast<uint32_t>(cascade), 1u, 0u};
}
} // namespace

void ShadowPass::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
}

void ShadowPass::shutdown() {
    if (m_shadowStatsActive && m_frameDebugService != nullptr) {
        m_frameDebugService->cancelShadowFrame();
    }
    m_resourceMgr = nullptr;
    m_frameContext = nullptr;
    m_frameTargets = nullptr;
    m_frameDebugService = nullptr;
    m_graphFramePrepared = false;
    m_graphExecutionBegun = false;
    m_shadowStatsActive = false;
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

bool ShadowPass::prepareGraphFrame(const FrameContext& ctx,
                                   const RenderSettings& settings,
                                   DeferredRenderTargets& targets,
                                   const IWorldView& worldView) {
    if (m_graphFramePrepared || m_shadowRenderer == nullptr ||
        m_terrainRenderer == nullptr || m_worldRenderBuffer == nullptr ||
        m_resourceMgr == nullptr || ctx.shared == nullptr ||
        ctx.shared->rhiDevice == nullptr ||
        ctx.shared->terrainRhiPipelines == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        if (!targets.ensureCsmShadowDepthTextureView(rhiDevice, cascade) ||
            !targets.ensureCsmShadowTransparentTextureViews(rhiDevice, cascade)) {
            return false;
        }
    }

    m_shadowRenderer->computeLightDirection(ctx.skyColors.sunDirection,
                                            ctx.skyColors.sunVisibility,
                                            ctx.skyColors.moonDirection,
                                            ctx.skyColors.moonVisibility);
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

    shadow::ShadowMatrices::Settings shadowSettings;
    shadowSettings.shadowDistance = settings.shadow.distance;
    shadowSettings.shadowResolution = settings.shadow.resolution;
    m_shadowRenderer->updateFromBasis(basis, shadowSettings);

    const float shadowDistance = std::max(64.0f, settings.shadow.distance);
    std::array<CascadeAabbCuller, SHADOW_CASCADE_COUNT> cascadeCullers{};
    float casterCullDistance = shadowDistance;
    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
        const float worldPadding = std::max(16.0f, cascadeData.texelWorldSize * 16.0f);
        cascadeCullers[cascade] = {
            cascadeData.viewProj,
            worldPadding / std::max(1.0f, cascadeData.radius),
            std::max(64.0f, cascadeData.texelWorldSize * 64.0f) /
                std::max(1.0f, cascadeData.depthExtent),
            true,
            0,
            0
        };
        const glm::mat4& cullerMatrix = cascadeCullers[cascade].viewProj;
        cascadeCullers[cascade].absClipExtentX = glm::vec3(
            std::abs(cullerMatrix[0][0]), std::abs(cullerMatrix[1][0]),
            std::abs(cullerMatrix[2][0]));
        cascadeCullers[cascade].absClipExtentY = glm::vec3(
            std::abs(cullerMatrix[0][1]), std::abs(cullerMatrix[1][1]),
            std::abs(cullerMatrix[2][1]));
        cascadeCullers[cascade].absClipExtentZ = glm::vec3(
            std::abs(cullerMatrix[0][2]), std::abs(cullerMatrix[1][2]),
            std::abs(cullerMatrix[2][2]));
        casterCullDistance = std::max(
            casterCullDistance,
            cascadeData.radius + std::max(32.0f, cascadeData.texelWorldSize * 64.0f));
        m_cascadeOpaqueRanges[cascade].clear();
        m_cascadeCutoutRanges[cascade].clear();
        m_cascadeTransparentRanges[cascade].clear();
    }

    shadow::ShadowCasterCuller shadowCuller;
    shadowCuller.setup(casterCullDistance, 1.0f, ctx.camera.position);
    shadowCuller.resetCounters();
    m_terrainRenderer->clearTransparentBatches();
    m_terrainRenderer->collectShadowChunks(
        worldView, ctx.camera.position, shadowDistance, &shadowCuller,
        cascadeCullers, m_cascadeOpaqueRanges, m_cascadeCutoutRanges,
        m_cascadeTransparentRanges);
    m_terrainRenderer->syncTransparentBatches();

    m_visibleTotal = shadowCuller.getVisibleCount();
    m_culledTotal = shadowCuller.getCulledCount();
    m_maxCasterDistance = shadowCuller.getMaxCasterDistance();
    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
        ShadowCascadeStats& stats = m_cascadeStats[cascade];
        stats = {};
        stats.boxVisible = cascadeCullers[cascade].visibleCount;
        stats.boxCulled = cascadeCullers[cascade].culledCount;
        stats.distanceVisible = m_visibleTotal;
        stats.distanceCulled = m_culledTotal;
        stats.cutoutEntries = cascade < kCutoutShadowCasterCascadeCount
            ? static_cast<int>(m_cascadeCutoutRanges[cascade].size())
            : 0;
        stats.transparentEntries = cascade < kTransparentShadowCasterCascadeCount
            ? static_cast<int>(m_cascadeTransparentRanges[cascade].size())
            : 0;
        stats.splitNear = cascadeData.splitNear;
        stats.splitFar = cascadeData.splitFar;
        stats.radius = cascadeData.radius;
        stats.texelWorldSize = cascadeData.texelWorldSize;
        m_cascadeResolutions[cascade] = static_cast<uint32_t>(std::max(
            1, cascade >= 2 ? settings.shadow.resolution / 2
                            : settings.shadow.resolution));
        std::snprintf(
            m_cullerLabels[cascade].data(), m_cullerLabels[cascade].size(),
            "Shadow.Cascade%d.Culler boxVisible=%d boxCulled=%d zCull=%d distanceVisible=%d distanceCulled=%d",
            cascade, cascadeCullers[cascade].visibleCount,
            cascadeCullers[cascade].culledCount,
            cascadeCullers[cascade].useZCulling ? 1 : 0,
            m_visibleTotal, m_culledTotal);
    }

    m_frameContext = &ctx;
    m_frameTargets = &targets;
    m_frameDebugService = ctx.debugService;
    m_graphFramePrepared = true;
    return true;
}

RgPassHandle ShadowPass::addGraphPasses(RenderGraph& graph,
                                        const GraphResources& resources,
                                        const RgPassHandle dependency) {
    if (!m_graphFramePrepared || !dependency.isValid() ||
        !resources.depthOpaque.isValid() || !resources.depthAll.isValid() ||
        !resources.color0.isValid() || !resources.color1.isValid()) {
        return {};
    }

    RgPassHandle previous = dependency;
    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        const RgTextureSubresourceRange range = cascadeRange(cascade);
        char opaqueName[48];
        std::snprintf(opaqueName, sizeof(opaqueName),
                      "Shadow.Cascade%d.Opaque", cascade);
        RenderGraphPassBuilder opaque = graph.addPass(
            {opaqueName, RgPassType::Graphics, RhiQueueType::Graphics});
        opaque.dependsOn(previous)
            .writeTexture(resources.depthOpaque, RhiResourceState::DepthWrite, range)
            .setExecute([this, cascade](RgPassContext& pass) {
                return recordOpaquePass(pass.commandList(), cascade);
            });
        previous = opaque.handle();

        char copyName[48];
        std::snprintf(copyName, sizeof(copyName),
                      "Shadow.Cascade%d.CopyDepth", cascade);
        RenderGraphPassBuilder copy = graph.addPass(
            {copyName, RgPassType::Copy, RhiQueueType::Graphics});
        copy.dependsOn(previous)
            .readTexture(resources.depthOpaque, RhiResourceState::TransferSrc, range)
            .writeTexture(resources.depthAll, RhiResourceState::TransferDst, range)
            .setExecute([this, cascade](RgPassContext& pass) {
                return recordCopyPass(pass.commandList(), cascade);
            });
        previous = copy.handle();

        char transparentName[48];
        std::snprintf(transparentName, sizeof(transparentName),
                      "Shadow.Cascade%d.Transparent", cascade);
        RenderGraphPassBuilder transparent = graph.addPass(
            {transparentName, RgPassType::Graphics, RhiQueueType::Graphics});
        transparent.dependsOn(previous)
            .readWriteTexture(resources.depthAll, RhiResourceState::DepthWrite, range)
            .writeTexture(resources.color0, RhiResourceState::RenderTarget, range)
            .writeTexture(resources.color1, RhiResourceState::RenderTarget, range)
            .setExecute([this, cascade](RgPassContext& pass) {
                return recordTransparentPass(pass.commandList(), cascade);
            });
        previous = transparent.handle();
    }
    return previous;
}

void ShadowPass::beginGraphExecution() {
    if (!m_graphFramePrepared || m_graphExecutionBegun) {
        std::abort();
    }
    m_graphExecutionBegun = true;
    m_shadowStatsActive = m_frameDebugService != nullptr &&
        m_frameDebugService->beginShadowFrame(
            SHADOW_CASCADE_COUNT, m_frameTargets->shadowResolution());
}

void ShadowPass::finishGraphExecution(const bool succeeded) {
    if (!m_graphFramePrepared || (succeeded && !m_graphExecutionBegun)) {
        std::abort();
    }
    if (m_shadowStatsActive) {
        if (succeeded) {
            for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
                m_frameDebugService->recordShadowCascadeStats(
                    cascade, m_cascadeStats[cascade]);
            }
            m_frameDebugService->recordShadowFrameTotals(
                m_visibleTotal, m_culledTotal, m_maxCasterDistance);
            m_frameDebugService->endShadowFrame();
        } else {
            m_frameDebugService->cancelShadowFrame();
        }
    }
    m_worldRenderBuffer->beginFrame();
    m_frameContext = nullptr;
    m_frameTargets = nullptr;
    m_frameDebugService = nullptr;
    m_graphFramePrepared = false;
    m_graphExecutionBegun = false;
    m_shadowStatsActive = false;
}

bool ShadowPass::recordOpaquePass(RhiCommandList& commandList, const int cascade) {
    if (!m_graphExecutionBegun || cascade < 0 || cascade >= SHADOW_CASCADE_COUNT) {
        return false;
    }
    const FrameContext& ctx = *m_frameContext;
    DeferredRenderTargets& targets = *m_frameTargets;
    const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
    const bool renderCutoutCasters = cascade < kCutoutShadowCasterCascadeCount;

    if (cascade == 0) {
        if (m_blockEntityRenderer != nullptr) {
            m_blockEntityRenderer->prepareFrame(*ctx.worldView);
        }
        if (m_dropRenderer != nullptr && m_dropSystem != nullptr) {
            m_dropRenderer->prepareFrame(*ctx.worldView, *m_dropSystem);
        }
        if (m_humanoidRenderer != nullptr && m_gameplayRegistry != nullptr) {
            m_humanoidRenderer->prepareFrame(
                *ctx.worldView, *m_gameplayRegistry, HumanoidRenderer::kRenderAll);
        }
    }

    m_worldRenderBuffer->resetDrawCommands();
    for (const GpuMeshRange& range : m_cascadeOpaqueRanges[cascade]) {
        m_worldRenderBuffer->addOpaque(range);
    }
    if (renderCutoutCasters) {
        for (const GpuMeshRange& range : m_cascadeCutoutRanges[cascade]) {
            m_worldRenderBuffer->addCutout(range);
        }
    }
    ShadowCascadeStats& stats = m_cascadeStats[cascade];
    stats.opaqueCommands = m_worldRenderBuffer->opaqueCommandCount();
    stats.cutoutCommands = m_worldRenderBuffer->cutoutCommandCount();
    stats.opaqueVertices = m_worldRenderBuffer->opaqueVertexCount();
    stats.cutoutVertices = m_worldRenderBuffer->cutoutVertexCount();

    const GpuTimerSegmentToken gpuTimer = m_frameDebugService != nullptr
        ? m_frameDebugService->beginGpuTimer(commandList, GpuTimerPass::Shadow)
        : GpuTimerSegmentToken{};
    if (m_shadowStatsActive) {
        m_frameDebugService->markShadowTimestamp(
            commandList, cascade, ShadowTimestampPoint::Start);
    }
    commandList.insertDebugMarker(
        m_cullerLabels[cascade].data(), glm::vec4(0.45f, 0.65f, 1.0f, 1.0f));
    if (m_blockEntityRenderer != nullptr &&
        !m_blockEntityRenderer->prepareShadow(
            commandList, ctx.camera.position, cascadeData.splitNear,
            cascadeData.splitFar)) {
        return false;
    }

    TerrainShadowFrameData shadowFrame;
    shadowFrame.modelView = cascadeData.view;
    shadowFrame.projection = cascadeData.projection;
    shadowFrame.lightDirection = m_shadowRenderer->lightDirection();
    shadowFrame.animationTime = ctx.animationTime;
    shadowFrame.shaderTime = ctx.shaderTime;
    shadowFrame.passMode = 0;
    if (!ctx.shared->terrainRhiPipelines->prepareShadow(
            commandList, *m_resourceMgr, shadowFrame) ||
        !m_worldRenderBuffer->prepareRhiOpaqueAndCutout(
            commandList,
            ctx.shared->terrainRhiPipelines->shadowMetadataLayout())) {
        return false;
    }

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = targets.csmShadowDepthTextureViewHandle(cascade);
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "CsmShadowOpaque";
    renderingInfo.renderArea = {0, 0, m_cascadeResolutions[cascade],
                                m_cascadeResolutions[cascade]};
    renderingInfo.depthStencilAttachment = &depthAttachment;
    commandList.beginRendering(renderingInfo);
    m_worldRenderBuffer->recordRhiOpaque(
        commandList, ctx.shared->terrainRhiPipelines->shadowOpaquePipeline(),
        ctx.shared->terrainRhiPipelines->shadowBindGroup());
    if (renderCutoutCasters) {
        m_worldRenderBuffer->recordRhiCutout(
            commandList, ctx.shared->terrainRhiPipelines->shadowCutoutPipeline(),
            ctx.shared->terrainRhiPipelines->shadowBindGroup());
        commandList.setGraphicsPipeline(
            ctx.shared->terrainRhiPipelines->shadowOpaquePipeline());
    }
    renderShadowBlockEntities(commandList, cascadeData.viewProj);
    renderShadowEntities(commandList, cascadeData.viewProj, ctx.camera.position,
                         cascadeData.splitNear, cascadeData.splitFar);
    renderShadowDrops(commandList, cascadeData.viewProj, ctx.animationTime);
    renderShadowFallingBlocks(commandList, cascadeData.viewProj, ctx.animationTime);
    if (m_shadowStatsActive) {
        m_frameDebugService->markShadowTimestamp(
            commandList, cascade, ShadowTimestampPoint::OpaqueEnd);
    }
    commandList.endRendering();
    if (m_frameDebugService != nullptr) {
        m_frameDebugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}

bool ShadowPass::recordCopyPass(RhiCommandList& commandList, const int cascade) {
    if (!m_graphExecutionBegun || cascade < 0 || cascade >= SHADOW_CASCADE_COUNT) {
        return false;
    }
    const GpuTimerSegmentToken gpuTimer = m_frameDebugService != nullptr
        ? m_frameDebugService->beginGpuTimer(commandList, GpuTimerPass::Shadow)
        : GpuTimerSegmentToken{};
    RhiTextureCopy depthCopy;
    depthCopy.src = m_frameTargets->csmShadowDepthTextureHandle();
    depthCopy.srcSubresource.baseArrayLayer = static_cast<uint32_t>(cascade);
    depthCopy.dst = m_frameTargets->csmShadowDepthAllTextureHandle();
    depthCopy.dstSubresource.baseArrayLayer = static_cast<uint32_t>(cascade);
    depthCopy.extent = {
        m_cascadeResolutions[cascade], m_cascadeResolutions[cascade], 1u};
    commandList.copyTexture(depthCopy);
    if (m_frameDebugService != nullptr) {
        m_frameDebugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}

bool ShadowPass::recordTransparentPass(RhiCommandList& commandList,
                                       const int cascade) {
    if (!m_graphExecutionBegun || cascade < 0 || cascade >= SHADOW_CASCADE_COUNT) {
        return false;
    }
    const FrameContext& ctx = *m_frameContext;
    DeferredRenderTargets& targets = *m_frameTargets;
    const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
    const bool renderTransparentCasters =
        cascade < kTransparentShadowCasterCascadeCount;
    const GpuTimerSegmentToken gpuTimer = m_frameDebugService != nullptr
        ? m_frameDebugService->beginGpuTimer(commandList, GpuTimerPass::Shadow)
        : GpuTimerSegmentToken{};

    m_worldRenderBuffer->resetDrawCommands();
    if (renderTransparentCasters) {
        for (const GpuMeshRange& range : m_cascadeTransparentRanges[cascade]) {
            m_worldRenderBuffer->addTransparent(range);
        }
        ShadowCascadeStats& stats = m_cascadeStats[cascade];
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
                commandList, *m_resourceMgr, shadowFrame) ||
            !m_worldRenderBuffer->prepareRhiTransparent(
                commandList,
                ctx.shared->terrainRhiPipelines->shadowMetadataLayout())) {
            return false;
        }
    }

    RhiColorAttachment colorAttachments[2];
    colorAttachments[0].view = targets.csmShadowColor0TextureViewHandle(cascade);
    colorAttachments[0].loadOp = RhiLoadOp::Clear;
    colorAttachments[0].storeOp = RhiStoreOp::Store;
    colorAttachments[0].clearColor[3] = 1.0f;
    colorAttachments[1].view = targets.csmShadowColor1TextureViewHandle(cascade);
    colorAttachments[1].loadOp = RhiLoadOp::Clear;
    colorAttachments[1].storeOp = RhiStoreOp::Store;
    colorAttachments[1].clearColor[3] = 0.0f;
    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = targets.csmShadowDepthAllTextureViewHandle(cascade);
    depthAttachment.depthLoadOp = RhiLoadOp::Load;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "CsmShadowTransparent";
    renderingInfo.renderArea = {0, 0, m_cascadeResolutions[cascade],
                                m_cascadeResolutions[cascade]};
    renderingInfo.colorAttachments = colorAttachments;
    renderingInfo.colorAttachmentCount = 2u;
    renderingInfo.depthStencilAttachment = &depthAttachment;
    commandList.beginRendering(renderingInfo);
    if (renderTransparentCasters) {
        m_worldRenderBuffer->recordRhiTransparent(
            commandList,
            ctx.shared->terrainRhiPipelines->shadowTransparentPipeline(),
            ctx.shared->terrainRhiPipelines->shadowBindGroup());
        m_cascadeStats[cascade].transparentRendered = true;
    }
    commandList.endRendering();
    if (m_shadowStatsActive) {
        m_frameDebugService->markShadowTimestamp(
            commandList, cascade, ShadowTimestampPoint::End);
    }
    if (m_frameDebugService != nullptr) {
        m_frameDebugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}
