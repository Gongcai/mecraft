#ifndef MECRAFT_FRAME_OUTPUT_H
#define MECRAFT_FRAME_OUTPUT_H

#include "renderer/rhi/RhiHandles.h"

#include <cstdint>
#include <glm/glm.hpp>

/// Shadow data for first-person held item rendering
struct FirstPersonShadowData {
    glm::mat4 cascadeViewProj[4]{};
    float cascadeSplitFar[4]{};
    float cascadeTexelWorldSize[4]{};
    float cascadeDepthExtent[4]{};
    RhiTextureHandle shadowTextureHandle;
    RhiTextureHandle shadowDepthRawHandle;
    RhiTextureHandle shadowDepthAllHandle;
    RhiTextureHandle shadowDepthAllRawHandle;
    RhiTextureHandle shadowColor0Handle;
    RhiTextureHandle shadowColor1Handle;
    glm::vec3 cameraPos = glm::vec3(0.0f);
    glm::vec3 sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float shadowDistance = 192.0f;
    float constantBias = 0.0007f;
    float slopeBias = 0.0022f;
    float normalOffset = 0.035f;
    float softness = 1.0f;
    float pcssStrength = 0.72f;
    int cascadeCount = 4;
    int softShadowsEnabled = 1;
    int pcssShadowsEnabled = 1;
    int shadowsEnabled = 1;
    float skyIntensity = 1.0f;
};

/// Output contract from render pipeline to RenderScene / PostProcess
/// Replaces implicit state queries like isDeferredFrameActive()
struct FrameOutput {
    // Scene render targets (post-tonemap or HDR depending on pipeline)
    RhiTextureHandle sceneColor;
    RhiTextureHandle sceneDepth;
    uint32_t sceneColorTex = 0;
    uint32_t sceneDepthTex = 0;

    // GBuffer depth (for effects that need scene depth in deferred)
    RhiTextureHandle gbufferDepth;
    uint32_t gbufferDepthTex = 0;

    // Weather mask (for rain/snow particles in post-process)
    RhiTextureHandle weatherMask;
    uint32_t weatherMaskTex = 0;

    // Deferred pipeline capabilities
    bool hasDeferredInputs = false;
    bool hasDebugView = false;
    bool skipPostProcess = false;

    // Shadow data for held item rendering
    FirstPersonShadowData heldItemShadow{};
};

#endif // MECRAFT_FRAME_OUTPUT_H
