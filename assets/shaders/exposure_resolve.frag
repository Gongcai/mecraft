#version 450 core

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uExposureDataTex;
layout(binding = 1) uniform sampler2D uPreviousExposureTex;
layout(push_constant) uniform RhiPushConstants {
    vec4 pExposure;
    // Minimum, configured maximum, daylight fraction, and night adaptation maximum.
    vec4 pLimits;
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

    // Limit dark adaptation in stops so night retains a lower luminance than day.
    // Apply the current bounds even between metering updates and after time edits.
    float minimumExposure = pLimits.x;
    float nightMaximum = clamp(pLimits.w, minimumExposure, pLimits.y);
    float maximumExposure = exp2(mix(log2(nightMaximum), log2(pLimits.y), clamp(pLimits.z, 0.0, 1.0)));
    targetExposure = clamp(targetExposure, minimumExposure, maximumExposure);

    float resolvedPrevious = uInitialized ? previousState.r : uManualExposure;
    float speed = uAutoExposureSpeed * (targetExposure < resolvedPrevious ? 1.5 : 1.0);
    float alpha = 1.0 - exp(-max(uFrameTime, 0.0) * speed);
    float adaptedExposure = uInitialized
        ? mix(resolvedPrevious, targetExposure, clamp(alpha, 0.0, 1.0))
        : targetExposure;

    FragColor = vec4(clamp(adaptedExposure, minimumExposure, maximumExposure), averageLum, targetExposure, 1.0);
}
