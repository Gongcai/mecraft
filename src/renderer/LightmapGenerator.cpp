//
// LightmapGenerator.cpp - GPU Lightmap Texture Generation
//

#include "LightmapGenerator.h"
#include <algorithm>
#include <cmath>

GLuint LightmapGenerator::generateLightmapTexture() {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // Generate default lightmap data (daytime)
    auto data = generateLightmapData(1.0f);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());

    // Nearest neighbor filtering for sharp light level transitions
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

void LightmapGenerator::updateLightmap(GLuint texture, float timeOfDay) {
    if (texture == 0) return;

    auto data = generateLightmapData(timeOfDay);

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 16, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

std::array<uint8_t, 16 * 16 * 3> LightmapGenerator::generateLightmapData(float timeOfDay) {
    std::array<uint8_t, 16 * 16 * 3> data{};

    // timeOfDay: 0.0 = midnight, 0.5 = noon, 1.0 = midnight
    timeOfDay = std::clamp(timeOfDay, 0.0f, 1.0f);

    for (int v = 0; v < 16; ++v) {  // Sky light level (V axis)
        for (int u = 0; u < 16; ++u) {  // Block light level (U axis)
            uint8_t blockLevel = static_cast<uint8_t>(u);
            uint8_t skyLevel = static_cast<uint8_t>(v);

            uint8_t blockR, blockG, blockB;
            uint8_t skyR, skyG, skyB;

            getBlockLightColor(blockLevel, blockR, blockG, blockB);
            getSkyLightColor(skyLevel, timeOfDay, skyR, skyG, skyB);

            // Blend block light and sky light (max of both)
            // Block light is warmer (orange), sky light is cooler (blue-white)

            float blockIntensity = blockLevel / 15.0f;
            float skyIntensity = (skyLevel / 15.0f) * (0.3f + 0.7f * timeOfDay);

            // Normalize intensities
            float totalIntensity = std::max(blockIntensity, skyIntensity * 0.8f);
            if (totalIntensity < 0.05f) totalIntensity = 0.05f;  // Minimum ambient

            // Blend colors based on intensity
            float blockWeight = blockIntensity / (blockIntensity + skyIntensity + 0.001f);
            float skyWeight = 1.0f - blockWeight;

            uint8_t r = floatToByte((blockR * blockWeight + skyR * skyWeight) / 255.0f * totalIntensity);
            uint8_t g = floatToByte((blockG * blockWeight + skyG * skyWeight) / 255.0f * totalIntensity);
            uint8_t b = floatToByte((blockB * blockWeight + skyB * skyWeight) / 255.0f * totalIntensity);

            size_t idx = (v * 16 + u) * 3;
            data[idx + 0] = r;
            data[idx + 1] = g;
            data[idx + 2] = b;
        }
    }

    return data;
}

uint8_t LightmapGenerator::floatToByte(float f) {
    int val = static_cast<int>(f * 255.0f);
    return static_cast<uint8_t>(std::clamp(val, 0, 255));
}

void LightmapGenerator::hsvToRgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r_, g_, b_;
    if (h < 1.0f / 6.0f) {
        r_ = c; g_ = x; b_ = 0;
    } else if (h < 2.0f / 6.0f) {
        r_ = x; g_ = c; b_ = 0;
    } else if (h < 3.0f / 6.0f) {
        r_ = 0; g_ = c; b_ = x;
    } else if (h < 4.0f / 6.0f) {
        r_ = 0; g_ = x; b_ = c;
    } else if (h < 5.0f / 6.0f) {
        r_ = x; g_ = 0; b_ = c;
    } else {
        r_ = c; g_ = 0; b_ = x;
    }

    r = floatToByte(r_ + m);
    g = floatToByte(g_ + m);
    b = floatToByte(b_ + m);
}

void LightmapGenerator::getBlockLightColor(uint8_t level, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (level == 0) {
        r = g = b = 0;
        return;
    }

    // Warm orange/yellow for torch light
    // Level 15 = bright orange, Level 1 = dim red
    float intensity = level / 15.0f;

    // Hue: 0.08 (orange) to 0.0 (red) as it dims
    float hue = 0.08f * intensity;
    float saturation = 0.6f + 0.3f * (1.0f - intensity);  // More saturated when dim
    float value = 0.3f + 0.7f * intensity;  // Brighter at higher levels

    hsvToRgb(hue, saturation, value, r, g, b);
}

void LightmapGenerator::getSkyLightColor(uint8_t level, float timeOfDay, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (level == 0) {
        r = g = b = 0;
        return;
    }

    float intensity = level / 15.0f;

    // Time of day affects sky color
    // Noon (1.0): Bright blue-white
    // Sunset/Sunrise (0.5): Orange tint
    // Midnight (0.0): Dark blue

    float brightness = 0.2f + 0.8f * timeOfDay;
    float dayness = std::sin(timeOfDay * 3.14159f);  // Peak at noon

    if (dayness > 0.5f) {
        // Daytime: blue-white
        float t = (dayness - 0.5f) * 2.0f;  // 0 to 1
        r = floatToByte(0.9f * brightness * intensity);
        g = floatToByte(0.95f * brightness * intensity);
        b = floatToByte(1.0f * brightness * intensity);
    } else {
        // Night/twilight: cooler blue
        float t = dayness * 2.0f;  // 0 to 1
        r = floatToByte((0.3f + 0.4f * t) * brightness * intensity);
        g = floatToByte((0.4f + 0.4f * t) * brightness * intensity);
        b = floatToByte((0.6f + 0.3f * t) * brightness * intensity);
    }
}
