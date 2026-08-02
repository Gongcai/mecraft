#ifndef MECRAFT_WORLD_LIGHT_GRID_CONTRACT_GLSL
#define MECRAFT_WORLD_LIGHT_GRID_CONTRACT_GLSL

const uint WORLD_LIGHT_GRID_CONTRACT_VERSION = 1u;

struct WorldLightCell {
    ivec4 coordinate;
    uvec4 indexRangeAndVersion;
};

struct WorldLightGridHeader {
    vec4 cellSizeAndInverse;
    uvec4 countsAndVersion;
};

#endif // MECRAFT_WORLD_LIGHT_GRID_CONTRACT_GLSL
