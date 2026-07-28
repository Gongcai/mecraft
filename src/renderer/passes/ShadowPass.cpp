#include "ShadowPass.h"
#include "../debug/RenderDebugService.h"
#include "../targets/DeferredRenderTargets.h"
#include "../shadow/ShadowRenderer.h"
#include "../shadow/ShadowMatrices.h"
#include "../shadow/ShadowCasterCuller.h"
#include "../core/RenderScene.h"
#include "../core/IDeferredGeometryProvider.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../mesh/TerrainRhiPipelineSet.h"
#include "../mesh/TerrainRenderer.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/IWorldView.h"

#include "../renderers/BlockEntityRenderer.h"
#include "../renderers/StaticMeshRenderer.h"
#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../renderers/FallingBlockRenderer.h"
#include "../../world/DropSystem.h"
#include "../../ecs/GameplayRegistry.h"

#include "../rhi/RhiShaderSourceLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <optional>

#include "renderer/core/FrameContext.h"

static constexpr int SHADOW_CASCADE_COUNT = shadow::ShadowRenderer::CASCADE_COUNT;
using ShadowCascadeData = shadow::ShadowRenderer::Cascade;

namespace {
constexpr int kTransparentShadowCasterCascadeCount = 2;
constexpr int kCutoutShadowCasterCascadeCount = SHADOW_CASCADE_COUNT;

[[nodiscard]] RgTextureSubresourceRange cascadeRange(const int cascade) {
    return {0u, 1u, static_cast<uint32_t>(cascade), 1u, 0u};
}

/// Push-constant block of shadow_cull.comp; layout mirrors the shader.
struct ShadowCullPushConstants {
    glm::mat4 viewProj;
    glm::vec4 params0;
    glm::vec4 params1;
};
} // namespace

void ShadowPass::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
}

