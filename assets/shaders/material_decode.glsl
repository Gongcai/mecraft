#ifndef MECRAFT_MATERIAL_DECODE_GLSL
#define MECRAFT_MATERIAL_DECODE_GLSL

#include "gpu_material_contract.glsl"
#include "pbr_brdf.glsl"

struct MaterialTextureSamples {
    vec4 baseColor;
    vec4 properties;
    float occlusion;
    vec3 emissive;
    float specularWeight;
    vec3 specularColor;
    float clearcoat;
    float clearcoatRoughness;
    float transmission;
    float thickness;
};

struct MaterialSample {
    vec4 baseColor;
    float metalness;
    float perceptualRoughness;
    float occlusion;
    vec3 emissive;
    vec3 dielectricF0;
    float specularF90;
    float clearcoat;
    float clearcoatPerceptualRoughness;
    float transmission;
    float thickness;
    vec3 attenuationColor;
    float attenuationDistance;
    bool attenuationEnabled;
    float ior;
};

MaterialTextureSamples defaultMaterialTextureSamples() {
    MaterialTextureSamples samples;
    samples.baseColor = vec4(1.0);
    samples.properties = vec4(1.0);
    samples.occlusion = 1.0;
    samples.emissive = vec3(1.0);
    samples.specularWeight = 1.0;
    samples.specularColor = vec3(1.0);
    samples.clearcoat = 1.0;
    samples.clearcoatRoughness = 1.0;
    samples.transmission = 1.0;
    samples.thickness = 1.0;
    return samples;
}

float materialPerceivedBrightness(vec3 color) {
    return sqrt(0.299 * color.r * color.r +
                0.587 * color.g * color.g +
                0.114 * color.b * color.b);
}

float solveSpecularGlossinessMetalness(
    vec3 diffuse,
    vec3 specular,
    float oneMinusSpecularStrength) {
    const float dielectricSpecular = 0.04;
    float specularBrightness = materialPerceivedBrightness(specular);
    if (specularBrightness < dielectricSpecular) {
        return 0.0;
    }
    float diffuseBrightness = materialPerceivedBrightness(diffuse);
    float a = dielectricSpecular;
    float b = diffuseBrightness * oneMinusSpecularStrength /
                  (1.0 - dielectricSpecular) +
              specularBrightness - 2.0 * dielectricSpecular;
    float c = dielectricSpecular - specularBrightness;
    float discriminant = max(b * b - 4.0 * a * c, 0.0);
    return clamp((-b + sqrt(discriminant)) / (2.0 * a), 0.0, 1.0);
}

vec3 decodeMaterialTangentNormal(vec3 encodedNormal, float scale) {
    vec3 tangentNormal = encodedNormal * 2.0 - 1.0;
    tangentNormal.xy *= scale;
    return normalize(tangentNormal);
}

bool materialPassesAlphaTest(GpuMaterial material, float alpha) {
    return material.modesAndFlags.x != GPU_MATERIAL_ALPHA_MASK ||
           alpha >= material.transmissionVolumeFactors.z;
}

vec3 evaluateMaterialEmission(MaterialSample material) {
    return material.emissive;
}

