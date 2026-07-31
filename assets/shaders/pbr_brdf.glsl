#ifndef MECRAFT_PBR_BRDF_GLSL
#define MECRAFT_PBR_BRDF_GLSL

const float PBR_RECIPROCAL_PI = 0.31830988618379067154;

float pbrPow5(float value) {
    float squared = value * value;
    return squared * squared * value;
}

float pbrPerceptualRoughnessToAlpha(float perceptualRoughness) {
    float roughness = clamp(perceptualRoughness, 0.0, 1.0);
    return roughness * roughness;
}

float pbrPerceptualRoughnessToAlphaSquared(float perceptualRoughness) {
    float alpha = pbrPerceptualRoughnessToAlpha(perceptualRoughness);
    return alpha * alpha;
}

float pbrDielectricF0FromIor(float ior) {
    float ratio = (ior - 1.0) / (ior + 1.0);
    return ratio * ratio;
}

vec3 pbrFresnelSchlick(float cosTheta, vec3 f0, float f90) {
    float fresnel = pbrPow5(1.0 - clamp(cosTheta, 0.0, 1.0));
    return f0 + (vec3(f90) - f0) * fresnel;
}

float pbrFresnelDielectricFromIor(float cosTheta, float ior) {
    float cosRefractionSquared =
        ior * ior + cosTheta * cosTheta - 1.0;
    if (cosRefractionSquared < 0.0) {
        return 1.0;
    }

    float cosRefraction = sqrt(cosRefractionSquared);
    float iorCosTheta = ior * cosTheta;
    float iorCosRefraction = ior * cosRefraction;
    float parallel =
        (iorCosTheta - cosRefraction) /
        (iorCosTheta + cosRefraction);
    float perpendicular =
        (iorCosRefraction - cosTheta) /
        (iorCosRefraction + cosTheta);
    return clamp(0.5 *
                     (parallel * parallel +
                      perpendicular * perpendicular),
                 0.0, 1.0);
}

float pbrFresnelDielectric(float cosTheta, float f0) {
    float ior = min(sqrt(max(f0, 0.0)), 0.99999);
    ior = (1.0 + ior) / (1.0 - ior);
    float cosRefractionSquared =
        1.0 - (1.0 - cosTheta * cosTheta) / (ior * ior);
    if (cosRefractionSquared < 0.0) {
        return 1.0;
    }

    float cosRefraction = sqrt(cosRefractionSquared);
    float iorCosTheta = ior * cosTheta;
    float iorCosRefraction = ior * cosRefraction;
    float parallel =
        (iorCosTheta - cosRefraction) /
        (iorCosTheta + cosRefraction);
    float perpendicular =
        (iorCosRefraction - cosTheta) /
        (iorCosRefraction + cosTheta);
    return clamp(0.5 *
                     (parallel * parallel +
                      perpendicular * perpendicular),
                 0.0, 1.0);
}

float pbrSmithGgxV1Inverse(float cosTheta, float alphaSquared) {
    return cosTheta + sqrt(
        (cosTheta - alphaSquared * cosTheta) * cosTheta +
        alphaSquared);
}

float pbrSmithGgxCorrelatedVisibility(float nDotV,
                                      float nDotL,
                                      float alphaSquared) {
    float ggxL = nDotL * sqrt(
        alphaSquared + (nDotV - nDotV * alphaSquared) * nDotV);
    float ggxV = nDotV * sqrt(
        alphaSquared + (nDotL - nDotL * alphaSquared) * nDotL);
    return 0.5 / (ggxL + ggxV);
}

float pbrDistributionGgx(float nDotH, float alphaSquared) {
    float denominator =
        1.0 + (nDotH * alphaSquared - nDotH) * nDotH;
    return alphaSquared * PBR_RECIPROCAL_PI /
           (denominator * denominator);
}

vec3 pbrEvaluateDirectSpecular(float lDotH,
                               float nDotV,
                               float nDotL,
                               float nDotH,
                               float alphaSquared,
                               vec3 f0,
                               float f90) {
    if (nDotL < 1e-5) {
        return vec3(0.0);
    }
    vec3 fresnel = pbrFresnelSchlick(lDotH, f0, f90);
    float distribution = pbrDistributionGgx(nDotH, alphaSquared);
    float visibility = pbrSmithGgxCorrelatedVisibility(
        max(nDotV, 1e-2), max(nDotL, 1e-2), alphaSquared);
    return min(vec3(nDotL * distribution * visibility) * fresnel,
               vec3(4.0));
}

vec3 pbrLambertDiffuse(vec3 baseColor) {
    return baseColor * PBR_RECIPROCAL_PI;
}

vec3 pbrMaterialSpecularF0(vec3 dielectricF0,
                           vec3 baseColor,
                           float metalness) {
    return mix(dielectricF0, baseColor, clamp(metalness, 0.0, 1.0));
}

float pbrMaterialSpecularF90(float dielectricF90, float metalness) {
    return mix(dielectricF90, 1.0, clamp(metalness, 0.0, 1.0));
}

vec3 pbrDiffuseWeight(vec3 fresnel, float metalness) {
    return (vec3(1.0) - fresnel) *
           (1.0 - clamp(metalness, 0.0, 1.0));
}

vec3 pbrEvaluateIblSpecular(vec3 prefilteredRadiance,
                            vec2 dfg,
                            vec3 f0,
                            float f90) {
    vec3 singleScatter = f0 * dfg.x + vec3(f90 * dfg.y);
    vec3 missingEnergy = max(vec3(1.0) - singleScatter, vec3(0.0));
    vec3 averageFresnel = f0 + (vec3(f90) - f0) * (1.0 / 21.0);
    vec3 multiScatter = missingEnergy * averageFresnel /
        max(vec3(1.0) - averageFresnel * (1.0 - dfg.x), vec3(1e-4));
    return prefilteredRadiance * (singleScatter + multiScatter);
}

#endif // MECRAFT_PBR_BRDF_GLSL