void ShadowPass::shutdown() {
    if (m_shadowStatsActive && m_frameDebugService != nullptr) {
        m_frameDebugService->cancelShadowFrame();
    }
    destroyCullResources();
    m_resourceMgr = nullptr;
    m_frameContext = nullptr;
    m_frameTargets = nullptr;
    m_frameDebugService = nullptr;
    m_graphFramePrepared = false;
    m_graphExecutionBegun = false;
    m_shadowStatsActive = false;
    m_externalGeometryFrame = false;
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

void ShadowPass::renderShadowStaticMeshes(RhiCommandList& commandList,
                                          const glm::mat4& shadowViewProj) {
    if (m_staticMeshRenderer != nullptr) {
        m_staticMeshRenderer->renderToShadowMap(commandList, shadowViewProj);
    }
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
                                   const IWorldView* worldView) {
    if (m_graphFramePrepared || m_shadowRenderer == nullptr ||
        m_resourceMgr == nullptr || ctx.shared == nullptr ||
        ctx.shared->rhiDevice == nullptr) {
        return false;
    }
    m_externalGeometryFrame =
        ctx.shared->deferredGeometryProvider != nullptr;
    if (!m_externalGeometryFrame &&
        (worldView == nullptr || m_terrainRenderer == nullptr ||
         m_worldRenderBuffer == nullptr ||
         ctx.shared->terrainRhiPipelines == nullptr)) {
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

    // Far cascades cover 46m+ where one frame of staleness is not
    // resolvable, so they update on alternating frames. Abrupt light flips
    // (sun/moon switch, teleports), settings changes, or temporal resets
    // force a full refresh because frozen matrices would no longer match
    // the world the stale shadow map captured.
    const glm::vec3 lightDirection = m_shadowRenderer->lightDirection();
    const bool forceAllCascades =
        !settings.shadow.farCascadeInterleaved || !m_farCascadesPrimed ||
        requiresTemporalReset(ctx.temporalResetReasons) ||
        settings.shadow.resolution != m_lastShadowResolution ||
        settings.shadow.distance != m_lastShadowDistance ||
        glm::dot(lightDirection, m_lastShadowLightDirection) < 0.999f;
    m_cascadeRenderedThisFrame = {true, true, true, true};
    if (!forceAllCascades) {
        const bool evenFrame = (ctx.frameIndex % 2u) == 0u;
        m_cascadeRenderedThisFrame[2] = evenFrame;
        m_cascadeRenderedThisFrame[3] = !evenFrame;
    }
    m_lastShadowLightDirection = lightDirection;
    m_lastShadowResolution = settings.shadow.resolution;
    m_lastShadowDistance = settings.shadow.distance;
    m_farCascadesPrimed = true;

    m_gpuCullEnabledThisFrame =
        !m_externalGeometryFrame && settings.shadow.gpuCascadeCullEnabled;
    m_cullLastRenderedCascade = 0;
    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        if (m_cascadeRenderedThisFrame[static_cast<size_t>(cascade)]) {
            m_cullLastRenderedCascade = cascade;
        }
    }

    m_shadowRenderer->updateFromBasis(basis, shadowSettings,
                                      m_cascadeRenderedThisFrame);

    const float shadowDistance = std::max(64.0f, settings.shadow.distance);
    std::array<CascadeAabbCuller, SHADOW_CASCADE_COUNT> cascadeCullers{};
    float casterCullDistance = shadowDistance;
    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
        const float worldPadding = std::max(16.0f, cascadeData.texelWorldSize * 16.0f);
        cascadeCullers[cascade] = {
            cascadeData.viewProj,
            m_cascadeRenderedThisFrame[static_cast<size_t>(cascade)],
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
    if (!m_externalGeometryFrame) {
        shadowCuller.setup(casterCullDistance, 1.0f, ctx.camera.position);
        shadowCuller.resetCounters();
        m_terrainRenderer->clearTransparentBatches();
        m_terrainRenderer->collectShadowChunks(
            *worldView, ctx.camera.position, shadowDistance, &shadowCuller,
            cascadeCullers, m_cascadeOpaqueRanges, m_cascadeCutoutRanges,
            m_cascadeTransparentRanges);
        m_terrainRenderer->syncTransparentBatches();
        m_visibleTotal = shadowCuller.getVisibleCount();
        m_culledTotal = shadowCuller.getCulledCount();
        m_maxCasterDistance = shadowCuller.getMaxCasterDistance();
    } else {
        m_visibleTotal = 0;
        m_culledTotal = 0;
        m_maxCasterDistance = 0.0f;
    }
    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
        ShadowCascadeStats& stats = m_cascadeStats[cascade];
        stats = {};
        stats.boxVisible = m_externalGeometryFrame
            ? 0 : cascadeCullers[cascade].visibleCount;
        stats.boxCulled = m_externalGeometryFrame
            ? 0 : cascadeCullers[cascade].culledCount;
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
        if (m_externalGeometryFrame) {
            std::snprintf(
                m_cullerLabels[cascade].data(), m_cullerLabels[cascade].size(),
                "Shadow.Cascade%d.ExternalGeometry", cascade);
        } else {
            std::snprintf(
                m_cullerLabels[cascade].data(), m_cullerLabels[cascade].size(),
                "Shadow.Cascade%d.Culler boxVisible=%d boxCulled=%d zCull=%d distanceVisible=%d distanceCulled=%d",
                cascade, cascadeCullers[cascade].visibleCount,
                cascadeCullers[cascade].culledCount,
                cascadeCullers[cascade].useZCulling ? 1 : 0,
                m_visibleTotal, m_culledTotal);
        }
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
        // Frozen far cascades keep last frame's depth layers and matrices.
        // The shadow timer contract still expects every cascade to stamp its
        // timestamp triple each frame, so a minimal pass records the three
        // points back to back (measuring ~0ms) and the stats ring drains.
        if (!m_cascadeRenderedThisFrame[static_cast<size_t>(cascade)]) {
            char frozenName[48];
            std::snprintf(frozenName, sizeof(frozenName),
                          "Shadow.Cascade%d.Frozen", cascade);
            RenderGraphPassBuilder frozen = graph.addPass(
                {frozenName, RgPassType::Copy, RhiQueueType::Graphics});
            frozen.dependsOn(previous)
                .setExecute([this, cascade](RgPassContext& pass) {
                    if (m_shadowStatsActive &&
                        m_frameDebugService != nullptr) {
                        m_frameDebugService->markShadowTimestamp(
                            pass.commandList(), cascade,
                            ShadowTimestampPoint::Start);
                        m_frameDebugService->markShadowTimestamp(
                            pass.commandList(), cascade,
                            ShadowTimestampPoint::OpaqueEnd);
                        m_frameDebugService->markShadowTimestamp(
                            pass.commandList(), cascade,
                            ShadowTimestampPoint::End);
                    }
                    return true;
                });
            previous = frozen.handle();
            continue;
        }
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
    if (!m_externalGeometryFrame) {
        m_worldRenderBuffer->beginFrame();
    }
    m_frameContext = nullptr;
    m_frameTargets = nullptr;
    m_frameDebugService = nullptr;
    m_graphFramePrepared = false;
    m_graphExecutionBegun = false;
    m_shadowStatsActive = false;
    m_externalGeometryFrame = false;
}

bool ShadowPass::recordOpaquePass(RhiCommandList& commandList, const int cascade) {
    if (!m_graphExecutionBegun || cascade < 0 || cascade >= SHADOW_CASCADE_COUNT) {
        return false;
    }
    const FrameContext& ctx = *m_frameContext;
    DeferredRenderTargets& targets = *m_frameTargets;
    const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
    const bool renderCutoutCasters = cascade < kCutoutShadowCasterCascadeCount;

    if (m_externalGeometryFrame) {
        const GpuTimerSegmentToken gpuTimer = m_frameDebugService != nullptr
            ? m_frameDebugService->beginGpuTimer(
                  commandList, GpuTimerPass::Shadow)
            : GpuTimerSegmentToken{};
        if (m_shadowStatsActive) {
            m_frameDebugService->markShadowTimestamp(
                commandList, cascade, ShadowTimestampPoint::Start);
        }
        RhiDepthStencilAttachment depthAttachment;
        depthAttachment.view =
            targets.csmShadowDepthTextureViewHandle(cascade);
        depthAttachment.depthLoadOp = RhiLoadOp::Clear;
        depthAttachment.depthStoreOp = RhiStoreOp::Store;
        depthAttachment.clearDepth = 1.0f;
        RhiRenderingInfo renderingInfo;
        renderingInfo.debugName = "CsmShadowExternalGeometry";
        renderingInfo.renderArea = {
            0, 0, m_cascadeResolutions[cascade],
            m_cascadeResolutions[cascade]};
        renderingInfo.depthStencilAttachment = &depthAttachment;
        commandList.beginRendering(renderingInfo);
        ctx.shared->deferredGeometryProvider->renderToShadowMap(
            commandList, cascadeData.viewProj);
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

    if (cascade == 0) {
        if (m_blockEntityRenderer != nullptr &&
            !m_blockEntityRenderer->prepareFrame(*ctx.worldView)) {
            return false;
        }
        if (m_dropRenderer != nullptr && m_dropSystem != nullptr &&
            !m_dropRenderer->prepareFrame(*ctx.worldView, *m_dropSystem)) {
            return false;
        }
        if (m_humanoidRenderer != nullptr && m_gameplayRegistry != nullptr &&
            !m_humanoidRenderer->prepareFrame(
                *ctx.worldView, *m_gameplayRegistry, HumanoidRenderer::kRenderAll)) {
            return false;
        }
        if (m_fallingBlockRenderer != nullptr && m_gameplayRegistry != nullptr &&
            !m_fallingBlockRenderer->prepareFrame(
                *ctx.worldView, *m_gameplayRegistry)) {
            return false;
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

    if (m_gpuCullEnabledThisFrame) {
        recordCascadeCull(commandList, ctx, cascade, renderCutoutCasters);
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
    renderShadowStaticMeshes(commandList, cascadeData.viewProj);
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

void ShadowPass::recordCascadeCull(RhiCommandList& commandList,
                                   const FrameContext& ctx,
                                   const int cascade,
                                   const bool renderCutoutCasters) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        cascade < 0 || cascade >= SHADOW_CASCADE_COUNT) {
        return;
    }
    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureCullPipeline(rhiDevice)) {
        return;
    }

    const RhiBufferHandle commandBuffers[2] = {
        m_worldRenderBuffer->opaqueIndirectBufferHandle(),
        m_worldRenderBuffer->cutoutIndirectBufferHandle()
    };
    const uint64_t commandCapacities[2] = {
        m_worldRenderBuffer->opaqueIndirectBufferCapacity(),
        m_worldRenderBuffer->cutoutIndirectBufferCapacity()
    };
    const uint32_t commandCounts[2] = {
        static_cast<uint32_t>(m_worldRenderBuffer->opaqueCommandCount()),
        renderCutoutCasters
            ? static_cast<uint32_t>(m_worldRenderBuffer->cutoutCommandCount())
            : 0u
    };

    // Consume the oldest readback slot before this frame's copy overwrites
    // it. Cascade 0 renders every frame, so the read happens exactly once
    // per frame and always before any counter traffic is recorded.
    const uint32_t ringIndex = m_cullRingWriteIndex;
    if (cascade == 0 && m_cullRingWritten[ringIndex]) {
        const void* mapped = rhiDevice.mapBuffer(
            m_cullReadbackBuffers[ringIndex], 0u, sizeof(uint32_t) * 4u);
        if (mapped != nullptr) {
            uint32_t counts[4];
            std::memcpy(counts, mapped, sizeof(counts));
            rhiDevice.unmapBuffer(m_cullReadbackBuffers[ringIndex]);
            m_cullStats.valid = true;
            for (size_t i = 0; i < 4; ++i) {
                m_cullStats.culled[i] = counts[i];
                m_cullStats.total[i] = m_cullTotalsRing[ringIndex][i];
            }
        }
    }

    // Zero this cascade's counter slot ahead of its dispatches; frozen
    // cascades keep the counts from their last rendered frame.
    const uint32_t zero = 0u;
    commandList.bufferBarrier({m_cullCounterBuffer,
                               RhiResourceState::StorageBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(m_cullCounterBuffer,
                             sizeof(uint32_t) * static_cast<uint64_t>(cascade),
                             &zero, sizeof(zero));
    commandList.bufferBarrier({m_cullCounterBuffer,
                               RhiResourceState::TransferDst,
                               RhiResourceState::StorageBuffer});

    const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
    // XY padding only needs to cover vertex animation sway and texel-snap
    // slop; undoing the CPU binning's wide 16-block floor is the point of
    // this pass.
    const float xyPadWorld = std::max(2.0f, cascadeData.texelWorldSize * 8.0f);
    // Z keeps the CPU padding untouched: a depth-range mistake silently
    // drops casters of tall structures, and the Z test culls nothing the
    // CPU pass has not already removed.
    const float zPadWorld = std::max(64.0f, cascadeData.texelWorldSize * 64.0f);
    ShadowCullPushConstants pushConstants{};
    pushConstants.viewProj = cascadeData.viewProj;
    pushConstants.params0 = glm::vec4(
        xyPadWorld / std::max(1.0f, cascadeData.radius),
        zPadWorld / std::max(1.0f, cascadeData.depthExtent),
        0.0f,
        static_cast<float>(cascade));
    pushConstants.params1 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

    for (int slot = 0; slot < 2; ++slot) {
        if (commandCounts[slot] == 0u || !commandBuffers[slot].isValid()) {
            continue;
        }
        if (!ensureCullBindGroup(rhiDevice, slot, commandBuffers[slot],
                                 commandCapacities[slot],
                                 m_worldRenderBuffer->metadataBufferHandle(),
                                 m_worldRenderBuffer->metadataBufferCapacity())) {
            return;
        }
        pushConstants.params0.z = static_cast<float>(commandCounts[slot]);
        commandList.bufferBarrier({commandBuffers[slot],
                                   RhiResourceState::IndirectArgument,
                                   RhiResourceState::StorageBuffer});
        commandList.setComputePipeline(m_cullPipeline);
        commandList.setBindGroup(0u, m_cullBindings[slot].bindGroup);
        commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                                  rhiFlag(RhiShaderStage::Compute));
        commandList.dispatch((commandCounts[slot] + 63u) / 64u, 1u, 1u);
        commandList.bufferBarrier({commandBuffers[slot],
                                   RhiResourceState::StorageBuffer,
                                   RhiResourceState::IndirectArgument});
    }
    m_cullTotals[static_cast<size_t>(cascade)] =
        commandCounts[0] + commandCounts[1];

    // The last rendered cascade ships all four counters into the ring.
    if (cascade == m_cullLastRenderedCascade) {
        commandList.bufferBarrier({m_cullCounterBuffer,
                                   RhiResourceState::StorageBuffer,
                                   RhiResourceState::TransferSrc});
        if (m_cullRingWritten[ringIndex]) {
            commandList.bufferBarrier({m_cullReadbackBuffers[ringIndex],
                                       RhiResourceState::HostRead,
                                       RhiResourceState::TransferDst});
        }
        RhiBufferCopy statsCopy;
        statsCopy.src = m_cullCounterBuffer;
        statsCopy.dst = m_cullReadbackBuffers[ringIndex];
        statsCopy.size = sizeof(uint32_t) * 4u;
        commandList.copyBuffer(statsCopy);
        commandList.bufferBarrier({m_cullReadbackBuffers[ringIndex],
                                   RhiResourceState::TransferDst,
                                   RhiResourceState::HostRead});
        commandList.bufferBarrier({m_cullCounterBuffer,
                                   RhiResourceState::TransferSrc,
                                   RhiResourceState::StorageBuffer});
        m_cullTotalsRing[ringIndex] = m_cullTotals;
        m_cullRingWritten[ringIndex] = true;
        m_cullRingWriteIndex = (ringIndex + 1u) % kCullStatsRingSize;
    }
}

void ShadowPass::recordTransparentCull(RhiCommandList& commandList,
                                       const FrameContext& ctx,
                                       const int cascade) {
    if (ctx.shared == nullptr || ctx.shared->rhiDevice == nullptr ||
        cascade < 0 || cascade >= SHADOW_CASCADE_COUNT) {
        return;
    }
    RhiDevice& rhiDevice = *ctx.shared->rhiDevice;
    if (!ensureCullPipeline(rhiDevice)) {
        return;
    }
    const RhiBufferHandle commandBuffer =
        m_worldRenderBuffer->transparentIndirectBufferHandle();
    const uint32_t commandCount = static_cast<uint32_t>(
        m_worldRenderBuffer->transparentCommandCount());
    if (commandCount == 0u || !commandBuffer.isValid()) {
        return;
    }
    if (!ensureCullBindGroup(rhiDevice, 2, commandBuffer,
                             m_worldRenderBuffer->transparentIndirectBufferCapacity(),
                             m_worldRenderBuffer->metadataBufferHandle(),
                             m_worldRenderBuffer->metadataBufferCapacity())) {
        return;
    }

    // Same padded light-frustum test as the opaque dispatch. The counter
    // slot was zeroed by this cascade's opaque pass and ships to the
    // readback ring only after the last rendered cascade, so transparent
    // counts recorded here are included.
    const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);
    const float xyPadWorld = std::max(2.0f, cascadeData.texelWorldSize * 8.0f);
    const float zPadWorld = std::max(64.0f, cascadeData.texelWorldSize * 64.0f);
    ShadowCullPushConstants pushConstants{};
    pushConstants.viewProj = cascadeData.viewProj;
    pushConstants.params0 = glm::vec4(
        xyPadWorld / std::max(1.0f, cascadeData.radius),
        zPadWorld / std::max(1.0f, cascadeData.depthExtent),
        static_cast<float>(commandCount),
        static_cast<float>(cascade));
    pushConstants.params1 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

    commandList.bufferBarrier({commandBuffer,
                               RhiResourceState::IndirectArgument,
                               RhiResourceState::StorageBuffer});
    commandList.setComputePipeline(m_cullPipeline);
    commandList.setBindGroup(0u, m_cullBindings[2].bindGroup);
    commandList.pushConstants(&pushConstants, sizeof(pushConstants),
                              rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((commandCount + 63u) / 64u, 1u, 1u);
    commandList.bufferBarrier({commandBuffer,
                               RhiResourceState::StorageBuffer,
                               RhiResourceState::IndirectArgument});
    m_cullTotals[static_cast<size_t>(cascade)] += commandCount;
}

bool ShadowPass::ensureCullPipeline(RhiDevice& rhiDevice) {
    if (m_cullRhiDevice != nullptr && m_cullRhiDevice != &rhiDevice) {
        destroyCullResources();
    }
    m_cullRhiDevice = &rhiDevice;
    if (m_cullPipeline.isValid()) {
        return ensureCullStatsBuffers(rhiDevice);
    }

    const std::optional<std::string> source =
        renderer::rhi::loadShaderSource("assets/shaders/shadow_cull.comp");
    if (!source.has_value()) {
        return false;
    }
    RhiShaderDesc shaderDesc;
    shaderDesc.debugName = "Shadow.Cull";
    shaderDesc.stage = RhiShaderStage::Compute;
    shaderDesc.source = source->c_str();
    shaderDesc.sourceSize = source->size();
    m_cullShader = rhiDevice.createShader(shaderDesc);
    if (!m_cullShader.isValid()) {
        return false;
    }

    RhiBindGroupLayoutDesc layoutDesc;
    layoutDesc.debugName = "Shadow.CullBindGroupLayout";
    layoutDesc.entries.push_back({
        0u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u});
    layoutDesc.entries.push_back({
        1u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u});
    layoutDesc.entries.push_back({
        2u, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u});
    m_cullBindGroupLayout = rhiDevice.createBindGroupLayout(layoutDesc);
    if (!m_cullBindGroupLayout.isValid()) {
        return false;
    }

    RhiPipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.debugName = "Shadow.CullPipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts.push_back(m_cullBindGroupLayout);
    pipelineLayoutDesc.pushConstantBytes =
        static_cast<uint32_t>(sizeof(ShadowCullPushConstants));
    pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
    m_cullPipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
    if (!m_cullPipelineLayout.isValid()) {
        return false;
    }

    RhiComputePipelineDesc pipelineDesc;
    pipelineDesc.debugName = "Shadow.CullPipeline";
    pipelineDesc.computeShader = m_cullShader;
    pipelineDesc.layout = m_cullPipelineLayout;
    m_cullPipeline = rhiDevice.createComputePipeline(pipelineDesc);
    if (!m_cullPipeline.isValid()) {
        return false;
    }
    return ensureCullStatsBuffers(rhiDevice);
}

bool ShadowPass::ensureCullStatsBuffers(RhiDevice& rhiDevice) {
    if (!m_cullCounterBuffer.isValid()) {
        RhiBufferDesc counterDesc;
        counterDesc.debugName = "Shadow.CullCounters";
        counterDesc.size = sizeof(uint32_t) * 4u;
        counterDesc.usage = rhiFlag(RhiBufferUsage::Storage) |
                            rhiFlag(RhiBufferUsage::TransferSrc) |
                            rhiFlag(RhiBufferUsage::TransferDst);
        counterDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        counterDesc.initialState = RhiResourceState::StorageBuffer;
        counterDesc.memoryCategory = RhiMemoryCategory::SceneData;
        m_cullCounterBuffer = rhiDevice.createBuffer(counterDesc, nullptr, 0u);
        if (!m_cullCounterBuffer.isValid()) {
            return false;
        }
    }
    for (uint32_t slot = 0u; slot < kCullStatsRingSize; ++slot) {
        if (m_cullReadbackBuffers[slot].isValid()) {
            continue;
        }
        // Mappable readback contract: MapRead usage with a TransferDst
        // resting state (HostRead is entered only after each copy).
        RhiBufferDesc readbackDesc;
        readbackDesc.debugName = "Shadow.CullReadback";
        readbackDesc.size = sizeof(uint32_t) * 4u;
        readbackDesc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                             rhiFlag(RhiBufferUsage::MapRead);
        readbackDesc.memoryUsage = RhiMemoryUsage::GpuToCpu;
        readbackDesc.initialState = RhiResourceState::TransferDst;
        readbackDesc.memoryCategory = RhiMemoryCategory::Readback;
        m_cullReadbackBuffers[slot] = rhiDevice.createBuffer(readbackDesc, nullptr, 0u);
        if (!m_cullReadbackBuffers[slot].isValid()) {
            return false;
        }
    }
    return true;
}

bool ShadowPass::ensureCullBindGroup(RhiDevice& rhiDevice,
                                     const int slot,
                                     const RhiBufferHandle commandBuffer,
                                     const uint64_t commandCapacity,
                                     const RhiBufferHandle metadataBuffer,
                                     const uint64_t metadataCapacity) {
    if (!commandBuffer.isValid() || !metadataBuffer.isValid() ||
        slot < 0 || slot >= 3) {
        return false;
    }
    CullBinding& binding = m_cullBindings[static_cast<size_t>(slot)];
    if (binding.bindGroup.isValid() &&
        binding.boundCommands.index == commandBuffer.index &&
        binding.boundCommands.generation == commandBuffer.generation &&
        binding.boundMetadata.index == metadataBuffer.index &&
        binding.boundMetadata.generation == metadataBuffer.generation) {
        return true;
    }
    if (binding.bindGroup.isValid()) {
        rhiDevice.destroyBindGroup(binding.bindGroup);
        binding = {};
    }

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_cullBindGroupLayout;
    RhiBindGroupEntry commandsEntry;
    commandsEntry.binding = 0u;
    commandsEntry.resource.buffer.buffer = commandBuffer;
    commandsEntry.resource.buffer.offset = 0u;
    commandsEntry.resource.buffer.range = commandCapacity;
    bindGroupDesc.entries.push_back(commandsEntry);
    RhiBindGroupEntry metadataEntry;
    metadataEntry.binding = 1u;
    metadataEntry.resource.buffer.buffer = metadataBuffer;
    metadataEntry.resource.buffer.offset = 0u;
    metadataEntry.resource.buffer.range = metadataCapacity;
    bindGroupDesc.entries.push_back(metadataEntry);
    RhiBindGroupEntry statsEntry;
    statsEntry.binding = 2u;
    statsEntry.resource.buffer.buffer = m_cullCounterBuffer;
    statsEntry.resource.buffer.offset = 0u;
    statsEntry.resource.buffer.range = sizeof(uint32_t) * 4u;
    bindGroupDesc.entries.push_back(statsEntry);

    binding.bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!binding.bindGroup.isValid()) {
        binding = {};
        return false;
    }
    binding.boundCommands = commandBuffer;
    binding.boundMetadata = metadataBuffer;
    return true;
}

void ShadowPass::destroyCullResources() {
    if (m_cullRhiDevice != nullptr) {
        for (CullBinding& binding : m_cullBindings) {
            if (binding.bindGroup.isValid()) {
                m_cullRhiDevice->destroyBindGroup(binding.bindGroup);
            }
            binding = {};
        }
        if (m_cullPipeline.isValid()) {
            m_cullRhiDevice->destroyPipeline(m_cullPipeline);
        }
        if (m_cullPipelineLayout.isValid()) {
            m_cullRhiDevice->destroyPipelineLayout(m_cullPipelineLayout);
        }
        if (m_cullBindGroupLayout.isValid()) {
            m_cullRhiDevice->destroyBindGroupLayout(m_cullBindGroupLayout);
        }
        if (m_cullShader.isValid()) {
            m_cullRhiDevice->destroyShader(m_cullShader);
        }
        if (m_cullCounterBuffer.isValid()) {
            m_cullRhiDevice->destroyBuffer(m_cullCounterBuffer);
        }
        for (RhiBufferHandle& buffer : m_cullReadbackBuffers) {
            if (buffer.isValid()) {
                m_cullRhiDevice->destroyBuffer(buffer);
            }
            buffer = {};
        }
    }
    m_cullPipeline = {};
    m_cullPipelineLayout = {};
    m_cullBindGroupLayout = {};
    m_cullShader = {};
    m_cullCounterBuffer = {};
    for (bool& written : m_cullRingWritten) {
        written = false;
    }
    m_cullRingWriteIndex = 0u;
    m_cullTotals = {};
    m_cullStats = {};
    m_cullRhiDevice = nullptr;
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

    if (!m_externalGeometryFrame) {
        m_worldRenderBuffer->resetDrawCommands();
    }
    if (!m_externalGeometryFrame && renderTransparentCasters) {
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
        if (m_gpuCullEnabledThisFrame) {
            recordTransparentCull(commandList, ctx, cascade);
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
    if (!m_externalGeometryFrame && renderTransparentCasters) {
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
