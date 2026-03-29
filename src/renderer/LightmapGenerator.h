//
// LightmapGenerator.h - GPU Lightmap Texture Generation
// Generates a 16x16 texture for block light (U axis) and sky light (V axis)
//

#ifndef MECRAFT_LIGHTMAPGENERATOR_H
#define MECRAFT_LIGHTMAPGENERATOR_H

#include <glad/glad.h>
#include <cstdint>
#include <array>

class LightmapGenerator {
public:
    // Initialize and generate the lightmap texture
    static GLuint generateLightmapTexture();

    // Update lightmap with custom colors (for day/night cycle)
    static void updateLightmap(GLuint texture, float timeOfDay);

    // Generate CPU-side lightmap data
    static std::array<uint8_t, 16 * 16 * 3> generateLightmapData(float timeOfDay = 1.0f);

private:
    // Color utilities
    static uint8_t floatToByte(float f);
    static void hsvToRgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b);

    // Block light color (warm/orange)
    static void getBlockLightColor(uint8_t level, uint8_t& r, uint8_t& g, uint8_t& b);

    // Sky light color (cool/blue-white, varies by time)
    static void getSkyLightColor(uint8_t level, float timeOfDay, uint8_t& r, uint8_t& g, uint8_t& b);
};

#endif // MECRAFT_LIGHTMAPGENERATOR_H
