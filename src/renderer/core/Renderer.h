//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_RENDERER_H
#define MECRAFT_RENDERER_H
#include "engine/camera/Camera.h"
#include "../../resource/ResourceMgr.h"
#include "../../thread/ThreadPool.h"
#include "engine/platform/Window.h"
#include "../mesh/ChunkMeshingService.h"
#include "../targets/DeferredRenderTargets.h"
#include "../passes/SsaoPass.h"
#include "../passes/VelocityPass.h"
#include "../passes/ReflectionPass.h"
#include "../passes/TemporalResolvePass.h"
#include "../passes/MotionBlurPass.h"
#include "../passes/DepthOfFieldPass.h"
#include "../passes/DeferredLightingPass.h"
#include "../passes/CloudPass.h"
#include "../passes/SceneCompositePass.h"
#include "../passes/VolumetricPass.h"
#include <memory>
#include "../renderers/GameplaySkyRenderer.h"
#include "Shader.h"
#include "../shadow/ShadowRenderer.h"
#include "../mesh/TerrainRenderCache.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../mesh/WorldDrawBatch.h"
#include <glm/glm.hpp>
#include <array>
#include <unordered_set>
#include <unordered_map>
#include <vector>

class World;
class Chunk;
class HumanoidRenderer;
class DropRenderer;
class ParticleSystem;
class DropSystem;

namespace ecs { class GameplayRegistry; }
namespace shadow { class ShadowCasterCuller; }

/// Decoupled data transfer structs for block interaction rendering.
/// These allow Renderer to draw block outlines/overlays without depending on Player or ECS.
struct BlockTargetRenderData {
    bool hasTarget = false;
    glm::ivec3 targetBlock{};
};

struct BlockBreakRenderData {
    bool active = false;
    float progress01 = 0.0f;
    glm::ivec3 blockPos{};
};

class Renderer {
public:
    enum class RenderPipelineMode : int {
        ForwardLegacy = 0,
        HybridDeferred = 1
    };

    enum class FogMode : int {
        Linear = 0,
        Exp = 1,
        Exp2 = 2
    };

