#version 450 core
out vec4 FragColor;

in vec2 vUV;
in float vLight;
in float vSunlight;
in float vBlockLight;
in float vAO;
in float vNormal;
in float vLayer;
in float vAnimationFrameCount;
in float vAnimationFps;
in float vAnimated;
in float vFogDist;
in vec3 vWorldPos;
flat in float vTintKind;
in vec2 vTintUV;

uniform sampler2DArray texArray;
uniform sampler2D uLightmapDay;
uniform sampler2D uLightmapNight;
uniform sampler2D uGrassColormap;
uniform sampler2D uFoliageColormap;
uniform sampler2D uOpaqueDepthTex;
uniform int uForceBaseLod;
uniform int uDepthSofteningEnabled;
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;
uniform int uDebugLightMode; // 0=off, 1=sky light heatmap, 2=block light heatmap, 3=combined heatmap
uniform float uSkyIntensity; // 0.0-1.0, day/night interpolation factor
uniform vec3 uSunLightColor;
uniform float uWindTime;
uniform float uAnimationTime;
uniform int uWaterEffectsEnabled;
uniform float uWaterStillFirstLayer;
uniform float uWaterStillLayerCount;
uniform float uWaterFlowFirstLayer;
uniform float uWaterFlowLayerCount;
uniform vec3 uCameraPos;

    vec3 srgbToLinear(vec3 color) {
        return pow(max(color, vec3(0.0)), vec3(2.2));
    }

    bool layerInRange(float layer, float firstLayer, float layerCount) {
        return layerCount > 0.5 && layer >= firstLayer - 0.5 && layer < firstLayer + layerCount - 0.5;
    }

    bool isWaterLayer(float layer) {
        return layerInRange(layer, uWaterStillFirstLayer, uWaterStillLayerCount) ||
               layerInRange(layer, uWaterFlowFirstLayer, uWaterFlowLayerCount);
    }

    float hash12(vec2 p) {
        vec3 p3 = fract(vec3(p.xyx) * 0.1031);
        p3 += dot(p3, p3.yzx + 33.33);
        return fract((p3.x + p3.y) * p3.z);
    }

    float valueNoise(vec2 p) {
        vec2 i = floor(p);
        vec2 f = fract(p);
        vec2 u = f * f * (3.0 - 2.0 * f);
        float a = hash12(i);
        float b = hash12(i + vec2(1.0, 0.0));
        float c = hash12(i + vec2(0.0, 1.0));
        float d = hash12(i + vec2(1.0, 1.0));
        return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
    }

    float waterNoise(vec2 p, float time) {
        float n = valueNoise(p * 0.72 + vec2(time * 0.035, -time * 0.021));
        n += valueNoise(p * 1.93 + vec2(-time * 0.052, time * 0.044)) * 0.52;
        n += valueNoise(p * 4.17 + vec2(time * 0.090, time * 0.027)) * 0.24;
        return n / 1.76;
    }

    vec3 waterEnhance(vec3 color, float alpha, float faceNormal, float depthGap) {
        float topFace = step(-0.5, faceNormal) * step(faceNormal, 0.5);
        vec2 p = vWorldPos.xz;
        float n = waterNoise(p, uAnimationTime);
        float nFine = waterNoise(p * 2.35 + vec2(17.2, -9.4), uAnimationTime * 1.37);
        float wave = (n - 0.5) * 2.0;
        float shimmer = smoothstep(0.62, 0.90, nFine) *
                        (1.0 - smoothstep(96.0, 180.0, vFogDist));

        vec3 viewDir = normalize(uCameraPos - vWorldPos);
        float facing = clamp(abs(dot(viewDir, vec3(0.0, 1.0, 0.0))), 0.0, 1.0);
        float fresnel = pow(1.0 - facing, 3.0);

        vec3 shallowTint = srgbToLinear(vec3(0.34, 0.66, 0.88));
        vec3 deepTint = srgbToLinear(vec3(0.06, 0.24, 0.42));
        float absorption = clamp(depthGap * 280.0, 0.0, 1.0);
        float distanceAbsorption = smoothstep(12.0, 84.0, vFogDist);
        vec3 waterTint = mix(shallowTint, deepTint, max(absorption, distanceAbsorption * 0.45));

        color = mix(color, color * waterTint, 0.34 + absorption * 0.26);
        color += shallowTint * (0.038 + 0.024 * wave + 0.055 * shimmer) * topFace;
        color += vec3(1.0) * fresnel * (0.066 + 0.060 * topFace + 0.032 * shimmer);
        return max(color, vec3(0.0));
    }

    // Ambient Occlusion brightness levels
    // Level 0 (fully occluded corner) = 0.72, level 3 (open) = 1.0
    // Keep the range narrow so corners are darkened but never go jet-black.
    const float aoLevels[4] = float[](0.72, 0.82, 0.91, 1.0);

    float computeFogFactor(float fogDistance) {
        if (uFogMode == 1) {
            return clamp(exp(-uFogDensity * fogDistance), 0.0, 1.0);
        }

        if (uFogMode == 2) {
            float d = uFogDensity * fogDistance;
            return clamp(exp(-(d * d)), 0.0, 1.0);
        }

        float linearRange = max(uFogEnd - uFogStart, 0.0001);
        return clamp((uFogEnd - fogDistance) / linearRange, 0.0, 1.0);
    }

    void main() {
        // Debug light visualization modes
        if (uDebugLightMode != 0) {
            float val;
            if (uDebugLightMode == 1) {
                val = vSunlight;
            } else if (uDebugLightMode == 2) {
                val = vBlockLight;
            } else {
                val = vLight;
            }
            // Heatmap: black -> blue -> cyan -> green -> yellow -> red -> white
            vec3 heatmap;
            if (val < 0.25) {
                heatmap = mix(vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), val * 4.0);
            } else if (val < 0.5) {
                heatmap = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), (val - 0.25) * 4.0);
            } else if (val < 0.75) {
                heatmap = mix(vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), (val - 0.5) * 4.0);
            } else {
                heatmap = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (val - 0.75) * 4.0);
            }
            FragColor = vec4(heatmap, 1.0);
            return;
        }

        // Cross vegetation alpha-cutout mips can darken noticeably at distance.
        bool isCrossVegetation = (vNormal > -2.5 && vNormal < -0.5);
        bool forceBaseLod = (uForceBaseLod != 0) || isCrossVegetation;
        float sampledLayer = vLayer;
        if (vAnimated > 0.5 && vAnimationFrameCount > 1.0 && vAnimationFps > 0.0) {
            float frame = mod(floor(uAnimationTime * vAnimationFps), vAnimationFrameCount);
            sampledLayer += frame;
        }

        bool waterLayer = (uWaterEffectsEnabled != 0) && isWaterLayer(sampledLayer);
        vec2 uv = vUV;
        if (waterLayer) {
            vec2 p = vWorldPos.xz;
            float n0 = waterNoise(p, uAnimationTime);
            float n1 = waterNoise(p + vec2(13.7, 5.1), uAnimationTime * 1.21);
            vec2 ripple = (vec2(n0, n1) - vec2(0.5)) * 0.040;
            float rippleDistance = 0.25 + 0.75 * (1.0 - smoothstep(18.0, 128.0, vFogDist));
            uv += ripple * rippleDistance;
        }

        vec3 sampleCoord = vec3(uv, sampledLayer);
        vec4 texColor = forceBaseLod
            ? textureLod(texArray, sampleCoord, 0.0)
            : texture(texArray, sampleCoord);

        if (texColor.a < 0.1)
            discard;

        vec3 albedo = srgbToLinear(texColor.rgb);
        if (vTintKind > 0.5 && vTintKind < 1.5) {
            albedo *= srgbToLinear(texture(uGrassColormap, vTintUV).rgb);
        } else if (vTintKind > 1.5 && vTintKind < 2.5) {
            albedo *= srgbToLinear(texture(uFoliageColormap, vTintUV).rgb);
        }

        // AO: bilinear interpolate through the discrete AO levels
        // GPU smoothly interpolates vAO between vertex values (e.g., 2.3),
        // so we must NOT discretize with int() - that destroys the gradient.
        float aoIdx = clamp(vAO, 0.0, 3.0);
        int aoLow = int(aoIdx);
        int aoHigh = min(aoLow + 1, 3);
        float aoFactor = mix(aoLevels[aoLow], aoLevels[aoHigh], fract(aoIdx));

        // Lightmap lookup:
        // vBlockLight and vSunlight are raw light levels normalized to [0,1] range (level/15).
        // The lightmap image layout:
        //   X axis (left to right) = block light 0 -> 15
        //   Y axis (top to bottom) = sky light 15 -> 0 (inverted)
        // OpenGL V=0 is the top of the image (sky=15, brightest), V=1 is bottom (sky=0, darkest).
        // So we invert vSunlight: high sky level -> low V -> top of texture -> bright.
        vec2 lightmapUV = vec2(vBlockLight, 1.0 - vSunlight);
        vec3 dayLight = srgbToLinear(texture(uLightmapDay, lightmapUV).rgb);
        vec3 nightLight = srgbToLinear(texture(uLightmapNight, lightmapUV).rgb);
        vec3 lightColor = mix(nightLight, dayLight, clamp(uSkyIntensity, 0.0, 1.0));
        float skyLightMask = clamp(vSunlight * uSkyIntensity, 0.0, 1.0);
        lightColor *= mix(vec3(1.0), uSunLightColor, skyLightMask * 0.35);

        // Combine texture, lightmap color, and AO
        vec3 finalColor = albedo * lightColor * aoFactor;

        if (uFogEnabled != 0) {
            float fogFactor = computeFogFactor(vFogDist);
            finalColor = mix(srgbToLinear(uFogColor), finalColor, fogFactor);
        }

        float alpha = texColor.a;
        float waterDepthGap = 0.0;
        if (uDepthSofteningEnabled != 0 && alpha < 0.999) {
            vec2 screenUv = gl_FragCoord.xy / vec2(textureSize(uOpaqueDepthTex, 0));
            float opaqueDepth = texture(uOpaqueDepthTex, screenUv).r;
            if (opaqueDepth < 1.0) {
                float depthGap = max(opaqueDepth - gl_FragCoord.z, 0.0);
                waterDepthGap = depthGap;
                float nearSoftening = 1.0 - smoothstep(36.0, 72.0, vFogDist);
                float contactFade = smoothstep(0.000005, 0.00035, depthGap);
                float softenedAlpha = mix(alpha, alpha * contactFade, nearSoftening * 0.45);
                alpha = max(softenedAlpha, texColor.a * 0.65);
            }
        }

        if (waterLayer) {
            finalColor = waterEnhance(finalColor, alpha, vNormal, waterDepthGap);
            alpha = clamp(alpha + 0.08, texColor.a * 0.70, 0.92);
        }

        FragColor = vec4(finalColor, alpha);
    }
