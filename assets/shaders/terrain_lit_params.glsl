layout(std140, set = 1, binding = 15) uniform TerrainLitParams {
    mat4 rhiTerrainLitView;
    mat4 rhiTerrainLitViewProj;
    vec4 rhiTerrainLitCameraAnimation;
    vec4 rhiTerrainLitFogColorStart;
    vec4 rhiTerrainLitFogParams;
    vec4 rhiTerrainLitSunLightColor;
    vec4 rhiTerrainLitSkyAmbientColor;
    vec4 rhiTerrainLitShadowTintColor;
    vec4 rhiTerrainLitHorizonScatterColor;
    vec4 rhiTerrainLitSunDirection;
    vec4 rhiTerrainLitMoonDirection;
    vec4 rhiTerrainLitMoonLightColor;
    vec4 rhiTerrainLitLightingParams0;
    vec4 rhiTerrainLitLightingParams1;
    vec4 rhiTerrainLitAtmosphereParams0;
    vec4 rhiTerrainLitAtmosphereParams1;
    vec4 rhiTerrainLitAtmosphereParams2;
    vec4 rhiTerrainLitAtmosphereParams3;
    vec4 rhiTerrainLitWaterAbsorption;
    vec4 rhiTerrainLitWaterLayers;
    ivec4 rhiTerrainLitControlFlags0;
    ivec4 rhiTerrainLitControlFlags1;
    ivec4 rhiTerrainLitControlFlags2;
    ivec4 rhiTerrainLitWaterFlags;
    uvec4 rhiTerrainLitClusterGrid;
    vec4 rhiTerrainLitClusterDepth;
    uvec4 rhiTerrainLitClusterRenderExtent;
};

#define uCameraPos rhiTerrainLitCameraAnimation.xyz
#define uAnimationTime rhiTerrainLitCameraAnimation.w
#define uFogColor rhiTerrainLitFogColorStart.xyz
#define uFogStart rhiTerrainLitFogColorStart.w
#define uFogEnd rhiTerrainLitFogParams.x
#define uFogDensity rhiTerrainLitFogParams.y
#define uSkyIntensity rhiTerrainLitFogParams.z
#define uMoonVisibility rhiTerrainLitFogParams.w
#define uSunLightColor rhiTerrainLitSunLightColor.xyz
#define uSkyAmbientColor rhiTerrainLitSkyAmbientColor.xyz
#define uShadowTintColor rhiTerrainLitShadowTintColor.xyz
#define uHorizonScatterColor rhiTerrainLitHorizonScatterColor.xyz
#define uSunDirection rhiTerrainLitSunDirection.xyz
#define uMoonDirection rhiTerrainLitMoonDirection.xyz
#define uMoonLightColor rhiTerrainLitMoonLightColor.xyz
#define uDirectSunStrength rhiTerrainLitLightingParams0.x
#define uSkyAmbientStrength rhiTerrainLitLightingParams0.y
#define uWeatherSkylightScale rhiTerrainLitLightingParams0.z
#define uMinimumAmbient rhiTerrainLitLightingParams0.w
#define uBlockLightStrength rhiTerrainLitLightingParams1.x
#define uFakeBounceStrength rhiTerrainLitLightingParams1.y
#define uAlbedoDesaturation rhiTerrainLitLightingParams1.z
#define uShadowDesaturation rhiTerrainLitLightingParams1.w
#define uSunWarmth rhiTerrainLitAtmosphereParams0.x
#define uSkyCoolness rhiTerrainLitAtmosphereParams0.y
#define uAerialStrength rhiTerrainLitAtmosphereParams0.z
#define uHorizonScatterStrength rhiTerrainLitAtmosphereParams0.w
#define uWeatherWetness rhiTerrainLitAtmosphereParams1.x
#define uWeatherStorm rhiTerrainLitAtmosphereParams1.y
#define uAerialReduction rhiTerrainLitAtmosphereParams1.z
#define uLightningFlash rhiTerrainLitAtmosphereParams1.w
#define uSurfaceWetness rhiTerrainLitAtmosphereParams2.x
#define uSkyWetness rhiTerrainLitAtmosphereParams2.y
#define uFogWetness rhiTerrainLitAtmosphereParams2.z
#define uCloudWetness rhiTerrainLitAtmosphereParams2.w
#define uPrecipitation rhiTerrainLitAtmosphereParams3.x
#define uWindTime rhiTerrainLitAtmosphereParams3.y
#define uWaterAbsorption rhiTerrainLitWaterAbsorption.xyz
#define uPreExposure rhiTerrainLitWaterAbsorption.w
#define uWaterStillFirstLayer rhiTerrainLitWaterLayers.x
#define uWaterStillLayerCount rhiTerrainLitWaterLayers.y
#define uWaterFlowFirstLayer rhiTerrainLitWaterLayers.z
#define uWaterFlowLayerCount rhiTerrainLitWaterLayers.w
#define uSkyCaptureEnabled rhiTerrainLitControlFlags0.x
#define uCompositeInputsEnabled rhiTerrainLitControlFlags0.y
#define uWaterCompositeEnabled rhiTerrainLitControlFlags0.z
#define uForceBaseLod rhiTerrainLitControlFlags0.w
#define uDepthSofteningEnabled rhiTerrainLitControlFlags1.x
#define uFogEnabled rhiTerrainLitControlFlags1.y
#define uFogMode rhiTerrainLitControlFlags1.z
#define uDebugLightMode rhiTerrainLitControlFlags1.w
#define uAerialPerspectiveEnabled rhiTerrainLitControlFlags2.x
#define uVolumetricLightEnabled rhiTerrainLitControlFlags2.y
#define uVolumetricFogActive rhiTerrainLitControlFlags2.z
#define uHeldBlockLightValue rhiTerrainLitControlFlags2.w
#define uWaterEffectsEnabled rhiTerrainLitWaterFlags.x
#define uClusterGrid rhiTerrainLitClusterGrid
#define uClusterDepth rhiTerrainLitClusterDepth
#define uClusterRenderExtent rhiTerrainLitClusterRenderExtent
