#include "ForwardPipeline.h"
#include "RenderScene.h"
#include "../mesh/TerrainRenderCache.h"
#include "../mesh/TerrainRhiPipelineSet.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../renderers/GameplaySkyRenderer.h"
#include "../renderers/BlockEntityRenderer.h"
#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../engine/camera/Camera.h"
#include "../../engine/platform/Window.h"
#include "../../world/IWorldView.h"
#include "../../world/DayNightSystem.h"
#include "../../particle/ParticleSystem.h"

#include <glad/glad.h>
#include <algorithm>

ForwardPipeline::ForwardPipeline() = default;
ForwardPipeline::~ForwardPipeline() = default;

void ForwardPipeline::init(SharedRenderResources& shared) {
    m_shared = &shared;
    m_terrainRenderer = shared.terrain;
    m_terrainCache = shared.terrainCache;
    m_worldRenderBuffer = shared.worldRenderBuffer;
    m_skyRenderer = shared.sky;
    m_resourceMgr = shared.resources;

    // Enable forward vanilla shaders on sub-renderers (no deferred/shaderpack contracts)
    if (shared.sky) shared.sky->setForwardMode(true);
    if (shared.dropRenderer) shared.dropRenderer->setForwardMode(true);

    m_initialized = true;
}

void ForwardPipeline::shutdown() {
    // Revert sub-renderers to deferred shaders
    if (m_shared) {
        if (m_shared->sky) m_shared->sky->setForwardMode(false);
        if (m_shared->dropRenderer) m_shared->dropRenderer->setForwardMode(false);
    }

    m_shared = nullptr;
    m_terrainRenderer = nullptr;
    m_terrainCache = nullptr;
    m_worldRenderBuffer = nullptr;
    m_skyRenderer = nullptr;
    m_resourceMgr = nullptr;
    m_backbufferCommandList = nullptr;
    m_transparentBatch.clear();
    m_transparentPassPlan = {};
    m_initialized = false;
}

FrameOutput ForwardPipeline::renderFrame(const FrameContext& ctx, const RenderSettings& settings) {
    if (!m_initialized || !ctx.worldView || m_shared == nullptr ||
        m_shared->rhiDevice == nullptr) {
        return {};
    }

    RhiCommandList& commandList = m_shared->rhiDevice->beginFrame();
    m_backbufferCommandList = &commandList;
    if (!prepareTerrain(ctx, commandList)) {
        m_shared->rhiDevice->submitFrame(commandList);
        m_backbufferCommandList = nullptr;
        return {};
    }
    if (settings.weather.particlesEnabled && m_shared->particleSystem) {
        m_shared->particleSystem->prepareFrame(ctx.camera.view, commandList);
    }
    if (m_shared->blockEntityRenderer != nullptr) {
        m_shared->blockEntityRenderer->prepareFrame(*ctx.worldView);
    }
    if (!beginBackbufferFrame(ctx)) {
        m_shared->rhiDevice->submitFrame(commandList);
        m_backbufferCommandList = nullptr;
        return {};
    }

    // 1. Sky background (sun, moon, clouds, gradient)
    renderSky(ctx);
    if (!beginBackbufferScenePass(ctx)) {
        endBackbufferFrame();
        return {};
    }

    // 2. Opaque + cutout terrain
    renderTerrain();

    // 3. Forward-only scene objects that should depth-test against terrain.
    renderEntitiesAndParticles(ctx, settings);

    // 4. Transparent terrain (water, glass, etc.)
    renderTransparent();

    endBackbufferFrame();

    // 5. Build and return frame output
    return buildFrameOutput(ctx);
}

bool ForwardPipeline::beginBackbufferFrame(const FrameContext& ctx) {
    if (m_shared == nullptr || m_shared->rhiDevice == nullptr ||
        m_backbufferCommandList == nullptr ||
        !ctx.swapchainColorView.isValid() || !ctx.swapchainDepthStencilView.isValid()) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = ctx.swapchainColorView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = ctx.swapchainDepthStencilView;
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "ForwardBackbuffer";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, ctx.frameWidth)),
        static_cast<uint32_t>(std::max(1, ctx.frameHeight))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    m_backbufferCommandList->beginRendering(renderingInfo);
    m_backbufferCommandList->setViewport({
        0.0f,
        0.0f,
        static_cast<float>(std::max(1, ctx.frameWidth)),
        static_cast<float>(std::max(1, ctx.frameHeight)),
        0.0f,
        1.0f
    });

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    return true;
}

