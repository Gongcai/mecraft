layout(std140, set = 1, binding = 13) uniform TerrainWaterParams {
    mat4 rhiTerrainWaterView;
    mat4 rhiTerrainWaterViewProj;
    mat4 rhiTerrainWaterInvViewProj;
    vec4 rhiTerrainWaterCameraNear;
    vec4 rhiTerrainWaterAbsorptionFar;
    vec4 rhiTerrainWaterSunDirectionAnimation;
    vec4 rhiTerrainWaterMoonDirectionTime;
    vec4 rhiTerrainWaterSunLightSkyIntensity;
    vec4 rhiTerrainWaterMoonLightVisibility;
    vec4 rhiTerrainWaterSkyAmbientWeather;
    vec4 rhiTerrainWaterWetness;
    vec4 rhiTerrainWaterWaveParams;
    vec4 rhiTerrainWaterLayers;
    ivec4 rhiTerrainWaterControlFlags0;
    ivec4 rhiTerrainWaterControlFlags1;
    ivec4 rhiTerrainWaterControlFlags2;
    vec4 rhiTerrainWaterPreExposure;
};

#define uWaterViewProj rhiTerrainWaterViewProj
#define uView rhiTerrainWaterView
#define uInvViewProj rhiTerrainWaterInvViewProj
#define uCameraPos rhiTerrainWaterCameraNear.xyz
#define uNearPlane rhiTerrainWaterCameraNear.w
#define uWaterAbsorption rhiTerrainWaterAbsorptionFar.xyz
#define uFarPlane rhiTerrainWaterAbsorptionFar.w
#define uSunDirection rhiTerrainWaterSunDirectionAnimation.xyz
#define uAnimationTime rhiTerrainWaterSunDirectionAnimation.w
#define uMoonDirection rhiTerrainWaterMoonDirectionTime.xyz
#define uTime rhiTerrainWaterMoonDirectionTime.w
#define uSunLightColor rhiTerrainWaterSunLightSkyIntensity.xyz
#define uSkyIntensity rhiTerrainWaterSunLightSkyIntensity.w
#define uMoonLightColor rhiTerrainWaterMoonLightVisibility.xyz
#define uMoonVisibility rhiTerrainWaterMoonLightVisibility.w
#define uSkyAmbientColor rhiTerrainWaterSkyAmbientWeather.xyz
#define uWeatherWetness rhiTerrainWaterSkyAmbientWeather.w
#define uSkyWetness rhiTerrainWaterWetness.x
#define uFogWetness rhiTerrainWaterWetness.y
#define uCloudWetness rhiTerrainWaterWetness.z
#define uSurfaceWetness rhiTerrainWaterWetness.w
#define uWaterWaveHeight rhiTerrainWaterWaveParams.x
#define uWaterWaveSpeed rhiTerrainWaterWaveParams.y
#define uWaterIOR rhiTerrainWaterWaveParams.z
#define uMoonPhaseFlux rhiTerrainWaterWaveParams.w
#define uWaterStillFirstLayer rhiTerrainWaterLayers.x
#define uWaterStillLayerCount rhiTerrainWaterLayers.y
#define uWaterFlowFirstLayer rhiTerrainWaterLayers.z
#define uWaterFlowLayerCount rhiTerrainWaterLayers.w
#define uSkyCaptureEnabled rhiTerrainWaterControlFlags0.x
#define uCompositeInputsEnabled rhiTerrainWaterControlFlags0.y
#define uWaterCompositeEnabled rhiTerrainWaterControlFlags0.z
#define uDepthSofteningEnabled rhiTerrainWaterControlFlags0.w
#define uVolumetricFogActive rhiTerrainWaterControlFlags1.x
#define uFrameIndex rhiTerrainWaterControlFlags1.y
#define uFreezeBias rhiTerrainWaterControlFlags1.z
#define uRainSurfaceRipplesEnabled rhiTerrainWaterControlFlags1.w
#define uIsEyeInWater rhiTerrainWaterControlFlags2.x
#define uPreExposure rhiTerrainWaterPreExposure.x
