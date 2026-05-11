#version 450 core

in vec2 vTexCoord;
out vec2 FragVelocity;

uniform sampler2D uDepthTex;
uniform mat4 uInvViewProj;
uniform mat4 uPreviousViewProj;
uniform vec2 uJitter;
uniform vec2 uPreviousJitter;

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    if (depth >= 0.9999) {
        FragVelocity = vec2(0.0);
        return;
    }

    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
    vec4 previousClip = uPreviousViewProj * vec4(worldPos, 1.0);
    vec2 previousUv = previousClip.xy / max(previousClip.w, 0.00001) * 0.5 + 0.5;
    // The main gbuffer projection is currently not jittered. Including the Halton
    // offsets here creates artificial full-screen motion and makes TAA drag history.
    FragVelocity = vTexCoord - previousUv;
}
