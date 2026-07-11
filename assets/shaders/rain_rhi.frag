#version 450 core

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D uPrecipTexture;
#ifdef RAIN_SCENE_DEPTH
layout(binding = 1) uniform sampler2D uSceneDepthTexture;
#endif

layout(std140, binding = 15) uniform RhiPushConstants {
    mat4 uViewProj;
    vec4 uPrecipColorStrength;
    vec4 uAlphaScreenDepth;
    ivec4 uControls;
};

void main() {
    float alpha;
    if (uControls.x != 0) {
        float verticalFade = smoothstep(0.0, 0.12, vUv.y) *
                             (1.0 - smoothstep(0.88, 1.0, vUv.y));
        float lineFade = 1.0 - smoothstep(0.18, 0.50, abs(vUv.x - 0.5));
        alpha = verticalFade * lineFade * uAlphaScreenDepth.x * 0.28;
    } else {
        alpha = texture(uPrecipTexture, vUv).a * uAlphaScreenDepth.x;
    }
    if (alpha < 0.01) {
        discard;
    }

#ifdef RAIN_SCENE_DEPTH
    {
        ivec2 depthSize = textureSize(uSceneDepthTexture, 0);
        vec2 screenUv = gl_FragCoord.xy / max(uAlphaScreenDepth.yz, vec2(1.0));
        ivec2 depthTexel = ivec2(clamp(screenUv, vec2(0.0), vec2(0.999999)) * vec2(depthSize));
        float sceneDepth = texelFetch(uSceneDepthTexture, depthTexel, 0).r;
        if (sceneDepth < 0.999999) {
            alpha *= smoothstep(0.00004, 0.0012, sceneDepth - gl_FragCoord.z);
        }
    }
#endif
    if (alpha < 0.01) {
        discard;
    }

    fragColor = vec4(uPrecipColorStrength.rgb, alpha * uPrecipColorStrength.a);
}