bool ForwardPipeline::beginBackbufferScenePass(const FrameContext& ctx) {
    if (m_backbufferCommandList == nullptr ||
        !ctx.swapchainColorView.isValid() || !ctx.swapchainDepthStencilView.isValid()) {
        return false;
    }

    m_backbufferCommandList->endRendering();

    RhiColorAttachment colorAttachment;
    colorAttachment.view = ctx.swapchainColorView;
    colorAttachment.loadOp = RhiLoadOp::Load;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = ctx.swapchainDepthStencilView;
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "ForwardSceneBackbuffer";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, ctx.frameWidth)),
        static_cast<uint32_t>(std::max(1, ctx.frameHeight))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    m_backbufferCommandList->beginRendering(renderingInfo);
    m_backbufferCommandList->setViewport({
        0.0f,
        0.0f,
        static_cast<float>(std::max(1, ctx.frameWidth)),
        static_cast<float>(std::max(1, ctx.frameHeight)),
        0.0f,
        1.0f
    });
    return true;
}

void ForwardPipeline::endBackbufferFrame() {
    if (m_backbufferCommandList == nullptr || m_shared == nullptr || m_shared->rhiDevice == nullptr) {
        return;
    }

    m_backbufferCommandList->endRendering();
    m_shared->rhiDevice->submitFrame(*m_backbufferCommandList);
    m_backbufferCommandList = nullptr;
}

// ============================================================================
// Sky rendering
// ============================================================================

void ForwardPipeline::renderSky(const FrameContext& ctx) {
    if (!m_skyRenderer || !ctx.dayNightSystem || !ctx.cameraPtr) return;

    const auto& dayNight = *ctx.dayNightSystem;
    const float aspect = (ctx.frameHeight > 0)
        ? static_cast<float>(ctx.frameWidth) / static_cast<float>(ctx.frameHeight)
        : 1.0f;

    // Forward mode: pass 0 for skyCaptureTexture because sky gradient mode doesn't read it.
    m_skyRenderer->render(*ctx.cameraPtr, aspect, dayNight, 0);
}

// ============================================================================
// Terrain rendering (opaque + cutout)
// ============================================================================

bool ForwardPipeline::prepareTerrain(const FrameContext& ctx,
                                     RhiCommandList& commandList) {
    if (!m_shared || !m_shared->terrainRhiPipelines || !m_terrainRenderer ||
        !m_resourceMgr || !m_worldRenderBuffer) {
        return false;
    }

    auto& terrain = *m_terrainRenderer;
    auto& worldBuffer = *m_worldRenderBuffer;

    if (m_terrainCache) {
        m_terrainCache->releaseStaleMdiAllocations(*ctx.worldView);
        m_terrainCache->drainMeshingResults(*ctx.worldView, commandList);
    }
    worldBuffer.beginFrame();
    terrain.clearTransparentBatches();

    TerrainFrameData tfd = buildTerrainFrameData(ctx);
    terrain.setCameraPos(ctx.camera.position);
    terrain.updateFrustum(ctx.camera.viewProj);

    if (m_terrainCache) {
        m_terrainCache->submitMeshingJobs(*ctx.worldView, ctx.camera.position);
    }

    terrain.renderOpaqueChunksAndCollectPasses(*ctx.worldView, true);
    terrain.syncTransparentBatches();

    m_transparentBatch = terrain.transparentBatches();
    m_transparentPassPlan = terrain.transparentPassPlan();
    std::sort(m_transparentBatch.begin(), m_transparentBatch.end(),
              [](const DrawBatchEntry& lhs, const DrawBatchEntry& rhs) {
                  return lhs.distanceSq > rhs.distanceSq;
              });
    for (const DrawBatchEntry& entry : m_transparentBatch) {
        worldBuffer.addTransparent(entry.range);
    }

    if (!m_shared->terrainRhiPipelines->prepareForward(
            commandList,
            *m_resourceMgr,
            tfd) ||
        !worldBuffer.prepareRhiOpaqueAndCutout(
            commandList,
            m_shared->terrainRhiPipelines->forwardMetadataLayout()) ||
        !worldBuffer.prepareRhiTransparent(
            commandList,
            m_shared->terrainRhiPipelines->forwardMetadataLayout())) {
        return false;
    }
    return true;
}

