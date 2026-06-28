#include "ShadowPass.h"
#include "../../Diagnostics.h"
#include "../debug/RenderDebugLabels.h"
#include "../debug/RenderDebugService.h"
#include "../targets/DeferredRenderTargets.h"
#include "../shadow/ShadowRenderer.h"
#include "../shadow/ShadowMatrices.h"
#include "../shadow/ShadowCasterCuller.h"
#include "../mesh/TerrainRenderer.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../core/Shader.h"
#include "../renderers/GameplaySkyRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/IWorldView.h"
#include "../../world/World.h"
#include "../../world/chunk/Chunk.h"
#include "../../world/chunk/SubChunk.h"
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
    m_shadowDepthShader = resourceMgr.getShader("shadow_depth");
    m_entityShadowShader = resourceMgr.getShader("entity_shadow");
}

void ShadowPass::shutdown() {
    m_shadowDepthShader = nullptr;
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
    const TransparentPassPlan& preservedTransparentPlan,
    bool useMultiDrawIndirect) {
    ShadowPassOutput output;
    output.transparentBatch = preservedTransparentBatch;
    output.transparentPlan = preservedTransparentPlan;

    if (m_shadowDepthShader == nullptr ||
        m_shadowRenderer == nullptr ||
        m_terrainRenderer == nullptr ||
        m_worldRenderBuffer == nullptr ||
        m_resourceMgr == nullptr) {
        return output;
    }

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

    m_shadowDepthShader->use();
    m_shadowDepthShader->setInt("uUseModel", 0);
    m_shadowDepthShader->setInt("uVertexFormat", 1);
    m_shadowDepthShader->setInt("uForceBaseLod", 1);
    m_shadowDepthShader->setInt("texArray", 0);
    m_shadowDepthShader->setFloat("uAnimationTime", ctx.animationTime);
    m_shadowDepthShader->setFloat("uTime", ctx.shaderTime);
    m_shadowDepthShader->setVec3("uShadowLightDirection", m_shadowRenderer->lightDirection());
    m_shadowDepthShader->setInt("uNoiseTex", 1);
    m_shadowDepthShader->setInt("uGrassColormap", 2);
    m_shadowDepthShader->setInt("uFoliageColormap", 3);
    const GLuint noiseTex = m_resourceMgr != nullptr ? m_resourceMgr->getTexture2D("shader_noise2d") : 0;
    // Entity/drop shadow sub-passes bind their own textures, so restore terrain inputs before terrain draws.
    auto bindTerrainShadowInputs = [&]() {
        m_shadowDepthShader->use();
        m_shadowDepthShader->setInt("uUseModel", 0);
        m_shadowDepthShader->setInt("uVertexFormat", 1);
        m_shadowDepthShader->setInt("uForceBaseLod", 1);
        m_shadowDepthShader->setInt("texArray", 0);
        m_shadowDepthShader->setInt("uNoiseTex", 1);
        m_shadowDepthShader->setInt("uGrassColormap", 2);
        m_shadowDepthShader->setInt("uFoliageColormap", 3);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_resourceMgr->getTextureArray().textureID);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, noiseTex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getGrassColormap());
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getFoliageColormap());
        glActiveTexture(GL_TEXTURE0);
    };
    bindTerrainShadowInputs();

    const float shadowDist = std::max(64.0f, settings.shadow.distance);
    int visibleTotal = 0;
    int culledTotal = 0;
    float maxCasterDistance = 0.0f;
    const char* cullingMode = "CSMBoxCulling";

    // Transparent shadow pass: writes DepthAll + Color0/Color1 for:
    // 1. UW VL dual-depth detection (water caustics)
    // 2. Colored shadows (stained glass tint)
    // Maintained for every cascade so caustics/tints do not stop at split 0.
    const bool needTransparentShadow = true;
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
            false,
            0,
            0
        };
    }

    shadow::ShadowCasterCuller shadowCuller;
    shadowCuller.setup(shadowDist, 1.0f, ctx.camera.position);
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
    cullingMode = shadowCuller.getCullingMode();

    for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        char cascadeLabel[32];
        std::snprintf(cascadeLabel, sizeof(cascadeLabel), "Shadow.CSM.Cascade%d", cascade);
        renderer::debug::ScopedDebugGroup cascadeGroup(cascadeLabel);

        const ShadowCascadeData& cascadeData = m_shadowRenderer->cascade(cascade);

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
        for (const auto& range : m_cascadeCutoutRanges[cascade]) {
            m_worldRenderBuffer->addCutout(range);
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
        stats.cutoutEntries = static_cast<int>(m_cascadeCutoutEntries[cascade].size());
        stats.transparentEntries = useMultiDrawIndirect
            ? static_cast<int>(m_cascadeTransparentRanges[cascade].size())
            : static_cast<int>(m_cascadeTransparentEntries[cascade].size());
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
            targets.bindCsmShadowLayer(cascade, cascadeRes);
            glClear(GL_DEPTH_BUFFER_BIT);
            bindTerrainShadowInputs();
            m_shadowDepthShader->setMat4("viewProj", cascadeData.viewProj);
            m_shadowDepthShader->setMat4("uShadowModelView", cascadeData.view);
            m_shadowDepthShader->setMat4("uShadowProjection", cascadeData.projection);
            m_shadowDepthShader->setMat4("uShadowProjectionInverse", glm::inverse(cascadeData.projection));
            m_shadowDepthShader->setInt("uShadowPassMode", 0);
            if (useMultiDrawIndirect) {
                m_worldRenderBuffer->flushOpaque();
            } else {
                GLuint lastVao = 0;
                for (const auto& entry : m_cascadeOpaqueEntries[cascade]) {
                    if (entry.chunk == nullptr) continue;
                    const SubChunkMesh& mesh = entry.chunk->getColumnMesh();
                    if (mesh.vertexCount == 0) continue;
                    if (lastVao != mesh.vao) {
                        glBindVertexArray(mesh.vao);
                        lastVao = mesh.vao;
                    }
                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.vertexCount));
                }
            }
            // Cutout shadow: render with polygon offset to prevent self-shadowing artifacts.
            // Cutout geometry (grass, flowers) are single-layer planes that would otherwise
            // cast shadows on themselves, producing stripe patterns.
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(2.0f, 4.0f);
            m_terrainRenderer->renderCutoutChunks(m_cascadeCutoutEntries[cascade], *m_shadowDepthShader);
            glDisable(GL_POLYGON_OFFSET_FILL);
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
            // Restore shadow_depth shader — renderShadowEntities()/renderShadowDrops() activated other shaders.
            m_shadowDepthShader->use();
            if (shadowStatsActive) {
                debugService->markShadowTimestamp(cascade, ShadowTimestampPoint::OpaqueEnd);
            }
        }

        // Pass 2: Transparent/all.
        // Copy DepthOpaque → DepthAll, then draw water + stained glass on top with depth writes.
        if (needTransparentShadow) {
            renderer::debug::ScopedDebugGroup transparentGroup("Transparent");
            // Copy opaque depth to DepthAll as baseline (avoids re-rendering opaque)
            glCopyImageSubData(
                targets.csmShadowDepthTexture(), GL_TEXTURE_2D_ARRAY,
                0, 0, 0, cascade,
                targets.csmShadowDepthAllTexture(), GL_TEXTURE_2D_ARRAY,
                0, 0, 0, cascade,
                cascadeRes, cascadeRes, 1);

            targets.bindCsmShadowTransparentLayer(cascade, cascadeRes);
            // Depth already contains opaque from the copy; clear color explicitly
            const float clearColor0[] = {0.0f, 0.0f, 0.0f, 1.0f}; // no transparent marker
            const float clearColor1[] = {0.0f, 0.0f, 0.0f, 0.0f};
            glClearBufferfv(GL_COLOR, 0, clearColor0);
            glClearBufferfv(GL_COLOR, 1, clearColor1);
            bindTerrainShadowInputs();
            m_shadowDepthShader->setInt("uShadowPassMode", 1);
            m_shadowDepthShader->setMat4("viewProj", cascadeData.viewProj);
            m_shadowDepthShader->setMat4("uShadowModelView", cascadeData.view);
            m_shadowDepthShader->setMat4("uShadowProjection", cascadeData.projection);
            m_shadowDepthShader->setMat4("uShadowProjectionInverse", glm::inverse(cascadeData.projection));
            // Transparent casters: depth write ON, no blending, no sort.
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);

            // Render transparent shadow chunks — same logic as Renderer::renderTransparentShadowChunks
            m_shadowDepthShader->use();
            m_shadowDepthShader->setInt("uForceBaseLod", 1);
            if (useMultiDrawIndirect) {
                m_worldRenderBuffer->beginFrame();
                for (const GpuMeshRange& range : m_cascadeTransparentRanges[cascade]) {
                    m_worldRenderBuffer->addTransparent(range);
                }
                stats.transparentCommands = m_worldRenderBuffer->transparentCommandCount();
                stats.transparentVertices = m_worldRenderBuffer->transparentVertexCount();
                m_worldRenderBuffer->flushTransparent();
            } else {
                uint64_t transparentVertices = 0;
                size_t transparentCommands = 0;
                for (const ChunkRenderEntry& entry : m_cascadeTransparentEntries[cascade]) {
                    if (entry.chunk == nullptr) continue;
                    const SubChunk* sc = entry.chunk->getSubChunk(entry.scy);
                    if (!sc) continue;
                    const SubChunkMesh& mesh = sc->getMesh();
                    if (mesh.transparentVertexCount == 0) continue;
                    glBindVertexArray(mesh.transparentVao);
                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.transparentVertexCount));
                    transparentVertices += mesh.transparentVertexCount;
                    ++transparentCommands;
                }
                stats.transparentCommands = transparentCommands;
                stats.transparentVertices = transparentVertices;
            }
            m_shadowDepthShader->use();
            m_shadowDepthShader->setInt("uForceBaseLod", 0);
            stats.transparentRendered = true;
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

