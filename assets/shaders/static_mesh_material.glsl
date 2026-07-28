#ifndef MECRAFT_STATIC_MESH_MATERIAL_GLSL
#define MECRAFT_STATIC_MESH_MATERIAL_GLSL

struct StaticMeshMaterialSample {
    vec4 baseColor;
    float metalness;
    float roughness;
    vec3 dielectricF0;
    float specularF90;
    float clearcoat;
    float clearcoatRoughness;
    float transmission;
    float thickness;
    vec3 attenuationColor;
    float attenuationDistance;
    float ior;
};

const int STATIC_MATERIAL_SPECULAR = 1 << 0;
const int STATIC_MATERIAL_IOR = 1 << 1;
const int STATIC_MATERIAL_CLEARCOAT = 1 << 2;
const int STATIC_MATERIAL_TRANSMISSION = 1 << 3;
const int STATIC_MATERIAL_VOLUME = 1 << 4;

bool staticMeshMaterialHas(int extensionBit) {
    return (uMaterialFlags.w & extensionBit) != 0;
}

float staticMeshPerceivedBrightness(vec3 color) {
    return sqrt(0.299 * color.r * color.r +
                0.587 * color.g * color.g +
                0.114 * color.b * color.b);
}

float staticMeshSolveMetallic(vec3 diffuse,
                              vec3 specular,
                              float oneMinusSpecularStrength) {
    const float dielectricSpecular = 0.04;
    float specularBrightness = staticMeshPerceivedBrightness(specular);
    if (specularBrightness < dielectricSpecular) {
        return 0.0;
    }
    float diffuseBrightness = staticMeshPerceivedBrightness(diffuse);
    float a = dielectricSpecular;
    float b = diffuseBrightness * oneMinusSpecularStrength /
                  (1.0 - dielectricSpecular) +
              specularBrightness - 2.0 * dielectricSpecular;
    float c = dielectricSpecular - specularBrightness;
    float discriminant = max(b * b - 4.0 * a * c, 0.0);
    return clamp((-b + sqrt(discriminant)) / (2.0 * a), 0.0, 1.0);
}

StaticMeshMaterialSample sampleStaticMeshMaterial(vec2 uv) {
    StaticMeshMaterialSample material;
    material.dielectricF0 = vec3(0.04);
    material.specularF90 = 1.0;
    material.clearcoat = 0.0;
    material.clearcoatRoughness = 0.0;
    material.transmission = 0.0;
    material.thickness = 0.0;
    material.attenuationColor = vec3(1.0);
    material.attenuationDistance = 0.0;
    material.ior = staticMeshMaterialHas(STATIC_MATERIAL_IOR)
        ? uTransmissionVolumeFactors.w : 1.5;
    vec4 colorSample = texture(uBaseColorTexture, uv);
    vec4 propertySample = texture(uMetallicRoughnessTexture, uv);
    if (uMaterialFlags.y == 0) {
        material.baseColor = colorSample * uBaseColorFactor;
        material.metalness = clamp(
            propertySample.b * uMaterialFactors.x, 0.0, 1.0);
        material.roughness = clamp(
            propertySample.g * uMaterialFactors.y, 0.02, 1.0);
    } else {
        const float dielectricSpecular = 0.04;
        vec4 diffuse = colorSample * uBaseColorFactor;
        vec3 specular = propertySample.rgb * uWorkflowFactors.rgb;
        float oneMinusSpecularStrength =
            1.0 - max(max(specular.r, specular.g), specular.b);
        float metalness = staticMeshSolveMetallic(
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
        material.roughness = clamp(
            1.0 - propertySample.a * uWorkflowFactors.a, 0.02, 1.0);
    }

    float iorF0 = pow((material.ior - 1.0) / (material.ior + 1.0), 2.0);
    if (staticMeshMaterialHas(STATIC_MATERIAL_SPECULAR)) {
        float specularWeight = clamp(
            texture(uSpecularTexture, uv).a * uSpecularFactors.a,
            0.0, 1.0);
        vec3 specularColor = texture(uSpecularColorTexture, uv).rgb *
                             uSpecularFactors.rgb;
        material.dielectricF0 = clamp(
            vec3(iorF0 * specularWeight) * specularColor,
            vec3(0.0), vec3(1.0));
        material.specularF90 = specularWeight;
    } else {
        material.dielectricF0 = vec3(iorF0);
    }
    if (staticMeshMaterialHas(STATIC_MATERIAL_CLEARCOAT)) {
        material.clearcoat = clamp(
            texture(uClearcoatTexture, uv).r * uClearcoatFactors.x,
            0.0, 1.0);
        material.clearcoatRoughness = clamp(
            texture(uClearcoatRoughnessTexture, uv).g *
                uClearcoatFactors.y,
            0.02, 1.0);
    }
    if (staticMeshMaterialHas(STATIC_MATERIAL_TRANSMISSION)) {
        material.transmission = clamp(
            texture(uTransmissionTexture, uv).r *
                uTransmissionVolumeFactors.x,
            0.0, 1.0);
    }
    if (staticMeshMaterialHas(STATIC_MATERIAL_VOLUME)) {
        material.thickness = max(
            texture(uThicknessTexture, uv).g *
                uTransmissionVolumeFactors.y,
            0.0);
        material.attenuationColor = clamp(
            uAttenuationColorDistance.rgb, vec3(0.0), vec3(1.0));
        material.attenuationDistance = uAttenuationColorDistance.w;
    }
    return material;
}

#endif
