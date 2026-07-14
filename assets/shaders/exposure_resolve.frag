#version 450 core

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uExposureDataTex;
layout(binding = 1) uniform sampler2D uPreviousExposureTex;
layout(push_constant) uniform RhiPushConstants {
    vec4 pExposure;
    ivec4 pFlags;
};

#define uFrameTime pExposure.x
#define uAutoExposureSpeed pExposure.y
#define uAutoExposureBias pExposure.z
#define uManualExposure pExposure.w
#define uInitialized (pFlags.x != 0)
#define uReusePreviousTarget (pFlags.y != 0)

void main() {
    vec4 previousState = texelFetch(uPreviousExposureTex, ivec2(0, 0), 0);
    float averageLum = previousState.g;
    float targetExposure = previousState.b;

    if (!uReusePreviousTarget) {
        vec2 exposureData = texelFetch(uExposureDataTex, ivec2(0, 0), 0).rg;
        float safeWeightSum = max(exposureData.y, 1e-4);
        float averageLogLum = exposureData.x / safeWeightSum;
        averageLum = exp(averageLogLum * 0.75);

        float K = 19.0;
        float calibration = exp2(uAutoExposureBias) * K * 1e-2;
        float a = K * 1e-2 * 18.0;
        float b = a - K * 1e-2 * 0.04;
        targetExposure = calibration / (a - b * exp(-averageLum / b));
    }

    float resolvedPrevious = uInitialized ? previousState.r : uManualExposure;
    float speed = uAutoExposureSpeed * (targetExposure < resolvedPrevious ? 1.5 : 1.0);
    float alpha = 1.0 - exp(-max(uFrameTime, 0.0) * speed);
    float adaptedExposure = uInitialized
        ? mix(resolvedPrevious, targetExposure, clamp(alpha, 0.0, 1.0))
        : targetExposure;

    FragColor = vec4(max(adaptedExposure, 0.001), averageLum, targetExposure, 1.0);
}