void ForwardPipeline::renderTerrain() {
    if (m_backbufferCommandList == nullptr || m_shared == nullptr ||
        m_shared->terrainRhiPipelines == nullptr || m_worldRenderBuffer == nullptr) {
        return;
    }

    m_worldRenderBuffer->recordRhiOpaque(
        *m_backbufferCommandList,
        m_shared->terrainRhiPipelines->forwardOpaquePipeline(),
        m_shared->terrainRhiPipelines->forwardOpaqueBindGroup());
    m_worldRenderBuffer->recordRhiCutout(
        *m_backbufferCommandList,
        m_shared->terrainRhiPipelines->forwardCutoutPipeline(),
        m_shared->terrainRhiPipelines->forwardCutoutBindGroup());
    m_worldRenderBuffer->captureSceneFrameStats();
}

void ForwardPipeline::renderEntitiesAndParticles(const FrameContext& ctx, const RenderSettings& settings) {
    if (!m_shared || !ctx.cameraPtr || !ctx.windowPtr) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    if (m_shared->blockEntityRenderer) {
        m_shared->blockEntityRenderer->renderForward(*ctx.worldView, ctx.camera.viewProj, ctx.skyIntensity);
    }

    if (m_shared->dropRenderer && m_shared->dropSystem) {
        m_shared->dropRenderer->render(*m_shared->dropSystem, *ctx.cameraPtr, *ctx.windowPtr);
    }

    if (m_shared->humanoidRenderer && m_shared->gameplayRegistry) {
        const auto mode = ctx.renderLocalPlayerModel
            ? HumanoidRenderer::kRenderAll
            : HumanoidRenderer::kRenderMobsOnly;
        m_shared->humanoidRenderer->render(*m_shared->gameplayRegistry, *ctx.cameraPtr, *ctx.windowPtr, mode);
    }

    if (settings.weather.particlesEnabled && m_shared->particleSystem) {
        m_shared->particleSystem->render(*m_backbufferCommandList, ctx.camera.projection * ctx.camera.view);
    }
}

// ============================================================================
// Transparent rendering (water, glass, etc.)
// ============================================================================

void ForwardPipeline::renderTransparent() {
    if (!m_transparentPassPlan.hasAny() || m_backbufferCommandList == nullptr ||
        m_shared == nullptr || m_shared->terrainRhiPipelines == nullptr ||
        m_worldRenderBuffer == nullptr) {
        return;
    }

    m_worldRenderBuffer->recordRhiTransparent(
        *m_backbufferCommandList,
        m_shared->terrainRhiPipelines->forwardTransparentPipeline(),
        m_shared->terrainRhiPipelines->forwardTransparentBindGroup());
}

// ============================================================================
// Frame output
// ============================================================================

FrameOutput ForwardPipeline::buildFrameOutput(const FrameContext&) {
    FrameOutput output{};
    // Forward vanilla: no deferred inputs, skip post-process (bloom/exposure/grading)
    output.hasDeferredInputs = false;
    output.hasDebugView = false;
    output.skipPostProcess = true;
    return output;
}

// ============================================================================
// Data conversion helpers
// ============================================================================

TerrainFrameData ForwardPipeline::buildTerrainFrameData(const FrameContext& ctx) {
    TerrainFrameData tfd{};
    // Camera
    tfd.view = ctx.camera.view;
    tfd.viewProj = ctx.camera.viewProj;
    tfd.cameraPos = ctx.camera.position;
    tfd.animationTime = ctx.animationTime;
    tfd.shaderTime = ctx.shaderTime;

    // Fog (used by forward_basic_terrain.frag)
    tfd.fog.enabled = ctx.fog.enabled;
    tfd.fog.mode = ctx.fog.mode;
    tfd.fog.color = ctx.fog.color;
    tfd.fog.start = ctx.fog.startDistance;
    tfd.fog.end = ctx.fog.endDistance;
    tfd.fog.density = ctx.fog.density;

    // Only skyIntensity needed for day/night lightmap interpolation
    tfd.skyLighting.skyIntensity = ctx.skyIntensity;

    return tfd;
}
