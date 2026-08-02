#include "ForwardPipeline.h"
#include "../../Diagnostics.h"
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

#include <algorithm>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

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

    m_initialized = true;
}

void ForwardPipeline::shutdown() {
    if (m_shared != nullptr && m_shared->rhiDevice != nullptr) {
        m_renderGraph.releaseTransientResources(*m_shared->rhiDevice);
    }

    // Revert sub-renderers to deferred shaders
    if (m_shared) {
    }

    m_shared = nullptr;
    m_terrainRenderer = nullptr;
    m_terrainCache = nullptr;
    m_worldRenderBuffer = nullptr;
    m_skyRenderer = nullptr;
    m_resourceMgr = nullptr;
    m_transparentBatch.clear();
    m_transparentPassPlan = {};
    m_initialized = false;
}

FrameOutput ForwardPipeline::renderFrame(const FrameContext& ctx, const RenderSettings& settings) {
    if (!m_initialized || !ctx.worldView || m_shared == nullptr || m_shared->rhiDevice == nullptr ||
        m_shared->commandListPool == nullptr) {
        return {};
    }

    if (!executeFrameGraph(ctx, settings)) {
        return {};
    }
    return buildFrameOutput(ctx);
}

bool ForwardPipeline::executeFrameGraph(const FrameContext& ctx, const RenderSettings& settings) {
    if (m_shared == nullptr || m_shared->rhiDevice == nullptr || m_shared->commandListPool == nullptr ||
        !ctx.sceneCaptureColorTexture.isValid() || !ctx.sceneCaptureDepthTexture.isValid() ||
        !ctx.sceneCaptureColorView.isValid() || !ctx.sceneCaptureDepthView.isValid()) {
        return false;
    }

    RhiDevice& rhiDevice = *m_shared->rhiDevice;
    if (!prepareSceneTlas()) {
        return false;
    }
    m_renderGraph.reset();

    const auto importTexture = [&](const RhiTextureHandle texture, const RhiTextureViewHandle view,
                                   const RhiResourceState stableState, RgTextureHandle& graphTexture) {
        RhiTextureDesc desc;
        if (!rhiDevice.getTextureDesc(texture, desc)) {
            return false;
        }
        RgImportedTextureDesc imported;
        imported.name = desc.debugName;
        imported.texture = texture;
        imported.desc = desc;
        imported.initialState = stableState;
        imported.finalState = stableState;
        imported.defaultView = view;
        graphTexture = m_renderGraph.importTexture(imported);
        return graphTexture.isValid();
    };

    RgTextureHandle sceneColor;
    RgTextureHandle sceneDepth;
    if (!importTexture(ctx.sceneCaptureColorTexture, ctx.sceneCaptureColorView, RhiResourceState::ShaderRead,
                       sceneColor) ||
        !importTexture(ctx.sceneCaptureDepthTexture, ctx.sceneCaptureDepthView, RhiResourceState::DepthRead,
                       sceneDepth)) {
        return false;
    }

    RenderGraphPassBuilder sceneTlas =
        m_renderGraph.addPass({"Forward.SceneTLAS", RgPassType::Graphics, RhiQueueType::Graphics});
    sceneTlas.setExecute([&](RgPassContext& pass) {
        return m_shared->sceneTlasCache == nullptr || m_shared->sceneTlasCache->recordFrame(pass.commandList());
    });

    RenderGraphPassBuilder prepare =
        m_renderGraph.addPass({"Forward.Prepare", RgPassType::Graphics, RhiQueueType::Graphics});
    prepare.dependsOn(sceneTlas.handle()).setExecute([&](RgPassContext& pass) {
        return prepareGraphFrame(ctx, settings, pass.commandList());
    });

    RenderGraphPassBuilder sky = m_renderGraph.addPass({"Forward.Sky", RgPassType::Graphics, RhiQueueType::Graphics});
    sky.dependsOn(prepare.handle())
        .writeTexture(sceneColor, RhiResourceState::RenderTarget)
        .writeTexture(sceneDepth, RhiResourceState::DepthWrite)
        .setExecute([&](RgPassContext& pass) { return recordSkyPass(ctx, pass.commandList()); });

    RenderGraphPassBuilder scene =
        m_renderGraph.addPass({"Forward.Scene", RgPassType::Graphics, RhiQueueType::Graphics});
    scene.dependsOn(sky.handle())
        .readWriteTexture(sceneColor, RhiResourceState::RenderTarget)
        .writeTexture(sceneDepth, RhiResourceState::DepthWrite)
        .setExecute([&](RgPassContext& pass) { return recordScenePass(ctx, settings, pass.commandList()); });

    const RgCompileResult compiled = m_renderGraph.compile();
    if (!compiled.succeeded()) {
        MECRAFT_LOG_STREAM(std::cerr << "[ForwardPipeline] Render Graph compile failed: " << compiled.message << '\n');
        return false;
    }
    const RgExecuteResult executed = m_renderGraph.execute(rhiDevice, *m_shared->commandListPool);
    if (m_terrainCache != nullptr) {
        m_terrainCache->finishGraphExecution(executed.succeeded(), executed.completionToken());
    }
    if (m_shared->sceneTlasCache != nullptr) {
        m_shared->sceneTlasCache->finishGraphExecution(executed.succeeded(), executed.completionToken());
    }
    if (!executed.succeeded()) {
        MECRAFT_LOG_STREAM(std::cerr << "[ForwardPipeline] Render Graph execution failed: " << executed.message
                                     << '\n');
        return false;
    }
    return true;
}

