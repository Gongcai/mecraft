#ifndef MECRAFT_FRAME_CONTEXT_H
#define MECRAFT_FRAME_CONTEXT_H

#include "renderer/contracts/TemporalFrameContract.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>

// Forward declarations to avoid heavy includes
class IWorldView;
class World;
class Camera;
class Window;
class DayNightSystem;
class WeatherSystem;
class RenderDebugService;
struct SharedRenderResources;

/// Supplies an explicit deterministic frame clock to rendering code.
struct RenderFrameClock {
    uint32_t frameIndex = 0u;
    float deltaTimeSeconds = 0.0f;
    double animationTimeSeconds = 0.0;
    double shaderTimeSeconds = 0.0;
};

/// Camera data for current and previous frame (for temporal effects)
struct CameraData {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::mat4 viewProj = glm::mat4(1.0f);
    glm::mat4 jitteredViewProj = glm::mat4(1.0f);
    glm::mat4 jitteredInvViewProj = glm::mat4(1.0f);
    glm::mat4 invViewProj = glm::mat4(1.0f);
    glm::vec3 position = glm::vec3(0.0f);
    float nearPlane = 0.1f;
    float farPlane = 500.0f;
    float fovDegrees = 70.0f; // Vertical field of view in degrees
};

/// Sky illuminance data from SkyCapture metadata
struct SkyIlluminanceData {
    glm::vec3 directIlluminance = glm::vec3(0.0f);
    glm::vec3 skyIlluminance = glm::vec3(0.0f);
    glm::vec3 sunIlluminance = glm::vec3(0.0f);
    glm::vec3 moonIlluminance = glm::vec3(0.0f);
    glm::vec3 cloudDynamicWeather = glm::vec3(0.0f);
};

/// Sky colors computed from DayNightSystem
struct SkyColorsData {
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

/// Weather state for current frame
struct WeatherData {
    float wetness = 0.0f;
    float storm = 0.0f;
    float surfaceWetness = 0.0f;
    float skyWetness = 0.0f;
    float fogWetness = 0.0f;
    float cloudWetness = 0.0f;
    float precipitation = 0.0f;
    float rainStrength = 0.0f;
    float thunderStrength = 0.0f;
    float lightningFlash = 0.0f;
    float aerialReduction = 0.55f;
};

/// Fog configuration for current frame
struct FogData {
    bool enabled = true;
    int mode = 0; // 0=linear, 1=exp, 2=exp2 (matches GL fog modes)
    glm::vec3 color = glm::vec3(0.67f, 0.84f, 1.0f);
    float startDistance = 140.0f;
    float endDistance = 260.0f;
    float density = 0.01f;
};

/// Volumetric settings for current frame
struct VolumetricData {
    bool lightEnabled = true;
    bool uwLightEnabled = true;
    bool fogEnabled = true;
    float fogStrength = 1.0f;
    float underwaterLightStrength = 0.1f;
    float fogCenterHeight = 63.0f;
    float fogHeightSpread = 100.0f;
    float fogNoiseScale = 0.04f;
    float fogLightStrength = 0.2f;
    float fogDensityScale = 1.0f;
    float baseDensity = 1.0f;
    float maxDistance = 260.0f;
    int fogSamples = 8;
};

/// Cloud settings for current frame
struct CloudData {
    bool shadowsEnabled = true;
    float shadowStrength = 0.0f;
    float shadowScale = 0.0045f;
    float shadowSpeed = 0.018f;
    float timeScale = 0.35f;
    float coverage = 0.35f;
    float density = 1.0f;
    float height = 1000.0f;
    float thickness = 1400.0f;
    float planarCoverage = 0.5f;
    float planarDensity = 1.0f;
    float planarAltitude = 7000.0f;
};

/// Atmosphere settings for current frame
struct AtmosphereData {
    float aerialStrength = 0.65f;
    float horizonScatterStrength = 0.78f;
    float sunWarmth = 0.34f;
    float skyCoolness = 0.18f;
    float directWeatherOcclusion = 1.0f;
    int directWeatherOcclusionOverride = 0;
};

/// Unified frame context passed to all render passes
/// This is the single source of truth for per-frame data
struct FrameContext {
    // Camera state
    CameraData camera;
    CameraData prevCamera;

