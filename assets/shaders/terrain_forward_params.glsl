layout(std140, binding = 5) uniform TerrainForwardParams {
    mat4 rhiTerrainForwardView;
    mat4 rhiTerrainForwardViewProj;
    vec4 rhiTerrainForwardAnimationSky;
    vec4 rhiTerrainForwardFogColorStart;
    vec4 rhiTerrainForwardFogParams;
    ivec4 rhiTerrainForwardControlFlags;
};

#define uAnimationTime rhiTerrainForwardAnimationSky.x
#define uSkyIntensity rhiTerrainForwardAnimationSky.y
#define uFogColor rhiTerrainForwardFogColorStart.xyz
#define uFogStart rhiTerrainForwardFogColorStart.w
#define uFogEnd rhiTerrainForwardFogParams.x
#define uFogDensity rhiTerrainForwardFogParams.y
#define uForceBaseLod rhiTerrainForwardControlFlags.x
#define uFogEnabled rhiTerrainForwardControlFlags.y
#define uFogMode rhiTerrainForwardControlFlags.z
#define uDebugLightMode rhiTerrainForwardControlFlags.w