bool ForwardPipeline::prepareSceneTlas() {
    if (m_shared == nullptr || m_shared->sceneTlasCache == nullptr) {
        return true;
    }
    renderer::rt::SceneTlasCache& cache = *m_shared->sceneTlasCache;
    if (!cache.supported()) {
        return true;
    }
    if (!cache.healthy()) {
        MECRAFT_LOG_STREAM(std::cerr << "[ForwardPipeline] " << cache.lastError() << '\n');
        return false;
    }

    std::vector<renderer::rt::SceneTlasInstanceInput> instances;
    if (m_terrainCache != nullptr) {
        const std::vector<TerrainBlasView> terrainViews = m_terrainCache->blasCache().activeViews();
        instances.reserve(terrainViews.size());
        for (const TerrainBlasView& terrain : terrainViews) {
            uint8_t mask = renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::ShadowCaster) |
                           renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::ReflectionVisible);
            if (terrain.opaqueVertexCount != 0u) {
                mask |= renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiOpaque);
            }
            if (terrain.cutoutVertexCount != 0u) {
                mask |= renderer::rt::sceneTlasMaskBit(renderer::rt::SceneTlasInstanceMask::GiCutout);
            }
            instances.push_back({{renderer::rt::SceneTlasInstanceKind::Terrain, terrain.key.chunkKey,
                                  static_cast<int64_t>(terrain.key.scy)},
                                 terrain.resource,
                                 glm::translate(glm::mat4(1.0f), terrain.worldOrigin),
                                 mask,
                                 false});
        }
    }
    const renderer::rt::SceneTlasSetResult result = cache.setInstances(std::move(instances));
    switch (result) {
    case renderer::rt::SceneTlasSetResult::Accepted:
    case renderer::rt::SceneTlasSetResult::Unchanged:
    case renderer::rt::SceneTlasSetResult::Unsupported: return true;
    case renderer::rt::SceneTlasSetResult::InvalidInstance:
        MECRAFT_LOG_STREAM(std::cerr << "[ForwardPipeline] " << cache.lastError() << '\n');
        return false;
    }
    return false;
}