#ifdef MECRAFT_ENABLE_CONSOLE_OUTPUT
    // Disabled verbose shadow stats logging
    // static int frameCounter = 0;
    // if (++frameCounter % 120 == 0) {
    //     MECRAFT_LOG_PRINTF("[shadow:csm] cascades=%d submitted=%d culled=%d maxDist=%.1f mode=%s halfPlane=%.1f\n",
    //                        SHADOW_CASCADE_COUNT, visibleTotal, culledTotal, maxCasterDistance, cullingMode, shadowDist);
    //     for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
    //         const ShadowCascadeStats& stats = cascadeStats[static_cast<size_t>(cascade)];
    //         MECRAFT_LOG_PRINTF("[shadow:csm:c%d] split=%.1f-%.1f texel=%.4f radius=%.1f box=%d/%d dist=%d/%d entries(cutout=%d trans=%d) cmds(o=%zu c=%zu t=%zu) verts(o=%llu c=%llu t=%llu)\n",
    //                            cascade,
    //                            stats.splitNear,
    //                            stats.splitFar,
    //                            stats.texelWorldSize,
    //                            stats.radius,
    //                            stats.boxVisible,
    //                            stats.boxCulled,
    //                            stats.distanceVisible,
    //                            stats.distanceCulled,
    //                            stats.cutoutEntries,
    //                            stats.transparentEntries,
    //                            stats.opaqueCommands,
    //                            stats.cutoutCommands,
    //                            stats.transparentCommands,
    //                            static_cast<unsigned long long>(stats.opaqueVertices),
    //                            static_cast<unsigned long long>(stats.cutoutVertices),
    //                            static_cast<unsigned long long>(stats.transparentVertices));
    //     }
    // }
#endif

    // Transitional compatibility for historical debug modes that still inspect
    // the legacy single-map projection: expose cascade 0 there.
    if (settings.debug.deferredLightDebugMode > 0 || settings.debug.lightDebugMode > 0) {
        glCopyImageSubData(targets.csmShadowDepthTexture(), GL_TEXTURE_2D_ARRAY,
                           0, 0, 0, 0,
                           targets.shadowDepthTexture(), GL_TEXTURE_2D,
                           0, 0, 0, 0,
                           targets.shadowResolution(),
                           targets.shadowResolution(),
                           1);
    }

    m_worldRenderBuffer->beginFrame();
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    return output;
}
