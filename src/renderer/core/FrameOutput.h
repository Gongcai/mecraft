#ifndef MECRAFT_FRAME_OUTPUT_H
#define MECRAFT_FRAME_OUTPUT_H

#include "renderer/rhi/RhiHandles.h"

#include <cstdint>
#include <glm/glm.hpp>

/// Storage encoding published for the NRD diffuse output texture.
enum class NrdDiffuseOutputEncoding : uint8_t { LinearRgb = 0, ReblurYCoCg };

/// Output contract from render pipeline to RenderScene / PostProcess
/// Replaces implicit state queries like isDeferredFrameActive()
struct FrameOutput {
    // Scene render targets (post-tonemap or HDR depending on pipeline)
    RhiTextureHandle sceneColor;
    RhiTextureHandle sceneDepth;

    // GBuffer depth (for effects that need scene depth in deferred)
    RhiTextureHandle gbufferDepth;

    // Weather mask (for rain/snow particles in post-process)
    RhiTextureHandle weatherMask;

    // Temporal reconstruction masks at render resolution.
    RhiTextureHandle reactiveMask;
    RhiTextureHandle transparencyMask;

    // Production RTGI signals. Raw is pre-exposed while NRD output is scene-referred.
    // These graph-owned handles remain valid until the next deferred graph execution.
    RhiTextureHandle rtgiRawDiffuse;
    RhiTextureHandle nrdDiffuse;
    RhiTextureHandle rtgiLeakageNormal;
    RhiTextureHandle rtgiLeakageViewZ;
    NrdDiffuseOutputEncoding nrdDiffuseEncoding = NrdDiffuseOutputEncoding::LinearRgb;
    float nrdDiffuseToPreExposedScale = 1.0f;

    // Deferred pipeline capabilities
    bool hasDeferredInputs = false;
    bool hasDebugView = false;
    bool skipPostProcess = false;
    bool hasRtgiRawDiffuse = false;
    bool hasNrdDiffuse = false;
    bool hasRtgiLeakageGuides = false;
};

#endif // MECRAFT_FRAME_OUTPUT_H
