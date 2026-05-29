#include "ShadowPass.h"
#include "../debug/RenderDebugLabels.h"
#include "../targets/DeferredRenderTargets.h"
#include "../shadow/ShadowRenderer.h"
#include "../shadow/ShadowMatrices.h"
#include "../shadow/ShadowCasterCuller.h"
#include "../mesh/TerrainRenderer.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../core/Shader.h"
#include "../renderers/GameplaySkyRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../world/World.h"
#include "../../world/chunk/Chunk.h"
#include "../../world/chunk/SubChunk.h"
#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../../world/DropSystem.h"
#include "../../ecs/GameplayRegistry.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
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

struct CascadeAabbCuller {
    glm::mat4 viewProj = glm::mat4(1.0f);
    float xyPaddingNdc = 0.0f;
    int visibleCount = 0;
    int culledCount = 0;
};

bool cascadeAabbVisible(const glm::vec3& boundsMin,
                        const glm::vec3& boundsMax,
                        void* userData) {
    auto* culler = static_cast<CascadeAabbCuller*>(userData);
    if (culler == nullptr) {
        return true;
    }

    glm::vec2 minNdc(FLT_MAX);
    glm::vec2 maxNdc(-FLT_MAX);
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 p(
            (corner & 1) != 0 ? boundsMax.x : boundsMin.x,
            (corner & 2) != 0 ? boundsMax.y : boundsMin.y,
            (corner & 4) != 0 ? boundsMax.z : boundsMin.z);
        const glm::vec4 clip = culler->viewProj * glm::vec4(p, 1.0f);
        const float invW = std::abs(clip.w) > 1.0e-6f ? 1.0f / clip.w : 1.0f;
        const glm::vec2 ndc(clip.x * invW, clip.y * invW);
        minNdc = glm::min(minNdc, ndc);
        maxNdc = glm::max(maxNdc, ndc);
    }

    const float pad = culler->xyPaddingNdc;
    const bool visible = !(maxNdc.x < -1.0f - pad || minNdc.x > 1.0f + pad ||
                           maxNdc.y < -1.0f - pad || minNdc.y > 1.0f + pad);
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

void ShadowPass::renderShadowEntities(const World& world, const glm::mat4& shadowViewProj) {
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

    m_humanoidRenderer->renderToShadowMap(world, *m_gameplayRegistry,
                                          shadowViewProj, HumanoidRenderer::kRenderAll);

    glBindVertexArray(0);
}

void ShadowPass::renderShadowDrops(const World& world, const glm::mat4& shadowViewProj,
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

    m_dropRenderer->renderToShadowMap(world, *m_dropSystem, shadowViewProj,
                                       shadowView, shadowProjection, animationTime, shaderTime);

    glBindVertexArray(0);
}