bool ForwardPipeline::prepareGraphFrame(const FrameContext& ctx, const RenderSettings& settings,
                                        RhiCommandList& commandList) {
    if (!prepareTerrain(ctx, commandList)) {
        return false;
    }
    if (settings.weather.particlesEnabled && m_shared->particleSystem != nullptr) {
        m_shared->particleSystem->prepareFrame(ctx.camera.view, commandList);
    }
    if (m_shared->blockEntityRenderer != nullptr) {
        if (!m_shared->blockEntityRenderer->prepareFrame(*ctx.worldView)) {
            return false;
        }
        if (!m_shared->blockEntityRenderer->prepareForward(commandList)) {
            return false;
        }
    }
    if (m_shared->dropRenderer != nullptr && m_shared->dropSystem != nullptr &&
        !m_shared->dropRenderer->prepareFrame(*ctx.worldView, *m_shared->dropSystem)) {
        return false;
    }
    if (m_shared->humanoidRenderer != nullptr && m_shared->gameplayRegistry != nullptr) {
        const HumanoidRenderer::RenderMode mode =
            ctx.renderLocalPlayerModel ? HumanoidRenderer::kRenderAll : HumanoidRenderer::kRenderMobsOnly;
        if (!m_shared->humanoidRenderer->prepareFrame(*ctx.worldView, *m_shared->gameplayRegistry, mode)) {
            return false;
        }
    }
    return true;
}

bool ForwardPipeline::recordSkyPass(const FrameContext& ctx, RhiCommandList& commandList) {
    if (m_skyRenderer == nullptr || ctx.dayNightSystem == nullptr || ctx.cameraPtr == nullptr ||
        !ctx.sceneCaptureColorView.isValid() || !ctx.sceneCaptureDepthView.isValid()) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = ctx.sceneCaptureColorView;
    colorAttachment.loadOp = RhiLoadOp::Clear;
    colorAttachment.storeOp = RhiStoreOp::Store;
    colorAttachment.clearColor[0] = 0.0f;
    colorAttachment.clearColor[1] = 0.0f;
    colorAttachment.clearColor[2] = 0.0f;
    colorAttachment.clearColor[3] = 1.0f;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = ctx.sceneCaptureDepthView;
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "ForwardBackbuffer";
    renderingInfo.renderArea = {0, 0, ctx.temporalExtents.renderExtent.width, ctx.temporalExtents.renderExtent.height};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    commandList.beginRendering(renderingInfo);
    commandList.setViewport({0.0f, 0.0f, static_cast<float>(ctx.temporalExtents.renderExtent.width),
                             static_cast<float>(ctx.temporalExtents.renderExtent.height), 0.0f, 1.0f});
    renderSky(ctx, commandList);
    commandList.endRendering();
    return true;
}

bool ForwardPipeline::recordScenePass(const FrameContext& ctx, const RenderSettings& settings,
                                      RhiCommandList& commandList) {
    if (!ctx.sceneCaptureColorView.isValid() || !ctx.sceneCaptureDepthView.isValid()) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = ctx.sceneCaptureColorView;
    colorAttachment.loadOp = RhiLoadOp::Load;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = ctx.sceneCaptureDepthView;
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "ForwardSceneBackbuffer";
    renderingInfo.renderArea = {0, 0, ctx.temporalExtents.renderExtent.width, ctx.temporalExtents.renderExtent.height};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    commandList.beginRendering(renderingInfo);
    commandList.setViewport({0.0f, 0.0f, static_cast<float>(ctx.temporalExtents.renderExtent.width),
                             static_cast<float>(ctx.temporalExtents.renderExtent.height), 0.0f, 1.0f});
    renderTerrain(commandList);
    renderEntitiesAndParticles(ctx, settings, commandList);
    renderTransparent(commandList);
    commandList.endRendering();
    return true;
}

// ============================================================================
// Sky rendering
// ============================================================================

void ForwardPipeline::renderSky(const FrameContext& ctx, RhiCommandList& commandList) {
    if (!m_skyRenderer || !ctx.dayNightSystem || !ctx.cameraPtr)
        return;

    const auto& dayNight = *ctx.dayNightSystem;
    const float aspect = static_cast<float>(ctx.temporalExtents.renderExtent.width) /
                         static_cast<float>(ctx.temporalExtents.renderExtent.height);

    m_skyRenderer->render(*ctx.cameraPtr, aspect, dayNight, commandList);
}

// ============================================================================
// Terrain rendering (opaque + cutout)
// ============================================================================

