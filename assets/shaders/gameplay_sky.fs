#version 330 core
in vec3 vWorldDir;
in vec2 vUV;
in vec4 vColor;

out vec4 FragColor;

uniform int uMode;
uniform sampler2D uTexture;
uniform vec3 uSkyTopColor;
uniform vec3 uSkyHorizonColor;
uniform vec4 uTintColor;
uniform float uBlackKeyThreshold;
uniform float uBlackKeySoftness;

void main() {
    if (uMode == 0) {
        float height = clamp(normalize(vWorldDir).y * 0.5 + 0.5, 0.0, 1.0);
        height = smoothstep(0.0, 1.0, height);
        vec3 color = mix(uSkyHorizonColor, uSkyTopColor, height);
        FragColor = vec4(color, 1.0);
        return;
    }

    if (uMode == 1) {
        vec4 texel = texture(uTexture, vUV);
        float brightness = max(max(texel.r, texel.g), texel.b);
        float keyedAlpha = smoothstep(uBlackKeyThreshold, uBlackKeyThreshold + uBlackKeySoftness, brightness);
        if (keyedAlpha <= 0.001) {
            discard;
        }
        vec3 unassociatedColor = texel.rgb / max(keyedAlpha, 0.001);
        unassociatedColor = min(unassociatedColor, vec3(1.0));
        FragColor = vec4(unassociatedColor, texel.a * keyedAlpha) * uTintColor;
        return;
    }

    if (uMode == 3) {
        FragColor = vec4(uTintColor.rgb * vColor.r, uTintColor.a);
        return;
    }

    FragColor = vColor * uTintColor;
}
