#ifndef MECRAFT_LAB_PBR_MATERIAL_GLSL
#define MECRAFT_LAB_PBR_MATERIAL_GLSL

const int LAB_PBR_VERSION = 13;
const ivec4 LAB_PBR_INTERNAL_NEUTRAL_SPECULAR = ivec4(0, 254, 0, 255);

struct LabPbrNormalSample {
    vec3 tangentNormal;
    float materialAo;
    float height;
};

struct LabPbrSpecularSample {
    float perceptualRoughness;
    vec3 f0;
    float metalness;
    float porosity;
    float subsurface;
    float emission;
    bool emissionProvided;
    int metalId;
};

LabPbrNormalSample decodeLabPbrNormal(vec4 texel) {
    LabPbrNormalSample decoded;
    vec2 encoded = texel.rg * 2.0 - 1.0;
    encoded.y = -encoded.y;
    decoded.tangentNormal = normalize(vec3(
        encoded,
        sqrt(max(1.0 - dot(encoded, encoded), 0.0))));
    decoded.materialAo = clamp(texel.b, 0.0, 1.0);
    decoded.height = clamp(texel.a, 0.0, 1.0);
    return decoded;
}

bool isLabPbrInternalNeutralSpecular(vec4 texel) {
    return all(equal(ivec4(round(clamp(texel, 0.0, 1.0) * 255.0)),
                     LAB_PBR_INTERNAL_NEUTRAL_SPECULAR));
}

const vec3 LAB_PBR_DEFINED_METAL_F0[8] = vec3[8](
    vec3(0.531229, 0.512357, 0.495829),
    vec3(0.944230, 0.776102, 0.373402),
    vec3(0.912298, 0.913851, 0.919681),
    vec3(0.555597, 0.554537, 0.554779),
    vec3(0.925952, 0.720902, 0.504154),
    vec3(0.632484, 0.625937, 0.641479),
    vec3(0.678849, 0.642401, 0.588410),
    vec3(0.962000, 0.949468, 0.922116));

vec3 decodeLabPbrF0(float encodedF0OrMetalId, vec3 baseColor) {
    float encoded = clamp(encodedF0OrMetalId, 0.0, 1.0);
    int metalId = int(round(encoded * 255.0));
    if (metalId <= 229) {
        return vec3(encoded);
    }
    if (metalId >= 230 && metalId <= 237) {
        return LAB_PBR_DEFINED_METAL_F0[metalId - 230];
    }
    // Source texels are validated before upload. Values 238 through 254 can
    // only be produced by mip filtering across defined material regions, and
    // remain metallic with albedo-tinted F0 in the sampled domain.
    return clamp(baseColor, vec3(0.0), vec3(1.0));
}

LabPbrSpecularSample decodeLabPbrSpecular(vec4 texel, vec3 baseColor) {
    LabPbrSpecularSample decoded;
    decoded.perceptualRoughness = 1.0 - clamp(texel.r, 0.0, 1.0);
    decoded.f0 = decodeLabPbrF0(texel.g, baseColor);
    decoded.metalId = int(round(clamp(texel.g, 0.0, 1.0) * 255.0));
    decoded.metalness = decoded.metalId >= 230 ? 1.0 : 0.0;

    int surfaceValue = int(round(clamp(texel.b, 0.0, 1.0) * 255.0));
    if (surfaceValue <= 64) {
        decoded.porosity = float(surfaceValue) * (1.0 / 64.0);
        decoded.subsurface = 0.0;
    } else {
        decoded.porosity = 0.0;
        decoded.subsurface = float(surfaceValue - 65) * (1.0 / 190.0);
    }

    int emissionValue = int(round(clamp(texel.a, 0.0, 1.0) * 255.0));
    decoded.emissionProvided = emissionValue != 255;
    decoded.emission = decoded.emissionProvided
        ? float(emissionValue) * (1.0 / 254.0)
        : 0.0;
    return decoded;
}

#endif // MECRAFT_LAB_PBR_MATERIAL_GLSL
