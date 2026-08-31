#ifndef MECRAFT_GAMEPLAY_SKY_RENDERER_H
#define MECRAFT_GAMEPLAY_SKY_RENDERER_H

#include <cstdint>

#include <glm/glm.hpp>

#include "renderer/rhi/RhiHandles.h"

#include <utility>

class Camera;
class DayNightSystem;
class RhiDevice;
class RhiCommandList;

class GameplaySkyRenderer {
public:
    // DerivativeMain-style atmosphere illuminance values.
    // These are in the shaderpack's internal atmosphere units, not real-world lux.
    struct SkyIlluminanceData {
        glm::vec3 directIlluminance = glm::vec3(0.0f);
        glm::vec3 skyIlluminance = glm::vec3(0.0f);
        glm::vec3 sunIlluminance = glm::vec3(0.0f);
        glm::vec3 moonIlluminance = glm::vec3(0.0f);
        glm::vec3 cloudDynamicWeather = glm::vec3(0.0f);
    };

    struct SkyColors {
        glm::vec3 top = glm::vec3(0.55f, 0.75f, 1.0f);
        glm::vec3 horizon = glm::vec3(0.70f, 0.86f, 1.0f);
        glm::vec3 fog = glm::vec3(0.70f, 0.86f, 1.0f);
        glm::vec4 halo = glm::vec4(1.0f, 0.42f, 0.10f, 0.0f);
        glm::vec3 sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 sunScatter = glm::vec3(1.0f, 0.55f, 0.20f);
        glm::vec3 sunLightColor = glm::vec3(1.0f);
        glm::vec3 skyAmbientColor = glm::vec3(0.70f, 0.86f, 1.0f);
        glm::vec3 shadowTintColor = glm::vec3(0.55f, 0.62f, 0.88f);
        glm::vec3 horizonScatterColor = glm::vec3(0.80f, 0.88f, 1.0f);
        glm::vec3 cloudColor = glm::vec3(1.0f);
        glm::vec3 moonDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        glm::vec3 moonLightColor = glm::vec3(0.32f, 0.40f, 0.70f);
        float haloStrength = 0.0f;
        float horizonHaze = 0.35f;
        float sunGlare = 0.0f;
        float sunVisibility = 1.0f;
        float moonVisibility = 0.0f;
        float moonPhaseAngle = 0.0f;
        float dayFactor = 1.0f;
        float nightFactor = 0.0f;
        float horizonFactor = 0.0f;
        float rainFactor = 0.0f;
        float wetnessFactor = 0.0f;
        float cloudinessFactor = 0.0f;
    };

    struct CloudySkyCaptureParams {
        float cameraAltitude = 0.0f;
        float moonPhaseFlux = 0.0f;
        float shaderTime = 0.0f;
        float cloudTimeScale = 1.0f;
        float cloudCoverage = 1.0f;
        float cloudDensity = 1.0f;
        float cloudHeight = 1000.0f;
        float cloudThickness = 1400.0f;
        float planarCloudCoverage = 0.5f;
        float planarCloudDensity = 1.0f;
        float planarCloudAltitude = 7000.0f;
        float weatherWetness = 0.0f;
        float weatherStorm = 0.0f;
        float skyWetness = 0.0f;
        float fogWetness = 0.0f;
        float cloudWetness = 0.0f;
        float surfaceWetness = 0.0f;
        float precipitation = 0.0f;
        glm::vec3 cameraPosition = glm::vec3(0.0f);
    };

