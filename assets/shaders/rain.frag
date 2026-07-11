#version 450 core
out vec4 FragColor;

in vec2 vUV;

uniform sampler2D uPrecipTex;
uniform sampler2D uSceneDepthTex;
uniform float uPrecipStrength;
uniform float uPrecipAlphaScale;
uniform vec2 uScreenSize;
uniform vec3 uPrecipColor;
uniform int uProceduralLines;
uniform int uDepthFadeEnabled;

void main() {
    float texAlpha = 0.0;
    if (uProceduralLines != 0) {
        float verticalFade = smoothstep(0.00, 0.12, vUV.y) * (1.0 - smoothstep(0.88, 1.00, vUV.y));
        float lineFade = 1.0 - smoothstep(0.18, 0.50, abs(vUV.x - 0.5));
        texAlpha = verticalFade * lineFade * uPrecipAlphaScale * 0.28;
    } else {
        texAlpha = texture(uPrecipTex, vUV).a * uPrecipAlphaScale;
    }
    if (texAlpha < 0.01) {
        discard;
    }
    if (uDepthFadeEnabled != 0) {
        ivec2 depthSize = textureSize(uSceneDepthTex, 0);
        if (depthSize.x > 0 && depthSize.y > 0) {
            vec2 screenUv = gl_FragCoord.xy / max(uScreenSize, vec2(1.0));
            ivec2 depthTexel = ivec2(clamp(screenUv, vec2(0.0), vec2(0.999999)) * vec2(depthSize));
            float sceneDepth = texelFetch(uSceneDepthTex, depthTexel, 0).r;
            if (sceneDepth < 0.999999) {
                float depthGap = sceneDepth - gl_FragCoord.z;
                texAlpha *= smoothstep(0.00004, 0.0012, depthGap);
            }
        }
    }
    if (texAlpha < 0.01) {
        discard;
    }
    FragColor = vec4(uPrecipColor, texAlpha * uPrecipStrength);
}
