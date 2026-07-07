#include "WaterCompositePass.h"
#include "../debug/RenderDebugLabels.h"
#include "../targets/DeferredRenderTargets.h"
#include "../gl/GlStateGuard.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../mesh/TerrainRenderer.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../mesh/TerrainRenderCache.h"
#include "../core/Shader.h"
#include "../../resource/ResourceMgr.h"
#include "../../engine/platform/Window.h"
#include "../../world/World.h"
#include "../../world/chunk/Chunk.h"
#include "../../world/chunk/SubChunk.h"

// ChunkRenderEntry is defined in TerrainRenderer.h (already included above)

#include <glad/glad.h>

#include <algorithm>

void WaterCompositePass::init(ResourceMgr& resourceMgr) {
    m_resourceMgr = &resourceMgr;
    m_waterCompositeShader = resourceMgr.getShader("water_composite");
}

void WaterCompositePass::shutdown() {
    m_waterCompositeShader = nullptr;
    m_resourceMgr = nullptr;
}

bool WaterCompositePass::execute(const FrameContext& ctx, const RenderSettings& settings,
                                   DeferredRenderTargets& targets, const IWorldView& worldView,
                                   int windowWidth, int windowHeight,
                                   bool deferredFrameActive, bool preTemporalResolve,
                                   int32_t capturedFramebuffer, const int32_t* capturedViewport,
                                   bool transparentCompositeEnabled,
                                   bool waterEffectsEnabled, bool rainSurfaceRipplesEnabled,
                                   bool volumetricFogActive,
                                   bool useMultiDrawIndirect,
                                   WorldRenderBuffer& worldRenderBuffer,
                                   const std::vector<DrawBatchEntry>& transparentBatch,
                                   const TransparentPassPlan& transparentPlan,
                                   const std::vector<ChunkRenderEntry>& transparentEntries) {
    if (!waterEffectsEnabled ||
        m_waterCompositeShader == nullptr ||
        m_resourceMgr == nullptr) {
        return false;
    }

    const auto hasNonMdiWater = [&transparentEntries]() {
        for (const auto& entry : transparentEntries) {
            if (!entry.chunk) continue;
            const SubChunk* sc = entry.chunk->getSubChunk(entry.scy);
            if (sc && sc->getMesh().waterVertexCount > 0) {
                return true;
            }
        }
        return false;
    };

    if (useMultiDrawIndirect ? !transparentPlan.hasWater() : !hasNonMdiWater()) {
        return false;
    }

    const bool deferredInputsEnabled = deferredFrameActive && targets.isReady();
    const bool compositeInputsEnabled = deferredInputsEnabled &&
                                        (preTemporalResolve || transparentCompositeEnabled);
    const int capturedWidth = capturedViewport[2] > 0 ? capturedViewport[2] : windowWidth;
    const int capturedHeight = capturedViewport[3] > 0 ? capturedViewport[3] : windowHeight;

    if (compositeInputsEnabled) {
        targets.copySceneResolvedToTransparentComposite();
        targets.copyDepthToTransparentComposite();
        targets.bindTransparentComposite();
    } else if (deferredFrameActive) {
        // Restore the captured framebuffer viewport
        targets.bindDefaultLike(capturedFramebuffer, capturedWidth, capturedHeight);
    }

    const TextureArray& texArray = m_resourceMgr->getTextureArray();

    m_waterCompositeShader->use();
    m_waterCompositeShader->setMat4("view", ctx.camera.view);
    // DerivativeMain parity: water vertices also carry taaOffset when the
    // water pass runs before temporal resolve. The post-TAA fallback stays raw
    // because there is no later TAA pass to accumulate it.
    const bool useJitteredWater = preTemporalResolve && settings.taa.enabled;
    m_waterCompositeShader->setMat4("viewProj",
        useJitteredWater ? ctx.camera.jitteredViewProj : ctx.camera.viewProj);
    m_waterCompositeShader->setMat4("uInvViewProj",
        useJitteredWater ? ctx.camera.jitteredInvViewProj : ctx.camera.invViewProj);
    m_waterCompositeShader->setMat4("model", glm::mat4(1.0f));
    m_waterCompositeShader->setInt("uUseModel", 0);
    m_waterCompositeShader->setInt("uVertexFormat", 1);
    m_waterCompositeShader->setInt("texArray", 0);
    m_waterCompositeShader->setInt("uOpaqueDepthTex", 5);
    m_waterCompositeShader->setInt("uSkyCaptureTex", 6);
    m_waterCompositeShader->setInt("uSceneColorTex", 7);
    m_waterCompositeShader->setInt("uNoiseTex", 8);
    m_waterCompositeShader->setInt("uReflectionTex", 9);
    m_waterCompositeShader->setInt("uAtmosphereLut", 10);
    m_waterCompositeShader->setInt("uVolumetricTex", 11);
    m_waterCompositeShader->setInt("uRippleNormalTex", 12);
    m_waterCompositeShader->setInt("uSkyCaptureEnabled", deferredFrameActive ? 1 : 0);
    m_waterCompositeShader->setInt("uCompositeInputsEnabled", compositeInputsEnabled ? 1 : 0);
    m_waterCompositeShader->setInt("uWaterCompositeEnabled", compositeInputsEnabled ? 1 : 0);
    m_waterCompositeShader->setInt("uDepthSofteningEnabled", deferredInputsEnabled ? 1 : 0);
    m_waterCompositeShader->setInt("uVolumetricFogActive", volumetricFogActive ? 1 : 0);
    m_waterCompositeShader->setInt("uFrameIndex", static_cast<int>(ctx.frameIndex & 0x7fffffffULL));
    m_waterCompositeShader->setInt("uFreezeBias", settings.volumetric.freezeBias ? 1 : 0);
    m_waterCompositeShader->setFloat("uAnimationTime", ctx.animationTime);
    m_waterCompositeShader->setFloat("uTime", ctx.shaderTime);
    m_waterCompositeShader->setMat4("uView", ctx.camera.view);
    m_waterCompositeShader->setVec3("uCameraPos", ctx.camera.position);
    m_waterCompositeShader->setFloat("uNearPlane", ctx.camera.nearPlane);
    m_waterCompositeShader->setFloat("uFarPlane", ctx.camera.farPlane);
    // DerivativeMain shaders.properties: uniform.vec3.waterAbsorption = vec3(0.4, 0.14, 0.08)
    m_waterCompositeShader->setVec3("uWaterAbsorption", glm::vec3(0.4f, 0.14f, 0.08f));
    m_waterCompositeShader->setVec3("uSunDirection", ctx.skyColors.sunDirection);
    m_waterCompositeShader->setVec3("uMoonDirection", ctx.skyColors.moonDirection);
    m_waterCompositeShader->setVec3("uSunLightColor", ctx.skyColors.sunLightColor);
    m_waterCompositeShader->setVec3("uMoonLightColor", ctx.skyColors.moonLightColor);
    m_waterCompositeShader->setVec3("uSkyAmbientColor", ctx.skyColors.skyAmbientColor);
    m_waterCompositeShader->setFloat("uSkyIntensity", ctx.skyIntensity);
    m_waterCompositeShader->setFloat("uMoonVisibility", ctx.skyColors.moonVisibility);
    m_waterCompositeShader->setFloat("uWeatherWetness", ctx.weather.wetness);
    m_waterCompositeShader->setFloat("uSkyWetness", ctx.weather.skyWetness);
    m_waterCompositeShader->setFloat("uFogWetness", ctx.weather.fogWetness);
    m_waterCompositeShader->setFloat("uCloudWetness", ctx.weather.cloudWetness);
    m_waterCompositeShader->setFloat("uSurfaceWetness", ctx.weather.surfaceWetness);
    m_waterCompositeShader->setFloat("uWaterWaveHeight", 1.0f);
    m_waterCompositeShader->setFloat("uWaterWaveSpeed", 1.0f);
    m_waterCompositeShader->setFloat("uWaterIOR", 1.33f);
    m_waterCompositeShader->setInt("uRainSurfaceRipplesEnabled", rainSurfaceRipplesEnabled ? 1 : 0);
    m_waterCompositeShader->setInt("uIsEyeInWater", ctx.eyeInWater ? 1 : 0);

    if (m_resourceMgr) {
        const TextureAnimationInfo still = m_resourceMgr->getTextureAnimation("water_still");
        const TextureAnimationInfo flow = m_resourceMgr->getTextureAnimation("water_flow");
        m_waterCompositeShader->setFloat("uWaterStillFirstLayer", static_cast<float>(still.firstLayer));
        m_waterCompositeShader->setFloat("uWaterStillLayerCount", static_cast<float>(std::max(1, still.frameCount)));
        m_waterCompositeShader->setFloat("uWaterFlowFirstLayer", static_cast<float>(flow.firstLayer));
        m_waterCompositeShader->setFloat("uWaterFlowLayerCount", static_cast<float>(std::max(1, flow.frameCount)));
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, renderer::rhi::gl::textureId(texArray.texture));
    renderer::debug::ScopedDebugGroup bindGroup("WaterComposite.BindInputs");
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, targets.depthTexture());
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, targets.skyCaptureTexture());
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D,
                  renderer::rhi::gl::textureId(targets.sceneResolvedTextureHandle()));
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D,
                  renderer::rhi::gl::textureId(m_resourceMgr->getTexture2DHandle("shader_noise2d")));
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, targets.reflectionTexture());
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_3D, targets.atmosphereLutTexture());
    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, targets.halfResTexture());
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D,
                  renderer::rhi::gl::textureId(m_resourceMgr != nullptr
                      ? m_resourceMgr->getTexture2DHandle("shader_ripple_normal")
                      : RhiTextureHandle{}));

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    renderer::gl::ScopedCullFaceDisable cullFaceGuard;

    int drawCallCount = 0;
    if (useMultiDrawIndirect) {
        // MDI path: sort water entries back-to-front, build water command list, flush via MDI
        renderer::debug::ScopedDebugGroup mdiGroup("WaterComposite.DrawWater.MDI");
        std::vector<const DrawBatchEntry*> waterEntries;
        for (const auto& entry : transparentBatch) {
            if (entry.kind == TransparentBatchKind::Water) {
                waterEntries.push_back(&entry);
            }
        }
        std::sort(waterEntries.begin(), waterEntries.end(),
            [](const DrawBatchEntry* a, const DrawBatchEntry* b) {
                return a->distanceSq > b->distanceSq;
            });

        if (!waterEntries.empty()) {
            worldRenderBuffer.clearWaterCommands();
            for (const auto* entry : waterEntries) {
                worldRenderBuffer.addWater(entry->range);
            }
            worldRenderBuffer.flushWater();
            worldRenderBuffer.mergeSceneWaterFrameStats();
            drawCallCount = static_cast<int>(waterEntries.size());
        }
    } else {
        // Non-MDI path: sort and draw water sub-chunks individually
        renderer::debug::ScopedDebugGroup cpuGroup("WaterComposite.DrawWater.CPU");
        struct WaterItem {
            const ChunkRenderEntry* entry = nullptr;
            float distanceSq = 0.0f;
        };
        std::vector<WaterItem> waterItems;
        for (const auto& entry : transparentEntries) {
            if (!entry.chunk) continue;
            const SubChunk* sc = entry.chunk->getSubChunk(entry.scy);
            if (!sc || sc->getMesh().waterVertexCount == 0) continue;
            const glm::ivec3 offset = entry.chunk->getWorldOffset();
            const int yBase = entry.scy * SubChunk::SIZE;
            const glm::vec3 center(
                static_cast<float>(offset.x) + Chunk::SIZE_X * 0.5f,
                static_cast<float>(yBase + offset.y) + SubChunk::SIZE * 0.5f,
                static_cast<float>(offset.z) + Chunk::SIZE_Z * 0.5f);
            const glm::vec3 toCamera = center - ctx.camera.position;
            waterItems.push_back({&entry, glm::dot(toCamera, toCamera)});
        }
        std::sort(waterItems.begin(), waterItems.end(),
            [](const WaterItem& a, const WaterItem& b) {
                return a.distanceSq > b.distanceSq;
            });
        for (const auto& item : waterItems) {
            const SubChunk* sc = item.entry->chunk->getSubChunk(item.entry->scy);
            const SubChunkMesh& mesh = sc->getMesh();
            const uint32_t firstWaterVertex = mesh.transparentVertexCount - mesh.waterVertexCount;
            glBindVertexArray(mesh.transparentVao);
            glDrawArrays(GL_TRIANGLES,
                         static_cast<GLint>(firstWaterVertex),
                         static_cast<GLsizei>(mesh.waterVertexCount));
            ++drawCallCount;
        }
    }

    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    for (int i = 11; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE0);

    if (preTemporalResolve && compositeInputsEnabled) {
        targets.copyTransparentCompositeToSceneComposite();
        targets.copyTransparentCompositeToSceneResolved();
    } else if (compositeInputsEnabled) {
        targets.blitTransparentCompositeTo(capturedFramebuffer, capturedWidth, capturedHeight);
        targets.bindDefaultLike(capturedFramebuffer, capturedWidth, capturedHeight);
    }

    for (int unit = 8; unit >= 5; --unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    // Return whether water was rendered before temporal resolve
    // (caller needs to set m_waterRenderedBeforeTemporal accordingly)
    return preTemporalResolve && compositeInputsEnabled;
}