MaterialSample decodeGltfMaterial(
    GpuMaterial gpuMaterial,
    MaterialTextureSamples textures) {
    MaterialSample material;
    material.baseColor = vec4(1.0);
    material.metalness = 0.0;
    material.perceptualRoughness = 1.0;
    material.occlusion = mix(
        1.0, textures.occlusion, gpuMaterial.materialFactors.w);
    material.emissive = textures.emissive *
                        gpuMaterial.emissiveFactorAndStrength.rgb *
                        gpuMaterial.emissiveFactorAndStrength.w;
    material.dielectricF0 = vec3(0.04);
    material.specularF90 = 1.0;
    material.clearcoat = 0.0;
    material.clearcoatPerceptualRoughness = 0.0;
    material.transmission = 0.0;
    material.thickness = 0.0;
    material.attenuationColor = vec3(1.0);
    material.attenuationDistance = 0.0;
    material.attenuationEnabled = false;
    material.ior = gpuMaterialHas(gpuMaterial, GPU_MATERIAL_FLAG_IOR)
        ? gpuMaterial.transmissionVolumeFactors.w
        : 1.5;

    if (gpuMaterial.modesAndFlags.y ==
        GPU_MATERIAL_WORKFLOW_METALLIC_ROUGHNESS) {
        material.baseColor =
            textures.baseColor * gpuMaterial.baseColorFactor;
        material.metalness = clamp(
            textures.properties.b * gpuMaterial.materialFactors.x,
            0.0, 1.0);
        material.perceptualRoughness = clamp(
            textures.properties.g * gpuMaterial.materialFactors.y,
            0.02, 1.0);
    } else {
        const float dielectricSpecular = 0.04;
        vec4 diffuse = textures.baseColor * gpuMaterial.baseColorFactor;
        vec3 specular = textures.properties.rgb *
                        gpuMaterial.specularGlossinessFactors.rgb;
        float oneMinusSpecularStrength =
            1.0 - max(max(specular.r, specular.g), specular.b);
        float metalness = solveSpecularGlossinessMetalness(
            diffuse.rgb, specular, oneMinusSpecularStrength);
        vec3 baseColorFromDiffuse = diffuse.rgb *
            (oneMinusSpecularStrength / (1.0 - dielectricSpecular)) /
            max(1.0 - metalness, 1e-6);
        vec3 baseColorFromSpecular =
            (specular - dielectricSpecular * (1.0 - metalness)) /
            max(metalness, 1e-6);
        material.baseColor = vec4(
            clamp(mix(baseColorFromDiffuse, baseColorFromSpecular,
                      metalness * metalness), 0.0, 1.0),
            diffuse.a);
        material.metalness = metalness;
        material.perceptualRoughness = clamp(
            1.0 - textures.properties.a *
                      gpuMaterial.specularGlossinessFactors.a,
            0.02, 1.0);
    }

    float iorF0 = pbrDielectricF0FromIor(material.ior);
    if (gpuMaterialHas(gpuMaterial, GPU_MATERIAL_FLAG_SPECULAR)) {
        float specularWeight = clamp(
            textures.specularWeight *
                gpuMaterial.dielectricSpecularFactors.a,
            0.0, 1.0);
        vec3 specularColor = textures.specularColor *
                             gpuMaterial.dielectricSpecularFactors.rgb;
        material.dielectricF0 = clamp(
            vec3(iorF0 * specularWeight) * specularColor,
            vec3(0.0), vec3(1.0));
        material.specularF90 = specularWeight;
    } else {
        material.dielectricF0 = vec3(iorF0);
    }
    if (gpuMaterialHas(gpuMaterial, GPU_MATERIAL_FLAG_CLEARCOAT)) {
        material.clearcoat = clamp(
            textures.clearcoat * gpuMaterial.clearcoatFactors.x,
            0.0, 1.0);
        material.clearcoatPerceptualRoughness = clamp(
            textures.clearcoatRoughness * gpuMaterial.clearcoatFactors.y,
            0.02, 1.0);
    }
    if (gpuMaterialHas(gpuMaterial, GPU_MATERIAL_FLAG_TRANSMISSION)) {
        material.transmission = clamp(
            textures.transmission *
                gpuMaterial.transmissionVolumeFactors.x,
            0.0, 1.0);
    }
    if (gpuMaterialHas(gpuMaterial, GPU_MATERIAL_FLAG_VOLUME)) {
        material.thickness = max(
            textures.thickness * gpuMaterial.transmissionVolumeFactors.y,
            0.0);
        material.attenuationColor = clamp(
            gpuMaterial.attenuationColorAndDistance.rgb,
            vec3(0.0), vec3(1.0));
        material.attenuationDistance =
            gpuMaterial.attenuationColorAndDistance.w;
        material.attenuationEnabled = !gpuMaterialHas(
            gpuMaterial,
            GPU_MATERIAL_FLAG_INFINITE_ATTENUATION_DISTANCE);
    }
    return material;
}

#endif // MECRAFT_MATERIAL_DECODE_GLSL
