#ifndef MECRAFT_GAMEPLAY_SKY_RENDERER_H
#define MECRAFT_GAMEPLAY_SKY_RENDERER_H

#include <cstdint>

#include <glm/glm.hpp>

#include "renderer/rhi/RhiHandles.h"

#include <utility>

class Camera;
class DayNightSystem;
class ResourceMgr;
class RhiDevice;
class Shader;

class GameplaySkyRenderer {
public:
    // DerivativeMain-style atmosphere illuminance values.
    // These are in the shaderpack's internal atmosphere units, not real-world lux.
    struct SkyIlluminanceData {
        glm::vec3 directIlluminance = glm::vec3(0.0f);
        glm::vec3 skyIlluminance    = glm::vec3(0.0f);
        glm::vec3 sunIlluminance    = glm::vec3(0.0f);
        glm::vec3 moonIlluminance   = glm::vec3(0.0f);
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

    void init(ResourceMgr& resourceMgr);
    void shutdown();
    /// Switch to forward vanilla shader (no atmosphere LUT / sky capture / DerivativeMain contract).
    /// Must be called after init(). Reverts to deferred shader if false.
    void setForwardMode(bool forward);
    void render(const Camera& camera, const float aspect, const DayNightSystem& dayNight, uint32_t skyCaptureTexture);
    void renderSkyCapture(const DayNightSystem& dayNight, RhiDevice& rhiDevice, RhiTextureViewHandle targetView,
                          int width, int height,
                          float cameraAltitude, uint32_t atmosphereLutTexture, float moonPhaseFlux,
                          float weatherWetness = 0.0f, float weatherStorm = 0.0f);
    void renderCloudySkyCapture(const DayNightSystem& dayNight, RhiDevice& rhiDevice,
                                RhiTextureViewHandle targetView, int skyCaptureWidth,
                                int skyCaptureHeight, float cameraAltitude, uint32_t atmosphereLutTexture,
                                float moonPhaseFlux, uint32_t noiseTexture, float shaderTime,
                                const SkyIlluminanceData& illuminance,
                                float cloudCoverage, float cloudDensity,
                                float cloudHeight, float cloudThickness,
                                float planarCloudCoverage, float planarCloudDensity,
                                float planarCloudAltitude,
                                float cloudTimeScale, const glm::vec3& cameraPos,
                                float weatherWetness = 0.0f, float weatherStorm = 0.0f);
    void writeSkyCacheMetadata(const SkyIlluminanceData& illuminance, RhiDevice& rhiDevice,
                               RhiTextureViewHandle targetView, int skyCaptureWidth,
                               float cameraAltitude, uint32_t atmosphereLutTexture, float moonPhaseFlux,
                               float weatherWetness = 0.0f, float weatherStorm = 0.0f);

    [[nodiscard]] SkyColors computeSkyColors(const DayNightSystem& dayNight) const;
    [[nodiscard]] SkyIlluminanceData computeSkyIlluminance(const SkyColors& colors,
                                                            float weatherWetness = 0.0f,
                                                            float weatherStorm = 0.0f) const;
    [[nodiscard]] static glm::vec3 computeCloudDynamicWeather(int worldDay, int worldTime);
    [[nodiscard]] glm::vec3 getLastFogColor() const;
    [[nodiscard]] static std::pair<glm::vec2, glm::vec2> getMoonPhaseUv(int phaseIndex);

private:
    struct CloudMeshInfo {
        float tileWorldSize = 0.0f;
        bool valid = false;
    };

    void initMeshes();
    void destroyMeshes();
    void initCloudMesh();
    void ensureDummySkyCaptureTexture();
    void bindDummySkyCaptureTexture(int32_t unit);
    void renderSkyGradient(const Camera& camera, float aspect, const SkyColors& colors, uint32_t skyCaptureTexture);
    void renderClouds(const Camera& camera, float aspect, const DayNightSystem& dayNight, const SkyColors& colors);
    void renderHalo(const Camera& camera, float aspect, const DayNightSystem& dayNight, const SkyColors& colors);
    [[nodiscard]] glm::mat4 buildSkyView(const Camera& camera) const;
    [[nodiscard]] glm::vec3 directionFromAngle(float angleRadians) const;

    Shader* m_shader = nullptr;
    Shader* m_deferredShader = nullptr;  // Original deferred shader (gameplay_sky)
    ResourceMgr* m_resourceMgr = nullptr;
    uint32_t m_dummySkyCaptureTexture = 0;

    uint32_t m_skyVao = 0;
    uint32_t m_skyVbo = 0;
    uint32_t m_haloVao = 0;
    uint32_t m_haloVbo = 0;
    uint32_t m_cloudVao = 0;
    uint32_t m_cloudVbo = 0;
    int32_t m_haloVertexCount = 0;
    int32_t m_cloudVertexCount = 0;
    CloudMeshInfo m_cloudMeshInfo{};

    SkyColors m_lastColors{};
};

#endif // MECRAFT_GAMEPLAY_SKY_RENDERER_H
