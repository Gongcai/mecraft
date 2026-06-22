struct TerrainVertexDecoded {
    vec3 pos;
    vec2 uv;
    float normal;
    float sunlight;
    float blockLight;
    float ao;
    float layer;
    float animationFrameCount;
    float animationFps;
    float animated;
    uint tintPacked;
};

layout(std430, binding = 0) readonly buffer TerrainSubChunkMetadataBuffer {
    vec4 terrainSubChunkOriginAndFlags[];
};

float decodePackedNormal(uint value) {
    if (value == 6u) return -1.0;
    if (value == 7u) return -2.0;
    return float(value);
}

TerrainVertexDecoded decodeTerrainPackedVertex(uint posPacked,
                                                uint uvPacked,
                                                uint lightAoLayer,
                                                uint tintAnim,
                                                uint metadataIndex) {
    TerrainVertexDecoded v;
    uint x = (posPacked >> 20u) & 0x0FFFu;
    uint y = (posPacked >> 8u) & 0x0FFFu;
    uint z = ((posPacked & 0xFFu) << 4u) | ((lightAoLayer >> 28u) & 0x0Fu);
    vec3 localPos = vec3(float(x), float(y), float(z)) * (1.0 / 128.0);
    v.pos = terrainSubChunkOriginAndFlags[metadataIndex].xyz + localPos;
    ivec2 uvFixed = ivec2(int((uvPacked >> 16u) & 0xFFFFu),
                          int(uvPacked & 0xFFFFu));
    uvFixed = ivec2((uvFixed.x >= 32768) ? uvFixed.x - 65536 : uvFixed.x,
                    (uvFixed.y >= 32768) ? uvFixed.y - 65536 : uvFixed.y);
    v.uv = vec2(uvFixed) * (1.0 / 1024.0);
    v.normal = decodePackedNormal((lightAoLayer >> 25u) & 0x07u);
    v.sunlight = float((lightAoLayer >> 17u) & 0xFFu) * (1.0 / 255.0);
    v.blockLight = float((lightAoLayer >> 9u) & 0xFFu) * (1.0 / 255.0);
    v.ao = float((lightAoLayer >> 7u) & 0x03u);
    uint layerLow = lightAoLayer & 0x7Fu;
    uint layerHigh = (tintAnim >> 16u) & 0x07u;
    v.layer = float(layerLow | (layerHigh << 7u));
    v.animationFrameCount = float((tintAnim >> 26u) & 0x3Fu);
    v.animationFps = float((tintAnim >> 20u) & 0x3Fu);
    v.animated = float((tintAnim >> 19u) & 0x01u);
    v.tintPacked = tintAnim & 0xFFFFu;
    return v;
}

TerrainVertexDecoded decodeLegacyBlockVertex(vec3 pos,
                                              vec2 uv,
                                              float normal,
                                              float sunlight,
                                              float blockLight,
                                              float ao,
                                              float layer,
                                              float animationFrameCount,
                                              float animationFps,
                                              float animated,
                                              uint tintPacked) {
    TerrainVertexDecoded v;
    v.pos = pos;
    v.uv = uv;
    v.normal = normal;
    v.sunlight = sunlight;
    v.blockLight = blockLight;
    v.ao = ao;
    v.layer = layer;
    v.animationFrameCount = animationFrameCount;
    v.animationFps = animationFps;
    v.animated = animated;
    v.tintPacked = tintPacked;
    return v;
}