    struct RenderPipelineSettings {
        RenderPipelineMode mode = RenderPipelineMode::HybridDeferred;
        bool shadowsEnabled = true;
        bool softShadowsEnabled = true;
        bool pcssShadowsEnabled = true;
        bool contactShadowsEnabled = false;
        bool cloudShadowsEnabled = false;  // DerivativeMain CLOUDS_SHADOW: off by default
        bool ssaoEnabled = true;
        bool bloomEnabled = true;
        float bloomThreshold = 0.0f;
        float bloomStrength = 1.0f;
        bool autoExposureEnabled = true;
        float autoExposureMin = 0.001f;
        float autoExposureMax = 64.0f; // Legacy UI field; DerivativeMain target exposure is not clamped.
        float autoExposureSpeed = 1.0f;
        float autoExposureBias = 0.0f;
        bool sunRaysEnabled = false;
        bool waterEffectsEnabled = true;
        bool transparentCompositeEnabled = true;
        bool shaderpackGradingEnabled = true;
        bool aerialPerspectiveEnabled = true;
        bool volumetricLightEnabled = true; // DerivativeMain VOLUMETRIC_LIGHT: base haze (airDensity)
        bool volumetricFogEnabled = true;
        bool volumetricSkyRayEnabled = true; // A/B toggle: sky pixels march volumetric fog
        bool volumetricTimeFadeEnabled = true; // DerivativeMain TIME_FADE
        int volumetricQualityTier = 1; // DerivativeMain FOG_TYPE: 0=Low, 1=Medium, 2=High, 3=Ultra
        bool uwVolumetricLightEnabled = true; // DerivativeMain UW_VOLUMETRIC_LIGHT: underwater volumetric light
        int volumetricFogSamples = 20; // DerivativeMain VOLUMETRIC_FOG_SAMPLES: march step count
        float volumetricShadowBiasScale = 1.0f; // bias multiplier for VFog A/B testing
        bool volumetricTemporalEnabled = true;
        float volumetricTemporalWeight = 0.90f;
        bool freezeR1 = false;       // A/B test: freeze VFog R1 dither (no temporal variation)
        bool freezeBias = false;     // A/B test: freeze VFog upscale bias (no temporal rotation)
        bool forceZeroVelocity = false; // A/B test: force zero velocity (verify TAA pure accumulation)
        bool freezeTaaJitter = false; // A/B test: disable projection jitter while keeping TAA resolve active
        bool taaEnabled = true;
        bool reflectionFilterEnabled = true;
        bool ssaoFilterEnabled = true;
        bool ssaoTemporalEnabled = true;
        float ssaoHistoryWeight = 0.85f;
        bool reflectionTemporalEnabled = true;
        float reflectionHistoryWeight = 0.90f;
        bool motionBlurEnabled = false;
        float taaBlendMin = 0.08f;
        float taaBlendMax = 0.26f;
        float reflectionFilterStrength = 1.0f;
        float motionBlurStrength = 1.0f;
        int motionBlurSamples = 8;
        bool dofEnabled = false;
        float dofIntensity = 0.15f;
        float dofAperture = 2.8f;
        float dofFocusDistance = 5.0f;
        float sceneCloudCompositeStrength = 0.85f;
        float sceneReflectionCompositeStrength = 1.0f;
        int debugViewMode = 0;
        int deferredLightDebugMode = 0; // 0=off, 1=direct, 2=skylight, 3=blocklight, 4=minAmbient, 5=fakeBounce, 6=beforePost
        bool derivativeStrictMode = false;
        int weatherPreset = 0; // DEPRECATED: weather state now lives in World::WeatherSystem
        int tonemapMode = 1; // 0=Reinhard, 1=AcademyFit, 2=Filmic, 3=AgX_Minimal, 4=AcademyFull, 5=AgX_Full
        bool debugDisableGreedyMeshing = false;
        int shadowResolution = 2048;
        float shadowDistance = 192.0f;
        float shadowSoftness = 1.0f;
        float shadowPcssStrength = 0.72f;
        float shadowConstantBias = 0.0007f;
        float shadowSlopeBias = 0.0022f;
        float shadowNormalOffset = 0.035f;
        float contactShadowStrength = 0.12f;
        float cloudShadowStrength = 0.28f;
        float cloudShadowScale = 0.0045f;
        float cloudShadowSpeed = 0.018f;
        float cloudTimeScale = 0.35f; // Mecraft time-unit adaptation for DerivativeMain CLOUDS_SPEED.
        float sunRayStrength = 0.18f;
        float volumetricLightStrength = 0.14f; // DEPRECATED: DerivativeMain volumetric path ignores this UI value.
        float colorTemperature = 1.0f;
        float vibrance = 0.0f;
        float highlightCompression = 0.0f;
        float filmEmulationStrength = 0.0f;
        float redModifierStrength = 0.35f;
        float colorLumaR = 1.02f;
        float colorLumaG = 1.00f;
        float colorLumaB = 0.96f;
        float albedoDesaturation = 0.0f;
        float sunWarmth = 0.34f;
        float skyCoolness = 0.18f;
        float shadowDesaturation = 0.22f;
        float splitToneStrength = 0.0f;
        float vignetteStrength = 0.0f;
        float shadowTintStrength = 0.40f;
        float directSunStrength = 1.0f;
        float skyAmbientStrength = 0.36f;
        float minimumAmbient = 0.055f;
        float shadowMinLight = 0.0f;
        float shadowContrast = 1.0f;
        float blockLightStrength = 1.00f;
        float fakeBounceStrength = 0.06f;
        float aerialStrength = 0.65f;
        float horizonScatterStrength = 0.78f;
        float volumetricFogStrength = 1.0f;
        // DerivativeMain-style VFog independent profile (decoupled from weather)
        float vfogCenterHeight = 63.0f;   // SEA_LEVEL: y-level where fog is densest
        float vfogHeightSpread = 100.0f;  // DerivativeMain falloff denominator: 100 -> exponent 0.01
        float vfogNoiseScale = 0.04f;     // noise sampling scale for structured fog
        float vfogLightStrength = 0.2f;   // DerivativeMain VOLUMETRIC_LIGHT_STRENGTH
        float vfogDensityScale = 1.0f;    // user density multiplier (volFogDensity equivalent)
        float underwaterVolumetricLightStrength = 0.1f; // DerivativeMain UW_VOLUMETRIC_LIGHT_STRENGTH
        float noiseDitherStrength = 0.015f;
        float sharpenStrength = 0.3f; // DerivativeMain CAS_STRENGTH
        float ssaoRadius = 0.6f;
        float ssaoStrength = 0.75f;
        int ssaoSamples = 16;
        float exposure = 12.0f;
        float gamma = 1.0f;
        float saturation = 1.0f;
        float contrast = 1.0f;
        bool purkinjeShiftEnabled = false; // DerivativeMain PurkinjeShift
        bool bloomyFogEnabled = true; // DerivativeMain BLOOMY_FOG
        int postprocessDebugMode = 0; // 0=off, 1=bloomData, 2=fogTransmittance, 3=bloomyFog, 4=rainMask
        int reflectionDebugMode = 0; // 0=off, 1=pixelWetness, 2=reflectance, 3=ssrHit, 4=roughness, 5=specularWeight, 6=compositeDelta, 7=puddleMask, 8=rainSplashMask, 9=rainRippleNormal, 10=rainRippleStrength, 11=f0, 12=skyFallback, 13=reflectionRgb, 14=hasReflection, 15=skyLightRaw, 16=voxelLightRG, 17=materialAux, 18=skyGradient, 19=finalContribution, 20=reflectionSource, 21=reflectanceX32, 22=f0X32, 23=roughness, 24=reflectionSourceX8, 25=finalContributionX32, 26=reflectionSceneRatio, 27=sceneLuma, 28=reflectionLumaX64, 29=reflectanceX128, 30=sourceGradientX128
        float directWeatherOcclusion = -1.0f; // <0 = auto from skyWetness; >=0 = manual override
        // Weather render profile — runtime-tunable per-weather visual parameters
        float weatherSkylightScale = 1.0f;   // [0,1] multiplier on skylight during precipitation
        float weatherExposureBias = 0.0f;    // EV offset on auto exposure during precipitation
        float weatherPostRainFog = 1.0f;     // [0,2] multiplier on post-process rain/snow fog
        float weatherRainAlphaScale = 2.5f;  // [0,5] rain particle alpha boost
        bool weatherRainLinesEnabled = true; // Debug isolation for visible precipitation lines
        bool sceneParticlesEnabled = true;    // Debug isolation for ECS particles
        bool rainWetSurfacesEnabled = true;   // Debug isolation for wet terrain material/reflection response
        bool rainSurfaceRipplesEnabled = true; // Debug isolation for wet-surface ripple normals
    };