ShadowPass::ShadowPassOutput ShadowPass::execute(
    const FrameContext& ctx, const RenderSettings& settings,
    DeferredRenderTargets& targets, const World& world,
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

        // Collect geometry once per cascade
        m_worldRenderBuffer->beginFrame();
        std::vector<ChunkRenderEntry> cutoutEntries;
        cutoutEntries.reserve(world.getActiveChunks().size() * 2);
        std::vector<ChunkRenderEntry> transparentEntries;
        transparentEntries.reserve(world.getActiveChunks().size() * 2);
        m_terrainRenderer->clearTransparentBatches();

        shadow::ShadowCasterCuller shadowCuller;
        shadowCuller.setup(shadowDist, 1.0f, ctx.camera.position);
        shadowCuller.resetCounters();
        const float cascadePaddingWorld = std::max(16.0f, cascadeData.texelWorldSize * 16.0f);
        // Conservative cascade culling: clip against the cascade's light-space
        // X/Y box only. Z stays unculled to avoid dropping long shadow casters
        // at low sun angles.
        CascadeAabbCuller cascadeCuller{
            cascadeData.viewProj,
            cascadePaddingWorld / std::max(1.0f, cascadeData.radius)
        };
        m_terrainRenderer->renderOpaqueChunksAndCollectPasses(world, cutoutEntries, transparentEntries, false,
                                                                shadowDist, &shadowCuller,
                                                                cascadeAabbVisible, &cascadeCuller);
        m_terrainRenderer->syncTransparentBatches();
        char cullerLabel[128];
        std::snprintf(cullerLabel, sizeof(cullerLabel),
                      "Shadow.Cascade%d.Culler boxVisible=%d boxCulled=%d distanceVisible=%d distanceCulled=%d",
                      cascade,
                      cascadeCuller.visibleCount,
                      cascadeCuller.culledCount,
                      shadowCuller.getVisibleCount(),
                      shadowCuller.getCulledCount());
        renderer::debug::insertEvent(cullerLabel);
        visibleTotal += shadowCuller.getVisibleCount();
        culledTotal += shadowCuller.getCulledCount();
        maxCasterDistance = std::max(maxCasterDistance, shadowCuller.getMaxCasterDistance());
        cullingMode = shadowCuller.getCullingMode();

        // Pass 1: Opaque-only → DepthOpaque (shadowtex1)
        {
            renderer::debug::ScopedDebugGroup opaqueGroup("Opaque");
            targets.bindCsmShadowLayer(cascade);
            glClear(GL_DEPTH_BUFFER_BIT);
            m_shadowDepthShader->setMat4("viewProj", cascadeData.viewProj);
            m_shadowDepthShader->setMat4("uShadowModelView", cascadeData.view);
            m_shadowDepthShader->setMat4("uShadowProjection", cascadeData.projection);
            m_shadowDepthShader->setMat4("uShadowProjectionInverse", glm::inverse(cascadeData.projection));
            m_shadowDepthShader->setInt("uShadowPassMode", 0);
            if (useMultiDrawIndirect) {
                m_worldRenderBuffer->flushOpaque();
            }
            m_terrainRenderer->renderCutoutChunks(cutoutEntries, *m_shadowDepthShader);
            // Entity shadow: render humanoid/mob depth into this cascade.
            renderShadowEntities(world, cascadeData.viewProj);
            // Drop shadow: render dropped items/blocks depth into this cascade.
            renderShadowDrops(world, cascadeData.viewProj, cascadeData.view, cascadeData.projection,
                              ctx.animationTime, ctx.shaderTime);
            // Restore shadow_depth shader — renderShadowEntities()/renderShadowDrops() activated other shaders.
            m_shadowDepthShader->use();
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
            m_shadowDepthShader->setInt("uForceBaseLod", 1);
            if (useMultiDrawIndirect) {
                m_worldRenderBuffer->beginFrame();
                const auto& batch = m_terrainRenderer->transparentBatches();
                for (const DrawBatchEntry& entry : batch) {
                    // Shadow pass needs all transparent (including water) in one list
                    m_worldRenderBuffer->addTransparent(entry.range);
                }
                m_worldRenderBuffer->flushTransparent();
            } else {
                for (const ChunkRenderEntry& entry : transparentEntries) {
                    if (entry.chunk == nullptr) continue;
                    const SubChunk* sc = entry.chunk->getSubChunk(entry.scy);
                    if (!sc) continue;
                    const SubChunkMesh& mesh = sc->getMesh();
                    if (mesh.transparentVertexCount == 0) continue;
                    glBindVertexArray(mesh.transparentVao);
                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.transparentVertexCount));
                }
            }
            m_shadowDepthShader->setInt("uForceBaseLod", 0);
        }
    }

    static int frameCounter = 0;
    if (++frameCounter % 120 == 0) {
        printf("[shadow:csm] cascades=%d submitted=%d culled=%d maxDist=%.1f mode=%s halfPlane=%.1f\n",
               SHADOW_CASCADE_COUNT, visibleTotal, culledTotal, maxCasterDistance, cullingMode, shadowDist);
    }

    // Transitional compatibility for historical debug modes that still inspect
    // the legacy single-map projection: expose cascade 0 there.
    glCopyImageSubData(targets.csmShadowDepthTexture(), GL_TEXTURE_2D_ARRAY,
                       0, 0, 0, 0,
                       targets.shadowDepthTexture(), GL_TEXTURE_2D,
                       0, 0, 0, 0,
                       targets.shadowResolution(),
                       targets.shadowResolution(),
                       1);

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
