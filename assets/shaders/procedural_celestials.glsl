#ifndef MECRAFT_PROCEDURAL_CELESTIALS_GLSL
#define MECRAFT_PROCEDURAL_CELESTIALS_GLSL

float celestialMoonMareBlob(vec2 p, vec2 center, float radius) {
    return 1.0 - smoothstep(radius * 0.45, radius, length(p - center));
}

void celestialBasis(vec3 axis, out vec3 right, out vec3 up) {
    vec3 reference = abs(axis.y) < 0.92 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    right = normalize(cross(reference, axis));
    up = normalize(cross(axis, right));
}

float proceduralSunAngularMask(vec3 worldDir, float innerRadius, float outerRadius) {
    vec3 sunDir = normalize(uSunDirection);
    float visibility = clamp(uSunVisibility, 0.0, 1.0) * smoothstep(-0.05, 0.08, sunDir.y);
    float angle = acos(clamp(dot(normalize(worldDir), sunDir), -1.0, 1.0));
    return (1.0 - smoothstep(innerRadius, outerRadius, angle)) * visibility;
}

vec3 renderProceduralSunDisk(vec3 worldDir) {
    vec3 sunDir = normalize(uSunDirection);
    float visibility = clamp(uSunVisibility, 0.0, 1.0) * smoothstep(-0.05, 0.08, sunDir.y);
    if (visibility <= 0.0) {
        return vec3(0.0);
    }

    const float radius = 0.034;
    float angle = acos(clamp(dot(worldDir, sunDir), -1.0, 1.0));
    float aa = max(fwidth(angle) * 1.5, 0.00045);
    float disk = 1.0 - smoothstep(radius - aa, radius + aa, angle);

    float centerToEdge = clamp(angle / radius, 0.0, 1.0);
    float limb = pow(max(1.0 - centerToEdge * centerToEdge, 0.0), 0.18);
    float core = 1.0 - smoothstep(0.0, radius * 0.72, angle);
    vec3 diskColor = mix(vec3(5.6, 2.55, 0.78), vec3(16.0, 13.0, 7.5), limb);
    diskColor *= 1.0 + core * 0.22;

    float corona = pow(max(1.0 - angle / (radius * 3.6), 0.0), 2.6) * (1.0 - disk) * 0.85;
    vec3 coronaColor = vec3(2.6, 0.95, 0.28) * corona;
    return (diskColor * disk + coronaColor) * visibility;
}

vec3 renderProceduralMoonDisk(vec3 worldDir) {
    vec3 moonDir = normalize(uMoonDirection);
    float visibility = clamp(uMoonVisibility, 0.0, 1.0) * smoothstep(-0.05, 0.08, moonDir.y);
    if (visibility <= 0.0) {
        return vec3(0.0);
    }

    const float radius = 0.027;
    float angle = acos(clamp(dot(worldDir, moonDir), -1.0, 1.0));
    float aa = max(fwidth(angle) / radius, 0.006);

    vec3 right;
    vec3 localUp;
    celestialBasis(moonDir, right, localUp);

    vec2 p = vec2(dot(worldDir, right), dot(worldDir, localUp)) / max(sin(radius), 1e-4);
    float r = length(p);
    float disk = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, r);
    if (disk <= 0.0) {
        return vec3(0.0);
    }

    vec3 normal = normalize(vec3(p, sqrt(max(1.0 - r * r, 0.0))));
    vec3 moonLightDir = normalize(vec3(sin(uMoonPhaseAngle), 0.0, cos(uMoonPhaseAngle)));
    float litRaw = dot(normal, moonLightDir);
    float litMask = smoothstep(-0.025, 0.045, litRaw);
    float lambert = pow(max(litRaw, 0.0), 0.72);

    float mare = 0.0;
    mare += celestialMoonMareBlob(p, vec2(-0.28, 0.18), 0.28) * 0.34;
    mare += celestialMoonMareBlob(p, vec2(0.18, -0.05), 0.22) * 0.26;
    mare += celestialMoonMareBlob(p, vec2(0.02, 0.32), 0.18) * 0.18;
    mare += celestialMoonMareBlob(p, vec2(-0.10, -0.30), 0.16) * 0.14;
    mare = clamp(mare, 0.0, 0.62);

    float earthshine = 0.025 + 0.025 * smoothstep(0.1, 1.0, -cos(uMoonPhaseAngle));
    float brightness = earthshine + litMask * (0.25 + 0.75 * lambert);
    float edgeRoundness = pow(max(1.0 - r * r, 0.0), 0.22);
    vec3 baseColor = mix(vec3(0.92, 0.95, 1.0), vec3(0.50, 0.56, 0.68), mare);
    vec3 color = baseColor * brightness * edgeRoundness * 1.65;
    return color * disk * visibility;
}

#endif