    struct FogSettings {
        bool enabled = true;
        FogMode mode = FogMode::Linear;
        glm::vec3 color = glm::vec3(0.67f, 0.84f, 1.0f);
        float startDistance = 140.0f;
        float endDistance = 260.0f;
        float density = 0.01f;
        bool autoDistanceByRenderDistance = true;
        float autoEndOffsetChunks = -0.5f;
        float autoFadeWidthChunks = 2.0f;
    };

    struct AtmosphereSettings {
        float aerialStrength = 0.65f;
        float horizonScatterStrength = 0.78f;
        float sunWarmth = 0.34f;
        float skyCoolness = 0.18f;
        float weatherWetness = 0.0f;
        float weatherStorm = 0.0f;
        float aerialReduction = 0.55f;
        float lightningFlash = 0.0f;
        float surfaceWetness = 0.0f;
        float skyWetness = 0.0f;
        float fogWetness = 0.0f;
        float cloudWetness = 0.0f;
        float precipitation = 0.0f;
        float directWeatherOcclusion = 1.0f; // 1.0=clear, 0.03=DerivativeMain Storm default
        int directWeatherOcclusionOverride = 0; // 0=auto, 1=manual bypass
    };

    struct VolumetricSettings {
        bool lightEnabled = true; // DerivativeMain VOLUMETRIC_LIGHT: base haze (airDensity)
        bool uwLightEnabled = true; // DerivativeMain UW_VOLUMETRIC_LIGHT: underwater volumetric light
        bool fogEnabled = true;
        float fogStrength = 1.0f;
        float baseDensity = 1.0f;
        float heightFalloff = 0.022f;
        float maxDistance = 260.0f;
        // DerivativeMain-style VFog independent profile (decoupled from weather)
        float fogCenterHeight = 63.0f;   // SEA_LEVEL: y-level where fog is densest
        float fogHeightSpread = 100.0f;  // DerivativeMain falloff denominator: 100 -> exponent 0.01
        float fogNoiseScale = 0.04f;     // noise sampling scale for structured fog
        float fogLightStrength = 0.2f;   // DerivativeMain VOLUMETRIC_LIGHT_STRENGTH
        float fogDensityScale = 1.0f;    // user density multiplier (volFogDensity equivalent)
        int fogSamples = 20;             // DerivativeMain VOLUMETRIC_FOG_SAMPLES: march step count
    };

    struct CloudSettings {
        bool shadowsEnabled = true;
        float shadowStrength = 0.0f;
        float shadowScale = 0.0045f;
        float shadowSpeed = 0.018f;
        float timeScale = 0.35f;
        float coverage = 0.35f;
        float density = 1.0f;
        float height = 1000.0f;     // DerivativeMain CLOUD_CUMULUS_CLEAR_ALTITUDE
        float thickness = 1400.0f;  // DerivativeMain CLOUD_CUMULUS_CLEAR_THICKNESS
        // Planar clouds (cirrus)
        float planarCoverage = 0.5f;
        float planarDensity = 1.0f;
        float planarAltitude = 7000.0f; // DerivativeMain CLOUD_PLANE_ALTITUDE
    };

    enum class FrustumPlane : size_t {
        Left = 0,
        Right = 1,
        Bottom = 2,
        Top = 3,
        Near = 4,
        Far = 5,
        Count = 6
    };

#ifdef MECRAFT_DEBUG
    struct MeshingFrameStats {
        int submitBudget = 0;
        int submitted = 0;
        int completed = 0;
        int inFlight = 0;
        int staleDropped = 0;
        int deferredResults = 0;
        double lastBuildMs = 0.0;
        double averageBuildMs = 0.0;
        uint32_t lastOpaqueFacesBeforeGreedy = 0;
        uint32_t lastOpaqueFacesAfterGreedy = 0;
        uint32_t lastTransparentFacesBeforeGreedy = 0;
        uint32_t lastTransparentFacesAfterGreedy = 0;
        uint32_t lastOpaqueVertexCount = 0;
    };

    struct CullingFrameStats {
        int regionTests = 0;
        int regionPassed = 0;
        int columnTests = 0;
        int columnPassed = 0;
        int chunkTests = 0;
        int chunkPassed = 0;
        int chunkCulled = 0;
        std::array<int, static_cast<size_t>(FrustumPlane::Count)> chunkCulledByPlane{};
    };

    struct GpuFrameStats {
        bool supported = false;
        bool valid = false;
        double gbufferMs = 0.0;
        double shadowMs = 0.0;
        double ssaoMs = 0.0;
        double lightingMs = 0.0;
        double transparentMs = 0.0;
        double volumetricMs = 0.0;
        double reflectionMs = 0.0;
        double cloudMs = 0.0;
        double waterMs = 0.0;
        double postMs = 0.0;
    };

