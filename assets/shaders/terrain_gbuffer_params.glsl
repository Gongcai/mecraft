layout(std140, binding = 13) uniform TerrainGBufferParams {
    mat4 rhiTerrainViewProj;
    vec4 rhiTerrainCameraAnimation;
    vec4 rhiTerrainSurfaceParams;
    ivec4 rhiTerrainMaterialFlags;
    ivec4 rhiTerrainWeatherFlags;
};
