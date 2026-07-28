#ifndef MECRAFT_STATIC_MESH_MATERIAL_GLSL
#define MECRAFT_STATIC_MESH_MATERIAL_GLSL

#include "material_decode.glsl"

MaterialSample sampleStaticMeshMaterial(vec2 uv) {
    MaterialTextureSamples textures = defaultMaterialTextureSamples();
    textures.baseColor = texture(uBaseColorTexture, uv);
    textures.properties = texture(uMetallicRoughnessTexture, uv);
    textures.occlusion = texture(uOcclusionTexture, uv).r;
    textures.emissive = texture(uEmissiveTexture, uv).rgb;
    if (gpuMaterialHas(uMaterial, GPU_MATERIAL_FLAG_SPECULAR)) {
        textures.specularWeight = texture(uSpecularTexture, uv).a;
        textures.specularColor = texture(uSpecularColorTexture, uv).rgb;
    }
    if (gpuMaterialHas(uMaterial, GPU_MATERIAL_FLAG_CLEARCOAT)) {
        textures.clearcoat = texture(uClearcoatTexture, uv).r;
        textures.clearcoatRoughness =
            texture(uClearcoatRoughnessTexture, uv).g;
    }
    if (gpuMaterialHas(uMaterial, GPU_MATERIAL_FLAG_TRANSMISSION)) {
        textures.transmission = texture(uTransmissionTexture, uv).r;
    }
    if (gpuMaterialHas(uMaterial, GPU_MATERIAL_FLAG_VOLUME)) {
        textures.thickness = texture(uThicknessTexture, uv).g;
    }
    return decodeGltfMaterial(uMaterial, textures);
}

#endif