    struct RenderWorkStats {
        size_t blockVertexBytes = 0;
        size_t opaqueCommands = 0;
        size_t cutoutCommands = 0;
        size_t transparentCommands = 0;
        size_t transparentGenericCommands = 0;
        size_t transparentWaterCommands = 0;
        size_t opaqueLogicalCommands = 0;
        size_t cutoutLogicalCommands = 0;
        size_t transparentLogicalCommands = 0;
        size_t opaquePoolCapacityVertices = 0;
        size_t cutoutPoolCapacityVertices = 0;
        size_t transparentPoolCapacityVertices = 0;
        size_t opaquePoolUsedVertices = 0;
        size_t cutoutPoolUsedVertices = 0;
        size_t transparentPoolUsedVertices = 0;
        float opaquePoolFragmentation = 0.0f;
        float cutoutPoolFragmentation = 0.0f;
        float transparentPoolFragmentation = 0.0f;
        uint64_t opaqueVertices = 0;
        uint64_t cutoutVertices = 0;
        uint64_t transparentVertices = 0;
        uint64_t transparentGenericVertices = 0;
        uint64_t transparentWaterVertices = 0;
        int cutoutCandidates = 0;
        int cutoutSkippedByDistance = 0;
        int mdiSubChunkTests = 0;
        int mdiSubChunksCulled = 0;
        // Upload budget stats
        size_t meshUploadBytesThisFrame = 0;
        size_t meshUploadVerticesThisFrame = 0;
        size_t meshUploadDeferredCount = 0;
        size_t worldBufferExpandCount = 0;
        double worldBufferUploadMs = 0.0;
    };

    static constexpr size_t MESHING_HISTORY_SIZE = 120;
#endif

    ~Renderer();
    void init(ResourceMgr& resourceMgr);
    void shutdown();
    void render(const World& world, const Camera &camera, const Window &window, const BlockTargetRenderData& target, const BlockBreakRenderData& blockBreak);
    void renderOpaqueAndCutout(const World& world, const Camera& camera, const Window& window);
    void renderTransparentAndOverlays(const World& world, const BlockTargetRenderData& target, const BlockBreakRenderData& blockBreak, const Window& window);

    void setMeshingSubmitBudget(int budget);
    void setRegionChunkSize(int chunkSize);
    void setAtlasAnisotropy(float anisotropy);
    void setFogEnabled(bool enabled);
    void setFogMode(FogMode mode);
    void setFogColor(const glm::vec3& color);
    void setFogLinearDistances(float startDistance, float endDistance);
    void setFogDensity(float density);
    void setFogAutoDistanceEnabled(bool enabled);
    void setFogAutoEndOffsetChunks(float offsetChunks);
    void setFogAutoFadeWidthChunks(float fadeWidthChunks);
    [[nodiscard]] FogSettings getFogSettings() const;
    void setHeldBlockLightValue(int value);

    // Debug light visualization: 0=off, 1=sky light heatmap, 2=block light heatmap, 3=combined heatmap
    void setDebugLightMode(int mode);
    [[nodiscard]] int getDebugLightMode() const;
    void setRenderPipelineSettings(const RenderPipelineSettings& settings);
    void setEyeInWater(bool inWater) { m_eyeInWater = inWater; }
    [[nodiscard]] RenderPipelineSettings getRenderPipelineSettings() const;
    [[nodiscard]] GameplaySkyRenderer::SkyIlluminanceData getSkyIlluminanceData() const { return m_currentFrameData.skyIlluminance; }
    [[nodiscard]] GameplaySkyRenderer::SkyColors getSkyColors() const { return m_currentFrameData.skyColors; }
    [[nodiscard]] glm::vec3 getFogColor() const { return m_currentFrameData.fogColor; }
    [[nodiscard]] bool isDeferredDebugViewActive() const;
    [[nodiscard]] bool isDeferredFrameActive() const { return m_deferredFrameActive; }
    void setRenderLocalPlayerModel(bool visible) { m_renderLocalPlayerModel = visible; }
    void setHumanoidRenderer(HumanoidRenderer* hr) { m_humanoidRenderer = hr; }
    void setDropRenderer(DropRenderer* dr) { m_dropRenderer = dr; }
    void setParticleSystem(ParticleSystem* ps) { m_particleSystem = ps; }
    void setDropSystem(DropSystem* ds) { m_dropSystem = ds; }
    void setGameplayRegistry(ecs::GameplayRegistry* reg) { m_gameplayRegistry = reg; }
    void renderDeferredDebugOverlay(const Window& window);
    [[nodiscard]] bool isHybridDeferredReady() const;
    // Shadow data for held item renderer — returns cascade matrices, textures, and settings.
    struct HeldItemShadowData {
        glm::mat4 cascadeViewProj[4]{};
        float cascadeSplitFar[4]{};
        float cascadeTexelWorldSize[4]{};
        GLuint shadowTexture = 0;
        GLuint shadowDepthRaw = 0;
        GLuint shadowDepthAll = 0;
        GLuint shadowDepthAllRaw = 0;
        GLuint shadowColor0 = 0;
        GLuint shadowColor1 = 0;
        glm::vec3 cameraPos = glm::vec3(0.0f);
        glm::vec3 sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        float shadowDistance = 192.0f;
        float constantBias = 0.0007f;
        float slopeBias = 0.0022f;
        float normalOffset = 0.035f;
        float softness = 1.0f;
        float pcssStrength = 0.72f;
        int cascadeCount = 4;
        int softShadowsEnabled = 1;
        int pcssShadowsEnabled = 1;
        int shadowsEnabled = 1;
        float skyIntensity = 1.0f;
    };
    [[nodiscard]] HeldItemShadowData getHeldItemShadowData() const;
    [[nodiscard]] GLuint gbufDepthTexture() const { return m_deferredTargets.depthTexture(); }
    [[nodiscard]] GLuint weatherMaskTexture() const { return m_deferredTargets.weatherMaskTexture(); }
    void bindWeatherMaskFbo() { m_deferredTargets.bindWeatherMask(); }
    void restoreDefaultFbo();
    [[nodiscard]] ThreadPool* getThreadPool() { return &m_threadPool; }
#ifdef MECRAFT_DEBUG
    void setChunkCullingDebugEnabled(bool enabled);
    [[nodiscard]] int getMeshingSubmitBudget() const;
    [[nodiscard]] int getRegionChunkSize() const;
    [[nodiscard]] bool isChunkCullingDebugEnabled() const;
    [[nodiscard]] MeshingFrameStats getMeshingFrameStats() const;
    [[nodiscard]] CullingFrameStats getCullingFrameStats() const;
    [[nodiscard]] GpuFrameStats getGpuFrameStats() const;
    [[nodiscard]] RenderWorkStats getRenderWorkStats() const;
    void setGpuTimerEnabled(bool enabled);
    [[nodiscard]] bool isGpuTimerEnabled() const;
    void setCutoutDistanceLimitEnabled(bool enabled);
    [[nodiscard]] bool isCutoutDistanceLimitEnabled() const;
    void setCutoutRenderDistanceChunks(float distanceChunks);
    [[nodiscard]] float getCutoutRenderDistanceChunks() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingSubmittedHistory() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingCompletedHistory() const;
    [[nodiscard]] const std::array<float, MESHING_HISTORY_SIZE>& getMeshingInFlightHistory() const;
    [[nodiscard]] size_t getMeshingHistoryCount() const;
#endif

