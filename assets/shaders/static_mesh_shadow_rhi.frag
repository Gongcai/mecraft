#version 450 core

layout(location = 0) in vec2 vUv;
layout(binding = 0) uniform sampler2D uBaseColorTexture;
layout(std140, binding = 5) uniform StaticMeshMaterialParams {
    vec4 uBaseColorFactor;
    vec4 uEmissiveAlphaCutoff;
    vec4 uMaterialFactors;
    vec4 uWorkflowFactors;
    vec4 uSpecularFactors;
    vec4 uClearcoatFactors;
    vec4 uTransmissionVolumeFactors;
    vec4 uAttenuationColorDistance;
    ivec4 uMaterialFlags;
};

void main() {
    float alpha = texture(uBaseColorTexture, vUv).a * uBaseColorFactor.a;
    if (uMaterialFlags.x != 0 && alpha < uEmissiveAlphaCutoff.w) {
        discard;
    }
}