    void init(RhiDevice& rhiDevice);
    void shutdown();
    void render(const Camera& camera, float aspect, const DayNightSystem& dayNight, RhiCommandList& commandList);
    void renderCloudySkyCapture(const SkyColors& colors, RhiCommandList& commandList, RhiTextureViewHandle targetView,
                                int skyCaptureWidth, int skyCaptureHeight, RhiTextureViewHandle atmosphereLutView,
                                RhiTextureHandle noiseTexture, const SkyIlluminanceData& illuminance,
                                const CloudySkyCaptureParams& params);
    [[nodiscard]] SkyColors computeSkyColors(const DayNightSystem& dayNight) const;
    [[nodiscard]] SkyIlluminanceData computeSkyIlluminance(const SkyColors& colors, float weatherWetness = 0.0f,
                                                           float weatherStorm = 0.0f) const;
    [[nodiscard]] static glm::vec3 computeCloudDynamicWeather(int worldDay, int worldTime);
    [[nodiscard]] glm::vec3 getLastFogColor() const;
    [[nodiscard]] static std::pair<glm::vec2, glm::vec2> getMoonPhaseUv(int phaseIndex);

private:
    struct alignas(16) CaptureUniforms {
        glm::vec4 skyTopHaze{0.0f};
        glm::vec4 skyHorizonGlare{0.0f};
        glm::vec4 sunDirectionVisibility{0.0f};
        glm::vec4 moonDirectionVisibility{0.0f};
        glm::vec4 sunScatterNight{0.0f};
        glm::vec4 moonLightPhaseFlux{0.0f};
        glm::vec4 directIlluminanceAltitude{0.0f};
        glm::vec4 skyIlluminanceTime{0.0f};
        glm::vec4 sunIlluminanceTimeScale{0.0f};
        glm::vec4 moonIlluminanceCoverage{0.0f};
        glm::vec4 cloudDynamicWeatherDensity{0.0f};
        glm::vec4 cloudGeometry{0.0f};
        glm::vec4 planarClouds{0.0f};
        glm::vec4 weather{0.0f};
        glm::vec4 wetness0{0.0f};
        glm::vec4 wetness1{0.0f};
        glm::vec4 cameraPosition{0.0f};
    };
    static_assert(sizeof(CaptureUniforms) == 272u);

    struct CloudMeshInfo {
        float tileWorldSize = 0.0f;
        bool valid = false;
    };

    void initMeshes();
    void destroyMeshes();
    void initCloudMesh();
    void synchronizeCaptureResources(RhiTextureViewHandle atmosphereLutView, RhiTextureHandle noiseTexture);
    void renderClouds(const Camera& camera, float aspect, const DayNightSystem& dayNight, const SkyColors& colors,
                      RhiCommandList& commandList);
    void renderHalo(const Camera& camera, float aspect, const DayNightSystem& dayNight, const SkyColors& colors,
                    RhiCommandList& commandList);
    [[nodiscard]] glm::mat4 buildSkyView(const Camera& camera) const;
    [[nodiscard]] glm::vec3 directionFromAngle(float angleRadians) const;

    
    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_skyVertexBuffer;
    RhiBufferHandle m_haloVertexBuffer;
    RhiBufferHandle m_cloudVertexBuffer;
    RhiTextureHandle m_captureNoiseTexture;
    RhiTextureViewHandle m_captureNoiseView;
    RhiBufferHandle m_captureUniformBuffer;
    RhiSamplerHandle m_captureSampler;
    RhiBindGroupLayoutHandle m_captureBindGroupLayout;
    RhiBindGroupHandle m_captureBindGroup;
    RhiPipelineLayoutHandle m_capturePipelineLayout;
    RhiShaderHandle m_captureVertexShader;
    RhiShaderHandle m_captureFragmentShader;
    RhiPipelineHandle m_capturePipeline;
    RhiShaderHandle m_visibleVertexShader;
    RhiShaderHandle m_visibleFragmentShader;
    RhiPipelineLayoutHandle m_visiblePipelineLayout;
    RhiPipelineHandle m_visiblePipeline;
    RhiShaderHandle m_haloVertexShader;
    RhiShaderHandle m_haloFragmentShader;
    RhiPipelineLayoutHandle m_haloPipelineLayout;
    RhiPipelineHandle m_haloPipeline;
    RhiShaderHandle m_cloudVertexShader;
    RhiShaderHandle m_cloudFragmentShader;
    RhiPipelineLayoutHandle m_cloudPipelineLayout;
    RhiPipelineHandle m_cloudPipeline;
    RhiTextureViewHandle m_captureAtmosphereLutView;
    int32_t m_haloVertexCount = 0;
    int32_t m_cloudVertexCount = 0;
    CloudMeshInfo m_cloudMeshInfo{};

    SkyColors m_lastColors{};
};

#endif // MECRAFT_GAMEPLAY_SKY_RENDERER_H