    // 视锥剔除
    void updateFrustum(const glm::mat4& viewProj);
    [[nodiscard]] int getDrawCallCount() const;
    [[nodiscard]] bool isMultiDrawIndirectEnabled() const { return m_useMultiDrawIndirect; }
    [[nodiscard]] int getGlSubmitCount() const;
    [[nodiscard]] float getAtlasAnisotropy() const;
    [[nodiscard]] float getAtlasMaxAnisotropy() const;
private:
    struct Plane {
        glm::vec3 n = glm::vec3(0.0f);
        float d = 0.0f;
    };

    struct ChunkRenderEntry {
        Chunk* chunk = nullptr;
        int scy = -1;  // -1 = column aggregate, otherwise sub-chunk index
        bool aggregated = false;
    };

    static constexpr int SHADOW_CASCADE_COUNT = shadow::ShadowRenderer::CASCADE_COUNT;
    using ShadowCascadeData = shadow::ShadowRenderer::Cascade;

    struct RenderFrameData {
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 projection = glm::mat4(1.0f);
        glm::mat4 viewProj = glm::mat4(1.0f);
        glm::mat4 jitteredViewProj = glm::mat4(1.0f);
        glm::mat4 jitteredInvViewProj = glm::mat4(1.0f);
        glm::mat4 invViewProj = glm::mat4(1.0f);
        glm::vec3 cameraPos = glm::vec3(0.0f);
        GameplaySkyRenderer::SkyColors skyColors{};
        GameplaySkyRenderer::SkyIlluminanceData skyIlluminance{};
        float skyIntensity = 1.0f;
        float animationTime = 0.0f;
        float shaderTime = 0.0f;
        bool fogEnabled = true;
        FogMode fogMode = FogMode::Linear;
        glm::vec3 fogColor = glm::vec3(0.67f, 0.84f, 1.0f);
        float fogStart = 0.0f;
        float fogEnd = 1.0f;
        float fogDensity = 0.01f;
        float weatherWetness = 0.0f;
        float weatherStorm = 0.0f;
        float aerialReduction = 0.55f;
        float lightningFlash = 0.0f;
        // Derived weather values — semantic separation for downstream consumers.
        float surfaceWetness = 0.0f;   // terrain/surface effects only
        float skyWetness = 0.0f;       // sky/post/direct rain occlusion
        float fogWetness = 0.0f;       // aerial/volumetric haze weighting
        float cloudWetness = 0.0f;     // cloud coverage/weather shaping
        float precipitation = 0.0f;    // combined rain+snow intensity
        float rainStrength = 0.0f;     // rain particles
        float thunderStrength = 0.0f;  // lightning frequency
        AtmosphereSettings atmosphere{};
        VolumetricSettings volumetric{};
        CloudSettings cloud{};
        bool moonShadowActive = false;
        bool eyeInWater = false;
        float nearPlane = 0.1f;
        float farPlane = 500.0f;
        // Temporal foundation
        uint64_t frameIndex = 0;
        glm::vec2 jitter = glm::vec2(0.0f);
        glm::vec2 previousJitter = glm::vec2(0.0f);
        glm::mat4 previousView = glm::mat4(1.0f);
        glm::mat4 previousProjection = glm::mat4(1.0f);
        glm::mat4 previousViewProj = glm::mat4(1.0f);
        glm::mat4 previousJitteredViewProj = glm::mat4(1.0f);
        glm::mat4 previousInvViewProj = glm::mat4(1.0f);
        float deltaTime = 0.0f;
    };