    // Frame timing
    uint64_t frameIndex = 0;
    float deltaTime = 0.0f;
    float animationTime = 0.0f;
    float shaderTime = 0.0f;

    // Scene-radiance scale applied before finite-precision HDR storage.
    float preExposure = 1.0f;
    float previousPreExposure = 1.0f;

    // Resource, active signal, rendering, and presentation dimensions form one contract.
    TemporalFrameExtents temporalExtents;

    // Current swapchain output target.
    RhiTextureHandle swapchainColorTexture;
    RhiTextureViewHandle swapchainColorView;
    RhiTextureViewHandle swapchainDepthStencilView;
    RhiTextureFormat swapchainColorFormat = RhiTextureFormat::Undefined;
    RhiTextureFormat swapchainDepthStencilFormat = RhiTextureFormat::Undefined;
    RhiTextureHandle sceneCaptureColorTexture;
    RhiTextureHandle sceneCaptureDepthTexture;
    RhiTextureViewHandle sceneCaptureColorView;
    RhiTextureViewHandle sceneCaptureDepthView;

    TemporalJitter jitter;
    TemporalJitter previousJitter;

    // Previous frame matrices (for velocity/temporal passes)
    glm::mat4 previousViewProj = glm::mat4(1.0f);
    glm::mat4 previousInvViewProj = glm::mat4(1.0f);
    glm::mat4 previousJitteredViewProj = glm::mat4(1.0f);
    // Previous view-projection with the CURRENT frame's jitter applied.
    // Velocity passes subtract "current jittered position - previous position";
    // projecting both ends with the same sub-pixel NDC offset cancels the
    // jitter exactly, so the shared velocity buffer stores true motion only.
    glm::mat4 previousViewProjWithCurrentJitter = glm::mat4(1.0f);
    // Double-precision composed reprojection for the velocity pass:
    // previousViewProjWithCurrentJitter * inverse(current raster view-proj).
    // Composing in fp64 on the CPU makes the product collapse to identity for
    // a static camera, so velocity carries no floating-point jitter residue.
    glm::mat4 velocityClipToPrevClip = glm::mat4(1.0f);
    // Rotation-only reprojection for clear-depth pixels representing the
    // infinitely distant sky. Camera translation must not create sky parallax.
    glm::mat4 skyVelocityClipToPrevClip = glm::mat4(1.0f);

    // Sky / Atmosphere
    SkyColorsData skyColors;
    SkyIlluminanceData skyIlluminance;
    float skyIntensity = 1.0f;

    // Weather
    WeatherData weather;

    // Fog
    FogData fog;

    // Volumetric
    VolumetricData volumetric;

    // Cloud
    CloudData cloud;

    // Atmosphere
    AtmosphereData atmosphere;

    // State flags
    bool eyeInWater = false;
    bool moonShadowActive = false;
    TemporalResetReasons temporalResetReasons = temporalResetReasonBit(TemporalResetReason::FirstFrame);
    float cameraRainVisibility = 1.0f; // 0=indoors, 1=outdoors (from multi-ray check)

    // Shared resources (non-owning pointer)
    SharedRenderResources* shared = nullptr;

    // Pointer to world view (for passes that need block/chunk queries)
    const IWorldView* worldView = nullptr;

    // Non-owning pointers to environment systems (for passes that need weather/sky data)
    const DayNightSystem* dayNightSystem = nullptr;
    const WeatherSystem* weatherSystem = nullptr;

    // Pointer to original Camera (for renderers that need Camera object, e.g. GameplaySkyRenderer)
    const Camera* cameraPtr = nullptr;

    // Pointer to original Window for legacy renderer adapters used by forward vanilla.
    const Window* windowPtr = nullptr;

    // Debug service for frame-scoped GPU timers and render statistics.
    RenderDebugService* debugService = nullptr;

    // Whether first-person camera should hide the local player model.
    bool renderLocalPlayerModel = false;
};

#endif // MECRAFT_FRAME_CONTEXT_H