bool ForwardPipeline::prepareTerrain(const FrameContext& ctx, RhiCommandList& commandList) {
    if (!m_shared || !m_shared->terrainRhiPipelines || !m_terrainRenderer || !m_resourceMgr || !m_worldRenderBuffer) {
        return false;
    }

    auto& terrain = *m_terrainRenderer;
    auto& worldBuffer = *m_worldRenderBuffer;

    if (m_terrainCache) {
        m_terrainCache->releaseStaleMdiAllocations(*ctx.worldView);
        if (!m_terrainCache->drainMeshingResults(*ctx.worldView, commandList)) {
            MECRAFT_LOG_STREAM(std::cerr << "[ForwardPipeline] " << m_terrainCache->blasCache().lastError() << '\n');
            return false;
        }
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
              [](const DrawBatchEntry& lhs, const DrawBatchEntry& rhs) { return lhs.distanceSq > rhs.distanceSq; });
    for (const DrawBatchEntry& entry : m_transparentBatch) {
        worldBuffer.addTransparent(entry.range);
    }

    if (!m_shared->terrainRhiPipelines->prepareForward(commandList, *m_resourceMgr, tfd) ||
        !worldBuffer.prepareRhiOpaqueAndCutout(commandList, m_shared->terrainRhiPipelines->forwardMetadataLayout()) ||
        !worldBuffer.prepareRhiTransparent(commandList, m_shared->terrainRhiPipelines->forwardMetadataLayout())) {
        return false;
    }
    return true;
}

void ForwardPipeline::renderTerrain(RhiCommandList& commandList) {
    if (m_shared == nullptr || m_shared->terrainRhiPipelines == nullptr || m_worldRenderBuffer == nullptr) {
        return;
    }

    m_worldRenderBuffer->recordRhiOpaque(commandList, m_shared->terrainRhiPipelines->forwardOpaquePipeline(),
                                         m_shared->terrainRhiPipelines->forwardOpaqueBindGroup());
    m_worldRenderBuffer->recordRhiCutout(commandList, m_shared->terrainRhiPipelines->forwardCutoutPipeline(),
                                         m_shared->terrainRhiPipelines->forwardCutoutBindGroup());
    m_worldRenderBuffer->captureSceneFrameStats();
}

void ForwardPipeline::renderEntitiesAndParticles(const FrameContext& ctx, const RenderSettings& settings,
                                                 RhiCommandList& commandList) {
    if (!m_shared || !ctx.cameraPtr || !ctx.windowPtr) {
        return;
    }

    if (m_shared->blockEntityRenderer) {
        m_shared->blockEntityRenderer->renderForward(commandList, ctx.camera.viewProj, ctx.skyIntensity);
    }

    if (m_shared->dropRenderer && m_shared->dropSystem) {
        m_shared->dropRenderer->renderForward(commandList, ctx.camera.viewProj, ctx.skyIntensity, ctx.animationTime);
    }

    if (m_shared->humanoidRenderer && m_shared->gameplayRegistry) {
        m_shared->humanoidRenderer->renderPreparedForward(commandList, ctx.camera.viewProj, ctx.skyIntensity);
        m_shared->humanoidRenderer->finishFrame();
    }

    if (settings.weather.particlesEnabled && m_shared->particleSystem) {
        m_shared->particleSystem->render(commandList, ctx.camera.projection * ctx.camera.view);
    }
}

// ============================================================================
// Transparent rendering (water, glass, etc.)
// ============================================================================

void ForwardPipeline::renderTransparent(RhiCommandList& commandList) {
    if (!m_transparentPassPlan.hasAny() || m_shared == nullptr || m_shared->terrainRhiPipelines == nullptr ||
        m_worldRenderBuffer == nullptr) {
        return;
    }

    m_worldRenderBuffer->recordRhiTransparent(commandList, m_shared->terrainRhiPipelines->forwardTransparentPipeline(),
                                              m_shared->terrainRhiPipelines->forwardTransparentBindGroup());
}

// ============================================================================
// Frame output
// ============================================================================

FrameOutput ForwardPipeline::buildFrameOutput(const FrameContext& ctx) {
    FrameOutput output{};
    output.sceneColor = ctx.sceneCaptureColorTexture;
    output.sceneDepth = ctx.sceneCaptureDepthTexture;
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