    void recordMeshingHistory();
    void drainMeshingResults(const World& world);
    void beginFrame(const Camera& camera, const Window &window);   // 设置 VP 矩阵, 清屏
    void renderWorld(const World& world);
    [[nodiscard]] RenderFrameData buildRenderFrameData(const World& world) const;
    [[nodiscard]] FrameContext buildFrameContextFromRenderFrameData(const RenderFrameData& frame) const;
    [[nodiscard]] RenderSettings buildRenderSettingsFromPipelineSettings() const;
    void bindSkyLightingUniforms(Shader& shader, const RenderFrameData& frame) const;
    void bindWeatherUniforms(Shader& shader, const RenderFrameData& frame, bool bindAerialReduction) const;
    void bindFogUniforms(Shader& shader, const RenderFrameData& frame) const;
    void bindAtmosphereUniforms(Shader& shader, const RenderFrameData& frame) const;
    void bindVolumetricUniforms(Shader& shader, const RenderFrameData& frame) const;
    void bindCloudUniforms(Shader& shader, const RenderFrameData& frame) const;
    void bindShadowFrameUniforms(Shader& shader, const RenderFrameData& frame) const;
    void bindSceneCompositeInputs(Shader& shader, const RenderFrameData& frame) const;
    void bindChunkRenderState(const RenderFrameData& frame, const TextureArray& texArray) const;
    void bindChunkRenderStateForShader(const RenderFrameData& frame, const TextureArray& texArray, Shader& shader) const;
    void bindWaterEffectUniforms(Shader& shader, bool enabled) const;
    void bindTransparentCompositeInputs(Shader& shader, bool deferredInputsEnabled, bool compositeInputsEnabled) const;
    void renderWorldForward(const World& world, const RenderFrameData& frame);
    bool renderWorldDeferred(const World& world, const Camera& camera, const Window& window, const RenderFrameData& frame);
    void renderTransparentCompositePass(const World& world, const Window& window);
    void renderWaterCompositePass(const World& world, const Window& window, bool preTemporalResolve = false);
    void renderGBufferTerrain(const World& world, const RenderFrameData& frame);
    void renderGBufferEntities(const World& world, const RenderFrameData& frame);
    void renderGBufferDrops(const World& world, const RenderFrameData& frame);
    void renderShadowMap(const World& world, const Camera& camera, const RenderFrameData& frame);
    void renderShadowEntities(const World& world, const glm::mat4& shadowViewProj);
    void renderShadowDrops(const World& world, const glm::mat4& shadowViewProj,
                           const glm::mat4& shadowView, const glm::mat4& shadowProjection,
                           float animationTime, float shaderTime);
    void renderSsaoPass(const Camera& camera, const Window& window);
    void renderDeferredLightingPass(const RenderFrameData& frame);
    void renderSceneCompositePass(const RenderFrameData& frame);
    void clearDeferredAuxiliaryTargets();
    void renderVelocityPass(const RenderFrameData& frame);
    void updateDeferredHistoryTargets();
    void renderReflectionPass(const RenderFrameData& frame);
    void renderReflectionFilterPass(const RenderFrameData& frame);
    void renderReflectionTemporalPass();
    void renderCloudPass(const RenderFrameData& frame);
    void renderVolumetricFogPass(const RenderFrameData& frame);
    void renderVolumetricTemporalPass(const RenderFrameData& frame);
    void compositeVolumetricFogPass();
    void renderParticlesToSceneResolved(const RenderFrameData& frame);
    void renderTemporalResolvePass(const RenderFrameData& frame);
    void renderSsaoFilterPass();
    void renderSsaoTemporalPass();
    void renderSsaoUpsamplePass();
    void renderMotionBlurPass(const RenderFrameData& frame);
    void renderDofPass(const RenderFrameData& frame);
    void renderDeferredDebugView(GLint framebuffer, int width, int height);
    void renderSkyCapturePass(const World& world);
    void renderFullscreen(Shader& shader) const;
    glm::vec3 currentShadowLightDirection(const World& world, bool* moonShadowActive = nullptr) const;
    void captureCurrentFramebuffer();
    void restoreCapturedFramebufferViewport(const Window& window);
    void submitMeshingJobs(const World& world);
    void renderOpaqueChunksAndCollectPasses(const World& world,
                                            std::vector<ChunkRenderEntry>& cutoutEntries,
                                            std::vector<ChunkRenderEntry>& transparentEntries,
                                            bool frustumCull = true,
                                            float maxCameraDistance = 0.0f,
                                            shadow::ShadowCasterCuller* shadowCuller = nullptr);
    void renderCutoutChunks(const std::vector<ChunkRenderEntry>& cutoutEntries);
    void renderTransparentChunks(const std::vector<ChunkRenderEntry>& transparentEntries);
    void renderTransparentShadowChunks(const std::vector<ChunkRenderEntry>& transparentEntries);
    void addTransparentBatch(const GpuMeshRange& range, float distanceSq, TransparentBatchKind kind);
    void syncChunkRenderColumns(const World& world);
    void refreshChunkRenderColumnCache(ChunkRenderColumnCache& column);
    void syncTerrainCacheFrameStats();
    void clearTransparentBatches();
    void syncTransparentBatches();
    void releaseStaleMdiAllocations(const World& world);
    void releaseMdiAllocation(const SubChunkGpuKey& key);
    void initOutlineMesh();
    void initBreakOverlayMesh();
    void renderBlockOutline(const World& world, const BlockTargetRenderData& target);
    void renderBlockBreakOverlay(const World& world, const BlockBreakRenderData& blockBreak);
#ifdef MECRAFT_DEBUG
    bool isChunkInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax, FrustumPlane* culledPlane) const;
    void recordChunkCull(FrustumPlane plane, int count);
    enum class GpuTimerPass : size_t {
        GBuffer = 0,
        Shadow = 1,
        Ssao = 2,
        Lighting = 3,
        Transparent = 4,
        Volumetric = 5,
        Reflection = 6,
        Cloud = 7,
        Water = 8,
        Post = 9,
        Count = 10
    };
    void initGpuTimers();
    void shutdownGpuTimers();
    void beginGpuTimerFrame();
    void beginGpuTimer(GpuTimerPass pass);
    void endGpuTimer(GpuTimerPass pass);
