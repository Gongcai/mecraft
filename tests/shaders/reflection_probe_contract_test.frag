#version 450 core

#include "../../assets/shaders/reflection_probe_contract.glsl"

layout(location = 0) out vec4 FragColor;

void main() {
    GpuReflectionProbe probe;
    probe.positionAndExposure = vec4(0.0, 1.0, 0.0, 1.0);
    probe.influenceMinAndBlendDistance = vec4(-4.0, 0.0, -4.0, 1.0);
    probe.influenceMaxAndValidity = vec4(4.0, 4.0, 4.0, 1.0);
    probe.boxProjectionMin = vec4(-5.0, -1.0, -5.0, 0.0);
    probe.boxProjectionMax = vec4(5.0, 5.0, 5.0, 0.0);
    probe.resourcesAndIdentity = uvec4(
        0u, 1u, 1u, REFLECTION_PROBE_CONTRACT_VERSION);

    vec3 surfacePosition = vec3(1.0, 0.5, 0.0);
    vec3 surfaceNormal = normalize(probe.positionAndExposure.xyz -
                                   surfacePosition);
    float weight = reflectionProbeInfluenceWeight(
        probe, surfacePosition, surfaceNormal);
    vec3 correctedDirection;
    bool projected = reflectionProbeBoxProject(
        probe, surfacePosition, normalize(vec3(1.0, 0.25, 0.5)),
        correctedDirection);
    FragColor = vec4(
        projected ? correctedDirection * 0.5 + 0.5 : vec3(0.0),
        weight);
}
