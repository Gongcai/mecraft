#include "GameplaySkyRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <vector>

#include "Shader.h"
#include "../core/Camera.h"
#include "../Paths.h"
#include "../resource/ResourceMgr.h"
#include "stb/stb_image.h"
#include "../world/DayNightSystem.h"

#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr float kSunSize = 1.15f;
constexpr float kMoonSize = 1.05f;
constexpr float kHaloSize = 3.25f;
constexpr float kBlackKeyThreshold = 0.035f;
constexpr float kBlackKeySoftness = 0.22f;
constexpr float kCloudHeight = 128.0f;
constexpr int kCloudMaskSample = 8;
constexpr float kCloudCellSize = 6.0f;
constexpr float kCloudThickness = 4.0f;
constexpr float kCloudDriftSpeed = 0.35f;
constexpr unsigned char kCloudAlphaThreshold = 120;
constexpr int kCloudSolidNumerator = 3;
constexpr int kCloudSolidDenominator = 5;

float smoothstep(float edge0, float edge1, float x) {
    const float denom = std::max(edge1 - edge0, 0.0001f);
    const float t = std::clamp((x - edge0) / denom, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

glm::vec3 lerp(const glm::vec3& a, const glm::vec3& b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    if (len <= 0.0001f) {
        return fallback;
    }
    return v / len;
}

struct BodyVertex {
    glm::vec3 position;
    glm::vec2 uv;
};

struct HaloVertex {
    glm::vec3 position;
    glm::vec2 uv;
    glm::vec4 color;
};

struct CloudVertex {
    glm::vec3 position;
    float shade;
};

bool isCloudPixelSolid(const std::vector<unsigned char>& pixels, const int width, const int height, const int x, const int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    const size_t idx = static_cast<size_t>(y * width + x) * 4;
    return idx + 3 < pixels.size() && pixels[idx + 3] > kCloudAlphaThreshold;
}

bool isMaskSolid(const std::vector<uint8_t>& mask, const int width, const int height, const int x, const int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    return mask[static_cast<size_t>(y * width + x)] != 0;
}

bool isExteriorEmpty(const std::vector<uint8_t>& exteriorEmpty, const int width, const int height, const int x, const int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return true;
    }
    return exteriorEmpty[static_cast<size_t>(y * width + x)] != 0;
}

void appendCloudQuad(std::vector<CloudVertex>& vertices,
                     const glm::vec3& a,
                     const glm::vec3& b,
                     const glm::vec3& c,
                     const glm::vec3& d,
                     const float shade) {
    vertices.push_back({a, shade});
    vertices.push_back({b, shade});
    vertices.push_back({c, shade});
    vertices.push_back({a, shade});
    vertices.push_back({c, shade});
    vertices.push_back({d, shade});
}

void appendCloudBoxFaceRect(std::vector<CloudVertex>& vertices,
                            const int x,
                            const int y,
                            const int width,
                            const int height,
                            const float originX,
                            const float originZ,
                            const float cellWorldSize,
                            const float y0,
                            const float y1,
                            const int face,
                            const float shade) {
    const float x0 = originX + static_cast<float>(x) * cellWorldSize;
    const float x1 = originX + static_cast<float>(x + width) * cellWorldSize;
    const float z0 = originZ + static_cast<float>(y) * cellWorldSize;
    const float z1 = originZ + static_cast<float>(y + height) * cellWorldSize;

    switch (face) {
        case 0:
            appendCloudQuad(vertices, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, shade);
            break;
        case 1:
            appendCloudQuad(vertices, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, shade);
            break;
        case 2:
            appendCloudQuad(vertices, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {x0, y0, z0}, shade);
            break;
        case 3:
            appendCloudQuad(vertices, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, shade);
            break;
        case 4:
            appendCloudQuad(vertices, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}, shade);
            break;
        case 5:
            appendCloudQuad(vertices, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {x0, y0, z1}, shade);
            break;
        default:
            break;
    }
}

void appendGreedySurface(std::vector<CloudVertex>& vertices,
                         const std::vector<uint8_t>& surface,
                         const int width,
                         const int height,
                         const float originX,
                         const float originZ,
                         const float cellWorldSize,
                         const float y0,
                         const float y1,
                         const int face,
                         const float shade) {
    std::vector<uint8_t> used(surface.size(), 0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y * width + x);
            if (surface[idx] == 0 || used[idx] != 0) {
                continue;
            }

            int rectWidth = 1;
            while (x + rectWidth < width) {
                const size_t nextIdx = static_cast<size_t>(y * width + x + rectWidth);
                if (surface[nextIdx] == 0 || used[nextIdx] != 0) {
                    break;
                }
                ++rectWidth;
            }

            int rectHeight = 1;
            bool canExtend = true;
            while (y + rectHeight < height && canExtend) {
                for (int dx = 0; dx < rectWidth; ++dx) {
                    const size_t nextIdx = static_cast<size_t>((y + rectHeight) * width + x + dx);
                    if (surface[nextIdx] == 0 || used[nextIdx] != 0) {
                        canExtend = false;
                        break;
                    }
                }
                if (canExtend) {
                    ++rectHeight;
                }
            }

            for (int dy = 0; dy < rectHeight; ++dy) {
                for (int dx = 0; dx < rectWidth; ++dx) {
                    used[static_cast<size_t>((y + dy) * width + x + dx)] = 1;
                }
            }

            appendCloudBoxFaceRect(vertices, x, y, rectWidth, rectHeight,
                                   originX, originZ, cellWorldSize, y0, y1, face, shade);
        }
    }
}
}