#endif
    bool isChunkInFrustum(const glm::vec3& chunkMin, const glm::vec3& chunkMax) const;
    //TODO: 传入 World 和 UI 数据进行渲染
    //void renderWorld(const World& world, const Camera& camera);
    //void renderUI(const UI& ui);
    void endFrame(const Window &window);

    int drawCallCount = 0;

    WorldRenderBuffer m_worldRenderBuffer;
    bool m_useMultiDrawIndirect = true;

    Shader* m_chunkShader = nullptr;
    Shader* m_chunkForwardShader = nullptr;
    Shader* m_transparentCompositeShader = nullptr;
    Shader* m_waterCompositeShader = nullptr;
    Shader* m_chunkGBufferShader = nullptr;
    Shader* m_entityGBufferShader = nullptr;
    Shader* m_shadowDepthShader = nullptr;
    Shader* m_entityShadowShader = nullptr;
    Shader* m_deferredLightingShader = nullptr;
    Shader* m_sceneCompositeShader = nullptr;
    Shader* m_deferredDebugShader = nullptr;
    Shader* m_ssaoShader = nullptr;
    Shader* m_velocityShader = nullptr;
    Shader* m_particleGBufferShader = nullptr;
    Shader* m_volumetricFogShader = nullptr;
    Shader* m_volumetricTemporalShader = nullptr;
    Shader* m_volumetricCompositeShader = nullptr;
    Shader* m_reflectionShader = nullptr;
    Shader* m_cloudShader = nullptr;
    Shader* m_bloomExtractShader = nullptr;
    Shader* m_bloomBlurShader = nullptr;
    Shader* m_temporalResolveShader = nullptr;
    Shader* m_reflectionFilterShader = nullptr;
    Shader* m_reflectionTemporalShader = nullptr;
    Shader* m_ssaoFilterShader = nullptr;
    Shader* m_ssaoTemporalShader = nullptr;
    Shader* m_ssaoUpsampleShader = nullptr;
    Shader* m_motionBlurShader = nullptr;
    Shader* m_dofShader = nullptr;
   // Shader* m_uiShader = nullptr;
    Shader* m_outlineShader = nullptr;
    Shader* m_breakOverlayShader = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;

    GLuint m_outlineVao = 0;
    GLuint m_outlineVbo = 0;
    GLuint m_breakOverlayVao = 0;
    GLuint m_breakOverlayVbo = 0;
    GLsizei m_breakOverlayVertexCount = 0;
    GLuint m_breakOverlayCrossVao = 0;
    GLuint m_breakOverlayCrossVbo = 0;
    GLsizei m_breakOverlayCrossVertexCount = 0;

    ThreadPool m_threadPool;
    ChunkMeshingService m_meshingService;
    GameplaySkyRenderer m_gameplaySkyRenderer;
    HumanoidRenderer* m_humanoidRenderer = nullptr;  // injected from Game
    DropRenderer* m_dropRenderer = nullptr;  // injected from Game
    ParticleSystem* m_particleSystem = nullptr;  // injected from Game
    DropSystem* m_dropSystem = nullptr;  // injected from Game
    ecs::GameplayRegistry* m_gameplayRegistry = nullptr;  // injected from Game
    DeferredRenderTargets m_deferredTargets;
    std::unique_ptr<SsaoPass> m_ssaoPass;
    std::unique_ptr<VelocityPass> m_velocityPass;
    std::unique_ptr<ReflectionPass> m_reflectionPass;
    std::unique_ptr<TemporalResolvePass> m_temporalResolvePass;
    std::unique_ptr<MotionBlurPass> m_motionBlurPass;
    std::unique_ptr<DepthOfFieldPass> m_dofPass;
    std::unique_ptr<DeferredLightingPass> m_deferredLightingPass;
    std::unique_ptr<CloudPass> m_cloudPass;
    std::unique_ptr<SceneCompositePass> m_sceneCompositePass;
    std::unique_ptr<VolumetricPass> m_volumetricPass;
    RenderPipelineSettings m_pipelineSettings{};
    bool m_eyeInWater = false;
    int m_heldBlockLightValue = 0;
    RenderFrameData m_currentFrameData{};
    bool m_currentFrameDataValid = false;
    uint64_t m_frameCounter = 0;
    RenderFrameData m_previousFrameData{};
    bool m_hasPreviousFrameData = false;
    GLint m_capturedFramebuffer = 0;
    GLint m_capturedViewport[4] = {0, 0, 0, 0};
    shadow::ShadowRenderer m_shadowRenderer;
    bool m_deferredFrameActive = false;
    bool m_renderLocalPlayerModel = true;
    bool m_waterRenderedBeforeTemporal = false;
    bool m_deferredHistoryUpdatedThisFrame = false;
    std::unordered_set<int64_t> m_meshingInFlight;
    std::vector<SubChunkMeshingResult> m_deferredMeshResults;
    int m_meshingSubmitBudget = 8;
    bool m_meshingSubmitBudgetOverridden = false;
    int m_meshingMaxInFlight = 16;
    double m_meshingSubmitTimeBudgetMs = 0.75;
    int m_meshingDrainBudget = 2;
    double m_meshingDrainTimeBudgetMs = 1.0;
    int m_meshingDrainVertexBudget = 65536;  // ~2MB vertex data per frame upload limit
    int m_regionChunkSize = 4;
