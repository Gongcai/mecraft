#version 330 core
out vec4 FragColor;

in vec2 vUV;
in vec3 vWorldPos;
in vec3 vLocalPos;

uniform float breakProgress;
uniform vec3 blockWorldPos;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float segmentDistance(vec2 p, vec2 a, vec2 b) {
    vec2 ap = p - a;
    vec2 ab = b - a;
    float t = clamp(dot(ap, ab) / max(dot(ab, ab), 1e-5), 0.0, 1.0);
    return length(ap - ab * t);
}

void getFaceUV(vec3 localPos, out vec2 faceUV, out float faceId) {
    const float eps = 0.01;

    if (abs(localPos.z - 1.0) < eps) {
        // +Z: u->+X, v->+Y
        faceUV = vec2(localPos.x, localPos.y);
        faceId = 0.0;
        return;
    }
    if (abs(localPos.z) < eps) {
        // -Z: u->-X, v->+Y
        faceUV = vec2(1.0 - localPos.x, localPos.y);
        faceId = 1.0;
        return;
    }
    if (abs(localPos.x - 1.0) < eps) {
        // +X: u->-Z, v->+Y
        faceUV = vec2(1.0 - localPos.z, localPos.y);
        faceId = 2.0;
        return;
    }
    if (abs(localPos.x) < eps) {
        // -X: u->+Z, v->+Y
        faceUV = vec2(localPos.z, localPos.y);
        faceId = 3.0;
        return;
    }
    if (abs(localPos.y - 1.0) < eps) {
        // +Y: u->+X, v->-Z
        faceUV = vec2(localPos.x, 1.0 - localPos.z);
        faceId = 4.0;
        return;
    }

    // -Y: u->+X, v->+Z
    faceUV = vec2(localPos.x, localPos.z);
    faceId = 5.0;
}

const float kDamageStageThresholds[10] = float[](
    0.00, 0.07, 0.15, 0.24, 0.34,
    0.46, 0.59, 0.73, 0.86, 0.95
);

int getDamageStage(float progress) {
    int stage = 0;
    for (int i = 1; i < 10; ++i) {
        if (progress >= kDamageStageThresholds[i]) {
            stage = i;
        }
    }
    return stage;
}

void main() {
    float p = clamp(breakProgress, 0.0, 1.0);
    if (p <= 0.0) {
        discard;
    }

    // Minecraft-like 10-stage damage progression (stage 0..9) using fixed thresholds.
    int stageIndex = getDamageStage(p);
    float stageReveal = float(stageIndex + 1) / 10.0;

    vec2 faceUV;
    float faceId;
    getFaceUV(vLocalPos, faceUV, faceId);

    vec2 tileUV = clamp(faceUV, 0.0, 0.9999);
    vec2 cell = floor(tileUV * 16.0);
    vec2 cellCenter = (cell + 0.5) / 16.0;

    // A fixed crack skeleton in [0..1]^2, then quantized to 16x16 cells.
    float minDist = 1.0;
    minDist = min(minDist, segmentDistance(cellCenter, vec2(0.10, 0.14), vec2(0.86, 0.80)));
    minDist = min(minDist, segmentDistance(cellCenter, vec2(0.15, 0.82), vec2(0.78, 0.18)));
    minDist = min(minDist, segmentDistance(cellCenter, vec2(0.48, 0.05), vec2(0.52, 0.95)));
    minDist = min(minDist, segmentDistance(cellCenter, vec2(0.05, 0.50), vec2(0.95, 0.54)));
    minDist = min(minDist, segmentDistance(cellCenter, vec2(0.24, 0.26), vec2(0.08, 0.66)));
    minDist = min(minDist, segmentDistance(cellCenter, vec2(0.74, 0.74), vec2(0.92, 0.42)));

    float crackCore = 1.0 - smoothstep(0.028, 0.11, minDist);
    vec2 blockSeed = floor(blockWorldPos.xz) + vec2(faceId * 13.0, faceId * 31.0);
    float jitter = hash12(cell + blockSeed + floor(vWorldPos.xy));

    // Cells near crack lines appear first; surrounding cells fill later as progress grows.
    float revealThreshold = mix(0.72 + jitter * 0.26, 0.04 + jitter * 0.22, crackCore);
    if (stageReveal < revealThreshold) {
        discard;
    }

    float alpha = mix(0.34, 0.66, stageReveal);
    FragColor = vec4(0.0, 0.0, 0.0, alpha);
}