void GameplaySkyRenderer::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("gameplay_sky");
    m_sunTexture = resourceMgr.getGuiTexture("sun");
    m_moonTexture = resourceMgr.getGuiTexture("moon_phases");
    initMeshes();
    initCloudMesh();
}

void GameplaySkyRenderer::shutdown() {
    destroyMeshes();
    m_shader = nullptr;
    m_sunTexture = 0;
    m_moonTexture = 0;
}

void GameplaySkyRenderer::render(const Camera& camera, const float aspect, const DayNightSystem& dayNight, GLuint skyCaptureTexture) {
    m_lastColors = computeSkyColors(dayNight);
    if (m_shader == nullptr || m_skyVao == 0) {
        return;
    }

    GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullFaceWasEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthMaskWasEnabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWasEnabled);

    GLint blendSrcRgb = GL_ONE;
    GLint blendDstRgb = GL_ZERO;
    GLint blendSrcAlpha = GL_ONE;
    GLint blendDstAlpha = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    renderSkyGradient(camera, aspect, m_lastColors, skyCaptureTexture);
    renderHalo(camera, aspect, dayNight, m_lastColors);

    const float sunAngle = dayNight.getCelestialAngleRadians();
    const float moonAngle = std::fmod(sunAngle + kPi, kTwoPi);
    renderCelestialBody(camera, aspect, sunAngle, kSunSize, m_sunTexture, glm::vec2(0.0f), glm::vec2(1.0f), m_lastColors.sunVisibility);

    const MoonPhaseUv moonUv = getMoonPhaseUvInternal(dayNight.getMoonPhaseIndex());
    const float moonPhaseFactor = static_cast<float>(std::abs(dayNight.getMoonPhaseIndex() - 4)) * 0.25f + 0.2f;
    const float moonDiscAlpha = 0.18f * moonPhaseFactor * m_lastColors.moonVisibility;
    renderCelestialBody(camera, aspect, moonAngle, kMoonSize, m_moonTexture, moonUv.uvMin, moonUv.uvMax, moonDiscAlpha);
    renderClouds(camera, aspect, dayNight, m_lastColors);

    if (blendWasEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    glBlendFuncSeparate(blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha);
    if (cullFaceWasEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    if (depthTestWasEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(depthMaskWasEnabled);
}

void GameplaySkyRenderer::renderSkyCapture(const DayNightSystem& dayNight,
                                           const GLuint framebuffer,
                                           const int width,
                                           const int height,
                                           const float cameraAltitude,
                                           const GLuint atmosphereLutTexture,
                                           const float moonPhaseFlux) {
    m_lastColors = computeSkyColors(dayNight);
    if (m_shader == nullptr || m_skyVao == 0 || framebuffer == 0 || width <= 0 || height <= 0) {
        return;
    }

    GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullFaceWasEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    // Raw sky: rows 0..skyCaptureRes.y+1 (258 rows). Matches DerivativeMain Deferred0.glsl.
    glViewport(0, 0, width, std::min(height, 258));
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    m_shader->use();
    m_shader->setInt("uMode", 4);
    m_shader->setMat4("uView", glm::mat4(1.0f));
    m_shader->setMat4("uProjection", glm::mat4(1.0f));
    m_shader->setMat4("uModel", glm::mat4(1.0f));
    m_shader->setVec3("uSkyTopColor", m_lastColors.top);
    m_shader->setVec3("uSkyHorizonColor", m_lastColors.horizon);
    m_shader->setVec3("uSunDirection", m_lastColors.sunDirection);
    m_shader->setVec3("uMoonDirection", m_lastColors.moonDirection);
    m_shader->setVec3("uSunScatterColor", m_lastColors.sunScatter);
    m_shader->setVec3("uMoonLightColor", m_lastColors.moonLightColor);
    m_shader->setFloat("uHorizonHaze", m_lastColors.horizonHaze);
    m_shader->setFloat("uSunGlare", m_lastColors.sunGlare);
    m_shader->setFloat("uSunVisibility", m_lastColors.sunVisibility);
    m_shader->setFloat("uMoonVisibility", m_lastColors.moonVisibility);
    m_shader->setFloat("uNightFactor", m_lastColors.nightFactor);
    m_shader->setInt("uIncludeCelestialDisks", 0);
    m_shader->setVec4("uTintColor", glm::vec4(1.0f));
    m_shader->setVec2("uUvMin", glm::vec2(0.0f));
    m_shader->setVec2("uUvMax", glm::vec2(1.0f));
    m_shader->setFloat("uCameraAltitude", cameraAltitude);
    m_shader->setFloat("uMoonPhaseFlux", moonPhaseFlux);
    if (atmosphereLutTexture != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, atmosphereLutTexture);
        m_shader->setInt("uAtmosphereLut", 1);
        glActiveTexture(GL_TEXTURE0);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(m_skyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (blendWasEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (cullFaceWasEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    if (depthTestWasEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void GameplaySkyRenderer::renderCloudySkyCapture(const DayNightSystem& dayNight,
                                                  const GLuint framebuffer,
                                                  const int skyCaptureWidth,
                                                  const int skyCaptureHeight,
                                                  const float cameraAltitude,
                                                  const GLuint atmosphereLutTexture,
                                                  const float moonPhaseFlux) {
    (void)dayNight; // Uses m_lastColors from preceding renderSkyCapture() call.
    if (m_shader == nullptr || m_skyVao == 0 || framebuffer == 0 || skyCaptureWidth <= 0 || skyCaptureHeight <= 258) {
        return;
    }

    GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullFaceWasEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    // Cloudy sky: rows 258..513 (256 rows). Matches DerivativeMain Deferred0.glsl cloudy sky region.
    glViewport(0, 258, skyCaptureWidth, 256);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    m_shader->use();
    m_shader->setInt("uMode", 4);
    m_shader->setMat4("uView", glm::mat4(1.0f));
    m_shader->setMat4("uProjection", glm::mat4(1.0f));
    m_shader->setMat4("uModel", glm::mat4(1.0f));
    m_shader->setVec3("uSkyTopColor", m_lastColors.top);
    m_shader->setVec3("uSkyHorizonColor", m_lastColors.horizon);
    m_shader->setVec3("uSunDirection", m_lastColors.sunDirection);
    m_shader->setVec3("uMoonDirection", m_lastColors.moonDirection);
    m_shader->setVec3("uSunScatterColor", m_lastColors.sunScatter);
    m_shader->setVec3("uMoonLightColor", m_lastColors.moonLightColor);
    m_shader->setFloat("uHorizonHaze", m_lastColors.horizonHaze);
    m_shader->setFloat("uSunGlare", m_lastColors.sunGlare);
    m_shader->setFloat("uSunVisibility", m_lastColors.sunVisibility);
    m_shader->setFloat("uMoonVisibility", m_lastColors.moonVisibility);
    m_shader->setFloat("uNightFactor", m_lastColors.nightFactor);
    m_shader->setInt("uIncludeCelestialDisks", 1);
    m_shader->setVec4("uTintColor", glm::vec4(1.0f));
    m_shader->setVec2("uUvMin", glm::vec2(0.0f));
    m_shader->setVec2("uUvMax", glm::vec2(1.0f));
    m_shader->setFloat("uCameraAltitude", cameraAltitude);
    m_shader->setFloat("uMoonPhaseFlux", moonPhaseFlux);
    if (atmosphereLutTexture != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, atmosphereLutTexture);
        m_shader->setInt("uAtmosphereLut", 1);
        glActiveTexture(GL_TEXTURE0);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(m_skyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (blendWasEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (cullFaceWasEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    if (depthTestWasEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void GameplaySkyRenderer::writeSkyCacheMetadata(const SkyIlluminanceData& illuminance,
                                                 GLuint framebuffer,
                                                 int skyCaptureWidth,
                                                 float cameraAltitude,
                                                 GLuint atmosphereLutTexture,
                                                 float moonPhaseFlux) {
    if (m_shader == nullptr || m_skyVao == 0 || framebuffer == 0 || skyCaptureWidth <= 0) {
        return;
    }

    GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullFaceWasEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    // Write to column skyCaptureWidth-1, rows 0..5 (6 pixels).
    // Rows 0-3: illuminance, row 5: cloudDynamicWeather.
    glViewport(skyCaptureWidth - 1, 0, 1, 6);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    m_shader->use();
    m_shader->setInt("uMode", 5);
    m_shader->setMat4("uView", glm::mat4(1.0f));
    m_shader->setMat4("uProjection", glm::mat4(1.0f));
    m_shader->setMat4("uModel", glm::mat4(1.0f));
    m_shader->setVec3("uDirectIlluminance", illuminance.directIlluminance);
    m_shader->setVec3("uSkyIlluminance", illuminance.skyIlluminance);
    m_shader->setVec3("uSunIlluminance", illuminance.sunIlluminance);
    m_shader->setVec3("uMoonIlluminance", illuminance.moonIlluminance);
    m_shader->setVec3("uCloudDynamicWeather", illuminance.cloudDynamicWeather);
    m_shader->setVec3("uSunDirection", m_lastColors.sunDirection);
    m_shader->setVec3("uMoonDirection", m_lastColors.moonDirection);
    m_shader->setFloat("uCameraAltitude", cameraAltitude);
    m_shader->setFloat("uMoonPhaseFlux", moonPhaseFlux);
    if (atmosphereLutTexture != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, atmosphereLutTexture);
        m_shader->setInt("uAtmosphereLut", 1);
        glActiveTexture(GL_TEXTURE0);
    }

    glBindVertexArray(m_skyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (blendWasEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (cullFaceWasEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    if (depthTestWasEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

GameplaySkyRenderer::SkyColors GameplaySkyRenderer::computeSkyColors(const DayNightSystem& dayNight) const {
    const float skyIntensity = dayNight.getSkyIntensity();
    const float progress = dayNight.getDayProgress01();
    const glm::vec3 sunDirection = directionFromAngle(dayNight.getCelestialAngleRadians());
    const glm::vec3 moonDirection = directionFromAngle(std::fmod(dayNight.getCelestialAngleRadians() + kPi, kTwoPi));
    const float sunHeight = std::clamp(sunDirection.y, -0.25f, 1.0f);
    const float sunriseWindow = 1.0f - std::abs(progress - 0.0f) / 0.07f;
    const float sunriseWrapWindow = 1.0f - std::abs(progress - 1.0f) / 0.07f;
    const float sunsetWindow = 1.0f - std::abs(progress - 0.5f) / 0.08f;
    const float warmWindow = std::clamp(std::max(std::max(sunriseWindow, sunriseWrapWindow), sunsetWindow), 0.0f, 1.0f);
    const float warm = warmWindow * warmWindow * (3.0f - 2.0f * warmWindow);

    const glm::vec3 dayTop(0.38f, 0.66f, 1.0f);
    const glm::vec3 dayHorizon(0.70f, 0.88f, 1.0f);
    const glm::vec3 nightTop(0.010f, 0.020f, 0.060f);
    const glm::vec3 nightHorizon(0.030f, 0.045f, 0.095f);
    const glm::vec3 duskTop(0.24f, 0.34f, 0.58f);
    const glm::vec3 duskHorizon(1.0f, 0.38f, 0.13f);
    const glm::vec3 goldenScatter(1.0f, 0.52f, 0.16f);
    const glm::vec3 noonScatter(0.62f, 0.80f, 1.0f);
    const glm::vec3 noonSunLight(1.0f, 0.98f, 0.92f);
    const glm::vec3 warmSunLight(1.0f, 0.58f, 0.28f);
    const glm::vec3 nightSunLight(0.36f, 0.44f, 0.72f);
    const glm::vec3 dayAmbient(0.68f, 0.82f, 1.0f);
    const glm::vec3 nightAmbient(0.12f, 0.16f, 0.28f);
    const glm::vec3 warmAmbient(0.92f, 0.54f, 0.30f);
    const glm::vec3 dayShadowTint(0.56f, 0.64f, 0.88f);
    const glm::vec3 nightShadowTint(0.10f, 0.13f, 0.24f);
    const glm::vec3 warmHorizonScatter(1.0f, 0.36f, 0.13f);

    SkyColors colors;
    colors.top = lerp(nightTop, dayTop, skyIntensity);
    colors.horizon = lerp(nightHorizon, dayHorizon, skyIntensity);
    colors.top = lerp(colors.top, duskTop, warm * 0.65f);
    colors.horizon = lerp(colors.horizon, duskHorizon, warm);
    colors.fog = lerp(colors.horizon, glm::vec3(0.80f, 0.90f, 1.0f), skyIntensity * (1.0f - warm) * 0.22f);
    colors.sunDirection = sunDirection;
    colors.moonDirection = moonDirection;
    colors.sunScatter = lerp(noonScatter, goldenScatter, warm);
    colors.sunLightColor = lerp(nightSunLight, noonSunLight, skyIntensity);
    colors.sunLightColor = lerp(colors.sunLightColor, warmSunLight, warm * 0.58f);
    colors.moonLightColor = glm::vec3(0.42f, 0.52f, 0.95f);
    colors.skyAmbientColor = lerp(nightAmbient, dayAmbient, skyIntensity);
    colors.skyAmbientColor = lerp(colors.skyAmbientColor, warmAmbient, warm * 0.32f);
    colors.shadowTintColor = lerp(nightShadowTint, dayShadowTint, skyIntensity);
    colors.shadowTintColor = lerp(colors.shadowTintColor, glm::vec3(0.34f, 0.28f, 0.44f), warm * 0.35f);
    colors.horizonScatterColor = lerp(colors.horizon, warmHorizonScatter, warm * 0.85f);
    colors.horizonHaze = std::clamp(0.28f + 0.44f * warm + 0.16f * (1.0f - skyIntensity), 0.0f, 0.85f);
    colors.sunGlare = std::clamp(0.18f * skyIntensity + 0.78f * warm, 0.0f, 1.0f);
    colors.haloStrength = std::clamp(warm + smoothstep(0.05f, 0.35f, sunHeight) * skyIntensity * 0.28f, 0.0f, 1.0f);
    colors.halo = glm::vec4(colors.sunScatter, 0.30f * skyIntensity + 0.68f * warm);
    colors.sunVisibility = smoothstep(-0.08f, 0.18f, sunDirection.y) * skyIntensity;
    colors.moonVisibility = smoothstep(-0.08f, 0.18f, moonDirection.y) * (1.0f - skyIntensity);
    colors.dayFactor = skyIntensity;
    colors.nightFactor = 1.0f - skyIntensity;
    colors.horizonFactor = std::clamp(1.0f - std::abs(sunDirection.y), 0.0f, 1.0f);
    colors.rainFactor = 0.0f;
    colors.wetnessFactor = 0.0f;
    colors.cloudinessFactor = m_cloudMeshInfo.valid ? 0.35f : 0.0f;

    const glm::vec3 cloudDayColor = lerp(colors.horizon, glm::vec3(1.0f), 0.84f);
    const glm::vec3 cloudNightColor(0.14f, 0.16f, 0.25f);
    const glm::vec3 cloudWarmColor(1.0f, 0.58f, 0.24f);
    colors.cloudColor = lerp(cloudNightColor, cloudDayColor, skyIntensity);
    colors.cloudColor = lerp(colors.cloudColor, cloudWarmColor, warm * 0.42f);
    return colors;
}

GameplaySkyRenderer::SkyIlluminanceData GameplaySkyRenderer::computeSkyIlluminance(const SkyColors& colors) const {
    SkyIlluminanceData data;

    // Match DerivativeMain's atmosphere-unit contract:
    //   sunIlluminance/moonIlluminance are solar_irradiance * transmittance,
    //   directIlluminance is their sum, and skyIlluminance is a low HDR
    //   hemisphere term. Do not use real-world lux here; cloud/fog/water shaders
    //   multiply these values by large shaderpack constants.
    constexpr glm::vec3 kSolarIrradiance(1.474000f, 1.850400f, 1.911980f);
    const float sunAltitude = std::clamp(colors.sunDirection.y, 0.0f, 1.0f);
    const float moonAltitude = std::clamp(colors.moonDirection.y, 0.0f, 1.0f);
    const float sunTransmittance = colors.sunVisibility * smoothstep(0.0f, 0.18f, sunAltitude);
    // DerivativeMain MoonFlux includes NIGHT_BRIGHTNESS (0.0005) which scales moon
    // contribution to physically correct levels. Without this, moon is ~2000x too bright.
    // The GPU metadata path (mode 5) uses atmGetSunAndSkyIrradiance with uMoonPhaseFlux
    // which already includes NIGHT_BRIGHTNESS. This CPU fallback should match.
    constexpr float kNightBrightness = 0.0005f;
    const float moonTransmittance = colors.moonVisibility * smoothstep(0.0f, 0.18f, moonAltitude) * kNightBrightness;

    data.sunIlluminance = kSolarIrradiance * colors.sunLightColor * sunTransmittance;
    data.moonIlluminance = kSolarIrradiance * colors.moonLightColor * moonTransmittance;
    data.directIlluminance = data.sunIlluminance + data.moonIlluminance;

    const float skyVisibility = std::clamp(colors.dayFactor + colors.moonVisibility * 0.18f, 0.0f, 1.0f);
    data.skyIlluminance = colors.skyAmbientColor * (0.10f + 0.42f * skyVisibility);

    return data;
}

glm::vec3 GameplaySkyRenderer::computeCloudDynamicWeather(const int worldDay, const int worldTime) {
    // Replicates DerivativeMain deferred.vsh cloudDynamicWeather computation.
    // Uses triple32 hash + per-day random weather maps interpolated over the day cycle.
    auto triple32 = [](uint32_t x) -> uint32_t {
        x ^= x >> 17; x *= 0xed5ad4bbu;
        x ^= x >> 11; x *= 0xac4c1b51u;
        x ^= x >> 15; x *= 0x31848babu;
        x ^= x >> 14;
        return x;
    };
    auto hash1 = [](float p) -> float {
        p = std::fmod(p, 1.0f);
        if (p < 0.0f) p += 1.0f;
        p = p * p * (3.0f - 2.0f * p); // fract approximation not needed, just use p
        // Exact GLSL: p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p);
        float v = p * 0.1031f;
        v = v - std::floor(v);
        v *= v + 33.33f;
        v *= v + v;
        return v - std::floor(v);
    };
    auto randWeather = [&](int state) -> glm::vec2 {
        float h = hash1(static_cast<float>(triple32(static_cast<uint32_t>(state))) / static_cast<float>(0xFFFFFFFFu));
        return glm::vec2(h, hash1(h + 0.1f)); // second component from offset hash
    };
    auto curve = [](float x) -> float {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    };
    auto remap = [](float lo, float hi, float x) -> float {
        return std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
    };

    // Interpolation factor: fract(worldTime / 24000.0 + vec2(0.65, 0.25))
    const float dayFrac = static_cast<float>(worldTime) / 24000.0f;
    const float tX = dayFrac + 0.65f;
    const float tY = dayFrac + 0.25f;
    const float curveTX = curve(tX - std::floor(tX));
    const float curveTY = curve(tY - std::floor(tY));

    const glm::vec2 w0 = randWeather(worldDay);
    const glm::vec2 w1 = randWeather(worldDay + 1);

    glm::vec2 weatherMap;
    weatherMap.x = w0.x + (w1.x - w0.x) * curveTX;
    weatherMap.y = w0.y + (w1.y - w0.y) * curveTY;

    glm::vec3 result;
    result.x = curve(remap(0.25f, 0.4f, weatherMap.x)) * 0.5f;           // cirrocumulus
    result.y = (1.0f - remap(0.65f, 0.8f, weatherMap.y));
    result.y = result.y * result.y * 0.5f;                                 // cirrus
    result.z = remap(0.4f, 0.55f, weatherMap.x * 2.0f - weatherMap.y);    // storm
    result.z *= 2.0f - result.z;

    return result;
}

glm::vec3 GameplaySkyRenderer::getLastFogColor() const {
    return m_lastColors.fog;
}

std::pair<glm::vec2, glm::vec2> GameplaySkyRenderer::getMoonPhaseUv(const int phaseIndex) {
    const int clamped = std::clamp(phaseIndex, 0, 7);
    const int col = clamped % 4;
    const int row = clamped / 4;
    const glm::vec2 uvMin(static_cast<float>(col) * 0.25f, static_cast<float>(row) * 0.5f);
    return {uvMin, uvMin + glm::vec2(0.25f, 0.5f)};
}

void GameplaySkyRenderer::initMeshes() {
    if (m_skyVao == 0) {
        constexpr std::array<float, 18> skyVertices = {
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
             3.0f, -1.0f, 0.0f, 2.0f, 0.0f, 1.0f,
            -1.0f,  3.0f, 0.0f, 0.0f, 2.0f, 1.0f,
        };

        glGenVertexArrays(1, &m_skyVao);
        glGenBuffers(1, &m_skyVbo);
        glBindVertexArray(m_skyVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_skyVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(skyVertices.size() * sizeof(float)),
                     skyVertices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
        glBindVertexArray(0);
    }

    if (m_bodyVao == 0) {
        constexpr std::array<BodyVertex, 6> bodyVertices = {{
            {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
            {{ 1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
            {{ 1.0f,  1.0f, 0.0f}, {1.0f, 1.0f}},
            {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
            {{ 1.0f,  1.0f, 0.0f}, {1.0f, 1.0f}},
            {{-1.0f,  1.0f, 0.0f}, {0.0f, 1.0f}},
        }};

        glGenVertexArrays(1, &m_bodyVao);
        glGenBuffers(1, &m_bodyVbo);
        glBindVertexArray(m_bodyVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_bodyVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bodyVertices.size() * sizeof(BodyVertex)),
                     bodyVertices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BodyVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BodyVertex), reinterpret_cast<void*>(sizeof(glm::vec3)));
        glBindVertexArray(0);
        m_bodyVertexCount = static_cast<GLsizei>(bodyVertices.size());
    }

    if (m_haloVao == 0) {
        constexpr int kSegments = 48;
        std::vector<HaloVertex> haloVertices;
        haloVertices.reserve(static_cast<size_t>(kSegments) * 3);

        const glm::vec4 centerColor(1.0f, 0.42f, 0.06f, 1.0f);
        const glm::vec4 edgeColor(1.0f, 0.42f, 0.06f, 0.0f);
        for (int i = 0; i < kSegments; ++i) {
            const float a0 = static_cast<float>(i) / static_cast<float>(kSegments) * kTwoPi;
            const float a1 = static_cast<float>(i + 1) / static_cast<float>(kSegments) * kTwoPi;
            haloVertices.push_back({glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(0.5f), centerColor});
            haloVertices.push_back({glm::vec3(std::cos(a0), std::sin(a0), 0.0f), glm::vec2(0.0f), edgeColor});
            haloVertices.push_back({glm::vec3(std::cos(a1), std::sin(a1), 0.0f), glm::vec2(0.0f), edgeColor});
        }

        glGenVertexArrays(1, &m_haloVao);
        glGenBuffers(1, &m_haloVbo);
        glBindVertexArray(m_haloVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_haloVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(haloVertices.size() * sizeof(HaloVertex)),
                     haloVertices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(HaloVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(HaloVertex), reinterpret_cast<void*>(sizeof(glm::vec3)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(HaloVertex), reinterpret_cast<void*>(sizeof(glm::vec3) + sizeof(glm::vec2)));
        glBindVertexArray(0);
        m_haloVertexCount = static_cast<GLsizei>(haloVertices.size());
    }

}

void GameplaySkyRenderer::destroyMeshes() {
    auto deleteBuffer = [](GLuint& vao, GLuint& vbo) {
        if (vbo != 0) {
            glDeleteBuffers(1, &vbo);
            vbo = 0;
        }
        if (vao != 0) {
            glDeleteVertexArrays(1, &vao);
            vao = 0;
        }
    };

    deleteBuffer(m_skyVao, m_skyVbo);
    deleteBuffer(m_haloVao, m_haloVbo);
    deleteBuffer(m_bodyVao, m_bodyVbo);
    deleteBuffer(m_cloudVao, m_cloudVbo);
    m_haloVertexCount = 0;
    m_bodyVertexCount = 0;
    m_cloudVertexCount = 0;
    m_cloudMeshInfo = {};
}

void GameplaySkyRenderer::initCloudMesh() {
    if (m_cloudVao != 0) {
        return;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* rawPixels = stbi_load(CLOUD_TEXTURE_PATH, &width, &height, &channels, 4);
    if (rawPixels == nullptr || width <= 0 || height <= 0) {
        if (rawPixels != nullptr) {
            stbi_image_free(rawPixels);
        }
        return;
    }

    std::vector<unsigned char> pixels(rawPixels, rawPixels + static_cast<size_t>(width * height * 4));
    stbi_image_free(rawPixels);

    const int maskWidth = std::max(1, (width + kCloudMaskSample - 1) / kCloudMaskSample);
    const int maskHeight = std::max(1, (height + kCloudMaskSample - 1) / kCloudMaskSample);
    std::vector<uint8_t> mask(static_cast<size_t>(maskWidth * maskHeight), 0);

    for (int my = 0; my < maskHeight; ++my) {
        for (int mx = 0; mx < maskWidth; ++mx) {
            int solidCount = 0;
            int sampleCount = 0;
            for (int sy = 0; sy < kCloudMaskSample; ++sy) {
                for (int sx = 0; sx < kCloudMaskSample; ++sx) {
                    const int px = mx * kCloudMaskSample + sx;
                    const int py = my * kCloudMaskSample + sy;
                    if (px >= width || py >= height) {
                        continue;
                    }
                    ++sampleCount;
                    if (isCloudPixelSolid(pixels, width, height, px, py)) {
                        ++solidCount;
                    }
                }
            }
            if (sampleCount > 0 && solidCount * kCloudSolidDenominator >= sampleCount * kCloudSolidNumerator) {
                mask[static_cast<size_t>(my * maskWidth + mx)] = 1;
            }
        }
    }

    std::vector<uint8_t> exteriorEmpty(static_cast<size_t>(maskWidth * maskHeight), 0);
    std::deque<glm::ivec2> queue;
    auto enqueueEmpty = [&](const int x, const int y) {
        if (x < 0 || y < 0 || x >= maskWidth || y >= maskHeight) {
            return;
        }
        const size_t idx = static_cast<size_t>(y * maskWidth + x);
        if (mask[idx] != 0 || exteriorEmpty[idx] != 0) {
            return;
        }
        exteriorEmpty[idx] = 1;
        queue.emplace_back(x, y);
    };

    for (int x = 0; x < maskWidth; ++x) {
        enqueueEmpty(x, 0);
        enqueueEmpty(x, maskHeight - 1);
    }
    for (int y = 0; y < maskHeight; ++y) {
        enqueueEmpty(0, y);
        enqueueEmpty(maskWidth - 1, y);
    }
    while (!queue.empty()) {
        const glm::ivec2 p = queue.front();
        queue.pop_front();
        enqueueEmpty(p.x - 1, p.y);
        enqueueEmpty(p.x + 1, p.y);
        enqueueEmpty(p.x, p.y - 1);
        enqueueEmpty(p.x, p.y + 1);
    }

    std::vector<CloudVertex> vertices;
    vertices.reserve(static_cast<size_t>(maskWidth * maskHeight * 18));

    const float cellWorldSize = kCloudCellSize * static_cast<float>(kCloudMaskSample);
    const float tileWidth = static_cast<float>(maskWidth) * cellWorldSize;
    const float tileDepth = static_cast<float>(maskHeight) * cellWorldSize;
    const float originX = -tileWidth * 0.5f;
    const float originZ = -tileDepth * 0.5f;
    const float y0 = -kCloudThickness * 0.5f;
    const float y1 = kCloudThickness * 0.5f;

    std::vector<uint8_t> topBottomSurface = mask;
    std::vector<uint8_t> negXSurface(static_cast<size_t>(maskWidth * maskHeight), 0);
    std::vector<uint8_t> posXSurface(static_cast<size_t>(maskWidth * maskHeight), 0);
    std::vector<uint8_t> negZSurface(static_cast<size_t>(maskWidth * maskHeight), 0);
    std::vector<uint8_t> posZSurface(static_cast<size_t>(maskWidth * maskHeight), 0);

    for (int y = 0; y < maskHeight; ++y) {
        for (int x = 0; x < maskWidth; ++x) {
            if (!isMaskSolid(mask, maskWidth, maskHeight, x, y)) {
                continue;
            }
            const size_t idx = static_cast<size_t>(y * maskWidth + x);
            negXSurface[idx] = isExteriorEmpty(exteriorEmpty, maskWidth, maskHeight, x - 1, y) ? 1 : 0;
            posXSurface[idx] = isExteriorEmpty(exteriorEmpty, maskWidth, maskHeight, x + 1, y) ? 1 : 0;
            negZSurface[idx] = isExteriorEmpty(exteriorEmpty, maskWidth, maskHeight, x, y - 1) ? 1 : 0;
            posZSurface[idx] = isExteriorEmpty(exteriorEmpty, maskWidth, maskHeight, x, y + 1) ? 1 : 0;
        }
    }

    appendGreedySurface(vertices, topBottomSurface, maskWidth, maskHeight,
                        originX, originZ, cellWorldSize, y0, y1, 0, 1.00f);
    appendGreedySurface(vertices, topBottomSurface, maskWidth, maskHeight,
                        originX, originZ, cellWorldSize, y0, y1, 1, 0.70f);
    appendGreedySurface(vertices, negXSurface, maskWidth, maskHeight,
                        originX, originZ, cellWorldSize, y0, y1, 2, 0.82f);
    appendGreedySurface(vertices, posXSurface, maskWidth, maskHeight,
                        originX, originZ, cellWorldSize, y0, y1, 3, 0.82f);
    appendGreedySurface(vertices, negZSurface, maskWidth, maskHeight,
                        originX, originZ, cellWorldSize, y0, y1, 4, 0.76f);
    appendGreedySurface(vertices, posZSurface, maskWidth, maskHeight,
                        originX, originZ, cellWorldSize, y0, y1, 5, 0.88f);

    if (vertices.empty()) {
        return;
    }

    glGenVertexArrays(1, &m_cloudVao);
    glGenBuffers(1, &m_cloudVbo);
    glBindVertexArray(m_cloudVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_cloudVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(CloudVertex)),
                 vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CloudVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(CloudVertex), reinterpret_cast<void*>(sizeof(glm::vec3)));
    glBindVertexArray(0);

    m_cloudVertexCount = static_cast<GLsizei>(vertices.size());
    m_cloudMeshInfo.tileWorldSize = tileWidth;
    m_cloudMeshInfo.valid = true;
}

void GameplaySkyRenderer::renderSkyGradient(const Camera& camera, const float aspect, const SkyColors& colors, GLuint skyCaptureTexture) {
    m_shader->use();
    m_shader->setInt("uMode", 0);
    m_shader->setMat4("uView", buildSkyView(camera));
    m_shader->setMat4("uProjection", glm::perspective(glm::radians(camera.getFOV()), aspect, 0.1f, 100.0f));
    m_shader->setMat4("uModel", glm::mat4(1.0f));
    m_shader->setVec3("uSkyTopColor", colors.top);
    m_shader->setVec3("uSkyHorizonColor", colors.horizon);
    m_shader->setVec3("uSunDirection", colors.sunDirection);
    m_shader->setVec3("uMoonDirection", colors.moonDirection);
    m_shader->setVec3("uSunScatterColor", colors.sunScatter);
    m_shader->setVec3("uMoonLightColor", colors.moonLightColor);
    m_shader->setFloat("uHorizonHaze", colors.horizonHaze);
    m_shader->setFloat("uSunGlare", colors.sunGlare);
    m_shader->setFloat("uSunVisibility", colors.sunVisibility);
    m_shader->setFloat("uMoonVisibility", colors.moonVisibility);
    m_shader->setFloat("uNightFactor", colors.nightFactor);
    m_shader->setVec4("uTintColor", glm::vec4(1.0f));
    m_shader->setVec2("uUvMin", glm::vec2(0.0f));
    m_shader->setVec2("uUvMax", glm::vec2(1.0f));

    // Bind SkyCapture texture for mode 0 visible sky (atmosphere LUT radiance)
    m_shader->setInt("uSkyCaptureTex", 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, skyCaptureTexture);

    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(m_skyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void GameplaySkyRenderer::renderClouds(const Camera& camera,
                                       const float aspect,
                                       const DayNightSystem& dayNight,
                                       const SkyColors& colors) {
    if (m_cloudVao == 0 || !m_cloudMeshInfo.valid) {
        return;
    }

    const glm::vec3 cameraPos = camera.getPosition();
    const float cloudY = kCloudHeight;
    if (cameraPos.y >= cloudY - 2.0f) {
        return;
    }

    const float tileSize = std::max(m_cloudMeshInfo.tileWorldSize, 1.0f);
    const float drift = static_cast<float>(dayNight.getTotalGameTime()) * kCloudDriftSpeed;
    const float baseTileX = std::floor((cameraPos.x - drift) / tileSize);
    const float baseTileZ = std::floor(cameraPos.z / tileSize);

    m_shader->use();
    m_shader->setInt("uMode", 3);
    m_shader->setMat4("uView", camera.getViewMatrix());
    m_shader->setMat4("uProjection", glm::perspective(glm::radians(camera.getFOV()), aspect, 0.1f, 1200.0f));
    m_shader->setVec4("uTintColor", glm::vec4(colors.cloudColor, 1.0f));

    GLboolean cullFaceWasEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean depthMaskWasEnabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWasEnabled);
    GLint previousCullFace = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &previousCullFace);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glBindVertexArray(m_cloudVao);

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const float tileX = baseTileX + static_cast<float>(dx);
            const float tileZ = baseTileZ + static_cast<float>(dz);
            glm::mat4 model(1.0f);
            model = glm::translate(model, glm::vec3(tileX * tileSize + drift, cloudY, tileZ * tileSize));
            m_shader->setMat4("uModel", model);
            glDrawArrays(GL_TRIANGLES, 0, m_cloudVertexCount);
        }
    }

    glBindVertexArray(0);
    if (cullFaceWasEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    glCullFace(previousCullFace);
    glDepthMask(depthMaskWasEnabled);
}

void GameplaySkyRenderer::renderHalo(const Camera& camera,
                                     const float aspect,
                                     const DayNightSystem& dayNight,
                                     const SkyColors& colors) {
    if (m_haloVao == 0 || colors.haloStrength <= 0.001f) {
        return;
    }

    const glm::vec3 direction = directionFromAngle(dayNight.getCelestialAngleRadians());
    const glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = safeNormalize(glm::cross(direction, up), glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 localUp = safeNormalize(glm::cross(right, direction), up);

    glm::mat4 model(1.0f);
    model[0] = glm::vec4(right * kHaloSize, 0.0f);
    model[1] = glm::vec4(localUp * kHaloSize, 0.0f);
    model[2] = glm::vec4(direction, 0.0f);
    model[3] = glm::vec4(direction * 12.0f, 1.0f);

    m_shader->use();
    m_shader->setInt("uMode", 2);
    m_shader->setMat4("uView", buildSkyView(camera));
    m_shader->setMat4("uProjection", glm::perspective(glm::radians(camera.getFOV()), aspect, 0.1f, 100.0f));
    m_shader->setMat4("uModel", model);
    m_shader->setVec4("uTintColor", colors.halo);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(m_haloVao);
    glDrawArrays(GL_TRIANGLES, 0, m_haloVertexCount);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void GameplaySkyRenderer::renderCelestialBody(const Camera& camera,
                                              const float aspect,
                                              const float angleRadians,
                                              const float size,
                                              const GLuint texture,
                                              const glm::vec2& uvMin,
                                              const glm::vec2& uvMax,
                                              const float alpha) {
    if (m_bodyVao == 0 || texture == 0 || alpha <= 0.001f) {
        return;
    }

    const glm::vec3 direction = directionFromAngle(angleRadians);
    if (direction.y < -0.18f) {
        return;
    }

    const float fade = smoothstep(-0.18f, 0.02f, direction.y);
    const glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = safeNormalize(glm::cross(direction, up), glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 localUp = safeNormalize(glm::cross(right, direction), up);

    glm::mat4 model(1.0f);
    model[0] = glm::vec4(right * size, 0.0f);
    model[1] = glm::vec4(localUp * size, 0.0f);
    model[2] = glm::vec4(direction, 0.0f);
    model[3] = glm::vec4(direction * 16.0f, 1.0f);

    m_shader->use();
    m_shader->setInt("uMode", 1);
    m_shader->setMat4("uView", buildSkyView(camera));
    m_shader->setMat4("uProjection", glm::perspective(glm::radians(camera.getFOV()), aspect, 0.1f, 100.0f));
    m_shader->setMat4("uModel", model);
    m_shader->setVec4("uTintColor", glm::vec4(1.0f, 1.0f, 1.0f, alpha * fade));
    m_shader->setVec2("uUvMin", uvMin);
    m_shader->setVec2("uUvMax", uvMax);
    m_shader->setFloat("uBlackKeyThreshold", kBlackKeyThreshold);
    m_shader->setFloat("uBlackKeySoftness", kBlackKeySoftness);
    m_shader->setInt("uTexture", 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(m_bodyVao);
    glDrawArrays(GL_TRIANGLES, 0, m_bodyVertexCount);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

glm::mat4 GameplaySkyRenderer::buildSkyView(const Camera& camera) const {
    return glm::mat4(glm::mat3(camera.getViewMatrix()));
}

glm::vec3 GameplaySkyRenderer::directionFromAngle(const float angleRadians) const {
    return safeNormalize(glm::vec3(0.25f, std::sin(angleRadians), -std::cos(angleRadians)),
                         glm::vec3(0.0f, 1.0f, 0.0f));
}

GameplaySkyRenderer::MoonPhaseUv GameplaySkyRenderer::getMoonPhaseUvInternal(const int phaseIndex) const {
    const auto pair = getMoonPhaseUv(phaseIndex);
    MoonPhaseUv uv;
    uv.uvMin = pair.first;
    uv.uvMax = pair.second;
    return uv;
}