#ifdef MECRAFT_DEBUG
    bool m_chunkCullingDebugEnabled = false;
    int m_meshingSubmittedThisFrame = 0;
    int m_meshingCompletedThisFrame = 0;
    int m_meshingStaleDroppedThisFrame = 0;
    double m_meshingBuildMsThisFrame = 0.0;
    double m_lastMeshingBuildMs = 0.0;
    uint32_t m_lastOpaqueFacesBeforeGreedy = 0;
    uint32_t m_lastOpaqueFacesAfterGreedy = 0;
    uint32_t m_lastTransparentFacesBeforeGreedy = 0;
    uint32_t m_lastTransparentFacesAfterGreedy = 0;
    uint32_t m_lastOpaqueVertexCount = 0;
    int m_regionTestsThisFrame = 0;
    int m_regionPassedThisFrame = 0;
    int m_columnTestsThisFrame = 0;
    int m_columnPassedThisFrame = 0;
    int m_chunkTestsThisFrame = 0;
    int m_chunkPassedThisFrame = 0;
    int m_chunkCulledThisFrame = 0;
    std::array<int, static_cast<size_t>(FrustumPlane::Count)> m_chunkCulledByPlaneThisFrame{};
    size_t m_meshingHistoryCount = 0;
    std::array<float, MESHING_HISTORY_SIZE> m_meshingSubmittedHistory{};
    std::array<float, MESHING_HISTORY_SIZE> m_meshingCompletedHistory{};
    std::array<float, MESHING_HISTORY_SIZE> m_meshingInFlightHistory{};
    static constexpr size_t GPU_TIMER_RING_SIZE = 4;
    std::array<std::array<GLuint, static_cast<size_t>(GpuTimerPass::Count)>, GPU_TIMER_RING_SIZE> m_gpuTimerQueries{};
    std::array<std::array<bool, static_cast<size_t>(GpuTimerPass::Count)>, GPU_TIMER_RING_SIZE> m_gpuTimerIssued{};
    GpuFrameStats m_gpuFrameStats{};
    size_t m_gpuTimerWriteIndex = 0;
    bool m_gpuTimersInitialized = false;
    bool m_gpuTimerEnabled = true;
    bool m_gpuTimerActive = false;
    bool m_gpuTimerCanIssueThisFrame = true;
    GpuTimerPass m_activeGpuTimerPass = GpuTimerPass::GBuffer;
    int m_cutoutCandidatesThisFrame = 0;
    int m_cutoutSkippedByDistanceThisFrame = 0;
    int m_mdiSubChunkTestsThisFrame = 0;
    int m_mdiSubChunksCulledThisFrame = 0;
#endif
    size_t m_meshUploadVerticesThisFrame = 0;
    size_t m_meshUploadBytesThisFrame = 0;
    size_t m_meshUploadDeferredCount = 0;
    size_t m_worldBufferExpandCountThisFrame = 0;
    double m_worldBufferUploadMsThisFrame = 0.0;

    glm::mat4 m_projection = glm::mat4(1.0f);
    glm::mat4 m_view = glm::mat4(1.0f);
    glm::mat4 m_viewProj = glm::mat4(1.0f);
    glm::vec3 m_cameraPos = glm::vec3(0.0f);
    float m_nearPlane = 0.1f;
    float m_farPlane = 500.0f;
    FogSettings m_fogSettings{};
    int m_debugLightMode = 0;
    bool m_cutoutDistanceLimitEnabled = true;
    float m_cutoutRenderDistanceChunks = 4.0f;
    // 视锥体6个平面
    std::array<Plane, 6> m_frustumPlanes{};
    TerrainRenderCache m_terrainCache;
    std::vector<ChunkRenderColumnCache> m_chunkRenderColumns;
    std::unordered_map<SubChunkGpuKey, MdiMeshAllocation, SubChunkGpuKeyHash> m_mdiMeshAllocations;
    std::vector<ChunkRenderEntry> m_deferredTransparentEntries;
    std::vector<DrawBatchEntry> m_deferredTransparentBatch;  // MDI path
    TransparentPassPlan m_transparentPassPlan;
    uint64_t m_chunkRenderColumnsRevision = 0;
    int m_chunkRenderColumnsRegionSize = 0;
};



#endif //MECRAFT_RENDERER_H
