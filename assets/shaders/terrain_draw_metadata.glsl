#ifndef MECRAFT_TERRAIN_DRAW_METADATA_GLSL
#define MECRAFT_TERRAIN_DRAW_METADATA_GLSL

// Matches WorldRenderBuffer::SubChunkDrawMetadata (32-byte std430 stride).
// xyz is the packed vertex origin; w bounds every local coordinate, including
// model geometry extending beyond the nominal sub-chunk. Identity stores IDs.
struct TerrainSubChunkMetadata {
    vec4 originAndFlags;
    uvec4 identity;
};

#endif
