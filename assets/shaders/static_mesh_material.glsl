#ifndef MECRAFT_STATIC_MESH_MATERIAL_GLSL
#define MECRAFT_STATIC_MESH_MATERIAL_GLSL

struct StaticMeshMaterialSample {
    vec4 baseColor;
    float metalness;
    float roughness;
};

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
    vec4 colorSample = texture(uBaseColorTexture, uv);
    vec4 propertySample = texture(uMetallicRoughnessTexture, uv);
    if (uMaterialFlags.y == 0) {
        material.baseColor = colorSample * uBaseColorFactor;
        material.metalness = clamp(
            propertySample.b * uMaterialFactors.x, 0.0, 1.0);
        material.roughness = clamp(
            propertySample.g * uMaterialFactors.y, 0.02, 1.0);
        return material;
    }

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
    return material;
}

#endif
