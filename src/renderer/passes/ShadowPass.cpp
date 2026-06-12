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
#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../../world/DropSystem.h"
#include "../../ecs/GameplayRegistry.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cfloat>
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


bool cascadeAabbVisible(const glm::vec3& boundsMin,
                        const glm::vec3& boundsMax,
                        void* userData) {
    auto* culler = static_cast<CascadeAabbCuller*>(userData);
    if (culler == nullptr) {
        return true;
    }

    glm::vec3 minNdc(FLT_MAX);
    glm::vec3 maxNdc(-FLT_MAX);
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 p(
            (corner & 1) != 0 ? boundsMax.x : boundsMin.x,
            (corner & 2) != 0 ? boundsMax.y : boundsMin.y,
            (corner & 4) != 0 ? boundsMax.z : boundsMin.z);
        const glm::vec4 clip = culler->viewProj * glm::vec4(p, 1.0f);
        const float invW = std::abs(clip.w) > 1.0e-6f ? 1.0f / clip.w : 1.0f;
        const glm::vec3 ndc(clip.x * invW, clip.y * invW, clip.z * invW);
        minNdc = glm::min(minNdc, ndc);
        maxNdc = glm::max(maxNdc, ndc);
    }

    const float xyPad = culler->xyPaddingNdc;
    const float zPad = culler->zPaddingNdc;
    bool visible = !(maxNdc.x < -1.0f - xyPad || minNdc.x > 1.0f + xyPad ||
                     maxNdc.y < -1.0f - xyPad || minNdc.y > 1.0f + xyPad);
    if (visible && culler->useZCulling) {
        visible = !(maxNdc.z < -1.0f - zPad || minNdc.z > 1.0f + zPad);
    }
    if (visible) {
        ++culler->visibleCount;
    } else {
        ++culler->culledCount;
    }
    return visible;
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
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_resourceMgr->getTextureArray().textureID);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, noiseTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getGrassColormap());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_resourceMgr->getFoliageColormap());

    const float shadowDist = std::max(64.0f, settings.shadow.distance);
    int visibleTotal = 0;
    int culledTotal = 0;
    float maxCasterDistance = 0.0f;
    const char* cullingMode = "CSMBoxCulling";

    // Transparent shadow pass: writes DepthAll + Color0/Color1 for:
    // 1. UW VL dual-depth detection (water caustics)
    // 2. Colored shadows (stained glass tint)
    // Always runs for cascade 0 to support colored shadow tinting.
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
            true,
            0,
            0
        };
    }

    shadow::ShadowCasterCuller shadowCuller;
    shadowCuller.setup(shadowDist, 1.0f, ctx.camera.position);
    shadowCuller.resetCounters();

    std::array<std::vector<GpuMeshRange>, 4> cascadeOpaqueRanges{};
    std::array<std::vector<GpuMeshRange>, 4> cascadeCutoutRanges{};
    std::array<std::vector<ChunkRenderEntry>, 4> cascadeOpaqueEntries{};
    std::array<std::vector<ChunkRenderEntry>, 4> cascadeCutoutEntries{};
    std::array<std::vector<ChunkRenderEntry>, 4> cascadeTransparentEntries{};

    m_terrainRenderer->clearTransparentBatches();
    m_terrainRenderer->collectShadowChunks(
        worldView,
        ctx.camera.position,
        shadowDist,
        &shadowCuller,
        cascadeCullers,
        cascadeOpaqueRanges,
        cascadeCutoutRanges,
        cascadeOpaqueEntries,
        cascadeCutoutEntries,
        cascadeTransparentEntries
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
        for (const auto& range : cascadeOpaqueRanges[cascade]) {
            m_worldRenderBuffer->addOpaque(range);
        }
        for (const auto& range : cascadeCutoutRanges[cascade]) {
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
        stats.cutoutEntries = static_cast<int>(cascadeCutoutEntries[cascade].size());
        stats.transparentEntries = static_cast<int>(cascadeTransparentEntries[cascade].size());
        stats.opaqueCommands = m_worldRenderBuffer->opaqueCommandCount();
        stats.cutoutCommands = m_worldRenderBuffer->cutoutCommandCount();
        stats.opaqueVertices = m_worldRenderBuffer->opaqueVertexCount();
        stats.cutoutVertices = m_worldRenderBuffer->cutoutVertexCount();
        stats.splitNear = cascadeData.splitNear;
        stats.splitFar = cascadeData.splitFar;
        stats.radius = cascadeData.radius;
        stats.texelWorldSize = cascadeData.texelWorldSize;

        // Pass 1: Opaque-only → DepthOpaque (shadowtex1)
        {
            renderer::debug::ScopedDebugGroup opaqueGroup("Opaque");
            if (shadowStatsActive) {
                debugService->markShadowTimestamp(cascade, ShadowTimestampPoint::Start);
            }
            const int cascadeRes = (cascade >= 2) ? settings.shadow.resolution / 2
                                                   : settings.shadow.resolution;
            targets.bindCsmShadowLayer(cascade, cascadeRes);
            glClear(GL_DEPTH_BUFFER_BIT);
            m_shadowDepthShader->setMat4("viewProj", cascadeData.viewProj);
            m_shadowDepthShader->setMat4("uShadowModelView", cascadeData.view);
            m_shadowDepthShader->setMat4("uShadowProjection", cascadeData.projection);
            m_shadowDepthShader->setMat4("uShadowProjectionInverse", glm::inverse(cascadeData.projection));
            m_shadowDepthShader->setInt("uShadowPassMode", 0);
            if (useMultiDrawIndirect) {
                m_worldRenderBuffer->flushOpaque();
            } else {
                GLuint lastVao = 0;
                for (const auto& entry : cascadeOpaqueEntries[cascade]) {
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
            m_terrainRenderer->renderCutoutChunks(cascadeCutoutEntries[cascade], *m_shadowDepthShader);
            glDisable(GL_POLYGON_OFFSET_FILL);
            // Entity shadow: render humanoid/mob depth into this cascade with distance/split culling.
            renderShadowEntities(worldView, cascadeData.viewProj, ctx.camera.position, cascadeData.splitNear, cascadeData.splitFar);
            // Drop shadow: render dropped items/blocks depth into this cascade.
            renderShadowDrops(worldView, cascadeData.viewProj, cascadeData.view, cascadeData.projection,
                              ctx.animationTime, ctx.shaderTime);
            // Restore shadow_depth shader — renderShadowEntities()/renderShadowDrops() activated other shaders.
            m_shadowDepthShader->use();
            if (shadowStatsActive) {
                debugService->markShadowTimestamp(cascade, ShadowTimestampPoint::OpaqueEnd);
            }
        }

        // Pass 2: Transparent/all — only cascade 0.
        // Copy DepthOpaque → DepthAll, then draw water + stained glass on top with depth writes.
        if (needTransparentShadow && cascade == 0) {
            renderer::debug::ScopedDebugGroup transparentGroup("Transparent");
            const int res = targets.shadowResolution();
            // Copy opaque depth to DepthAll as baseline (avoids re-rendering opaque)
            glCopyImageSubData(
                targets.csmShadowDepthTexture(), GL_TEXTURE_2D_ARRAY,
                0, 0, 0, cascade,
                targets.csmShadowDepthAllTexture(), GL_TEXTURE_2D_ARRAY,
                0, 0, 0, cascade,
                res, res, 1);

            targets.bindCsmShadowTransparentLayer(cascade);
            // Depth already contains opaque from the copy; clear color explicitly
            const float clearColor0[] = {0.0f, 0.0f, 0.0f, 1.0f}; // no transparent marker
            const float clearColor1[] = {0.0f, 0.0f, 0.0f, 0.0f};
            glClearBufferfv(GL_COLOR, 0, clearColor0);
            glClearBufferfv(GL_COLOR, 1, clearColor1);
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
                const auto& batch = m_terrainRenderer->transparentBatches();
                for (const DrawBatchEntry& entry : batch) {
                    // Shadow pass needs all transparent (including water) in one list
                    m_worldRenderBuffer->addTransparent(entry.range);
                }
                stats.transparentCommands = m_worldRenderBuffer->transparentCommandCount();
                stats.transparentVertices = m_worldRenderBuffer->transparentVertexCount();
                m_worldRenderBuffer->flushTransparent();
            } else {
                uint64_t transparentVertices = 0;
                size_t transparentCommands = 0;
                for (const ChunkRenderEntry& entry : cascadeTransparentEntries[cascade]) {
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
    static int frameCounter = 0;
    if (++frameCounter % 120 == 0) {
        MECRAFT_LOG_PRINTF("[shadow:csm] cascades=%d submitted=%d culled=%d maxDist=%.1f mode=%s halfPlane=%.1f\n",
                           SHADOW_CASCADE_COUNT, visibleTotal, culledTotal, maxCasterDistance, cullingMode, shadowDist);
        for (int cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
            const ShadowCascadeStats& stats = cascadeStats[static_cast<size_t>(cascade)];
            MECRAFT_LOG_PRINTF("[shadow:csm:c%d] split=%.1f-%.1f texel=%.4f radius=%.1f box=%d/%d dist=%d/%d entries(cutout=%d trans=%d) cmds(o=%zu c=%zu t=%zu) verts(o=%llu c=%llu t=%llu)\n",
                               cascade,
                               stats.splitNear,
                               stats.splitFar,
                               stats.texelWorldSize,
                               stats.radius,
                               stats.boxVisible,
                               stats.boxCulled,
                               stats.distanceVisible,
                               stats.distanceCulled,
                               stats.cutoutEntries,
                               stats.transparentEntries,
                               stats.opaqueCommands,
                               stats.cutoutCommands,
                               stats.transparentCommands,
                               static_cast<unsigned long long>(stats.opaqueVertices),
                               static_cast<unsigned long long>(stats.cutoutVertices),
                               static_cast<unsigned long long>(stats.transparentVertices));
        }
    }
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
