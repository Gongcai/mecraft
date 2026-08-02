#ifndef MECRAFT_TERRAIN_MATERIAL_SAMPLING_GLSL
#define MECRAFT_TERRAIN_MATERIAL_SAMPLING_GLSL

const float TERRAIN_ALPHA_CUTOFF = 0.1;
const uint TERRAIN_MATERIAL_TEXTURE_LAYER_CAPACITY = 1024u;

// Applies the fixed opacity boundary shared by terrain raster and ray-query consumers.
bool terrainAlphaTestPasses(float opacity) {
    return !isnan(opacity) && !isinf(opacity) && opacity >= TERRAIN_ALPHA_CUTOFF;
}

// Resolves the deterministic texture-array layer encoded by one validated terrain vertex.
float terrainAnimatedTextureLayer(float firstLayer, float frameCount, float framesPerSecond, float animated,
                                  float animationTimeSeconds) {
    float frame = mod(floor(animationTimeSeconds * framesPerSecond), frameCount);
    return firstLayer + (animated > 0.5 ? frame : 0.0);
}

#endif // MECRAFT_TERRAIN_MATERIAL_SAMPLING_GLSL
