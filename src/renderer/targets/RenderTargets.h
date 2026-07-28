#ifndef MECRAFT_RENDER_TARGETS_H
#define MECRAFT_RENDER_TARGETS_H

// Formal render target registry for the built-in deferred pipeline.
// Each slot maps to a physical GPU texture owned by DeferredRenderTargets.
// The DerivativeMain colortex equivalent is noted for cross-reference.
//
// This file is documentation-as-code: the enum and PassIO table make the
// buffer contract explicit and auditable with zero runtime cost.

namespace render {

// Logical render target slots.
enum class Target : int {
    // --- GBuffer MRT (written by gbuffers, read by lighting/composite) ---
    GAlbedo       = 0,   // RGBA8   — linear albedo.rgb, emissive hint.a         ≈ colortex6
    GNormalAo     = 1,   // RGB10A2 — octahedral normal.rg, vertex AO.b          ≈ colortex3 (RG)
    GVoxelLight   = 2,   // RG8     — sky light.r, block light.g                 ≈ colortex7 (RG)
    GMaterial     = 3,   // RGBA8   — perceptual roughness.r, specular F90.g, emission.b, SSS.a
    GMaterialAux  = 4,   // RGBA8   — DerivativeMain material id.r, wetness.g, porosity.b, metal.a
    GF0Metallic   = 5,   // RGBA8   — resolved RGB F0.rgb, metallic.a
    GObjectMaterialId = 6, // RG32UI — stable object ID.r, stable material ID.g
    GDepth        = 7,   // DEPTH32F — opaque + transparent depth                ≈ depthtex0

    // --- Deferred outputs ---
    SceneLighting = 10,  // RGBA16F — HDR scene after deferred lighting          ≈ colortex4
    ReflectionData= 11,  // RGBA16F — reflected radiance.rgb, specular weight.a  ≈ colortex2
    CloudData     = 12,  // RGBA16F — scattered light.rgb, transmittance.a       ≈ colortex1
    SkyCapture    = 13,  // RGBA16F — equirectangular sky map + metadata texels   ≈ colortex5
    Velocity      = 14,  // RG16F   — screen-space motion vector xy

    // --- Shadow ---
    ShadowDepth   = 15,  // DEPTH32F — shadow map depth
    ShadowColor   = 16,  // RGBA8   — albedo for colored shadows / caustics
    ShadowNormal  = 17,  // RGBA16F — encoded normal.rg, skylight.b, aux/height.a

    // --- Post / temporal ---
    SceneComposite= 20,  // RGBA16F — opaque HDR after screen-space effects (clouds, reflections)
    SceneResolved = 21,  // RGBA16F — post-TAA HDR scene (input to post-process)
    HistoryScene  = 22,  // RGBA16F — TAA color history (ping-pong)
    HistoryDepth  = 23,  // DEPTH32F — TAA depth history
    HistoryReflect= 24,  // RGBA16F — reflection temporal history
    HistoryCloud  = 25,  // RGBA16F — cloud temporal history
    TemporalCurrent= 26, // RGBA16F — TAA current-frame scratch (avoids reading history[current])

    // --- Utility ---
    SSAO          = 30,  // R8     — raw ambient occlusion
    SSAOFiltered  = 31,  // R8     — bilateral-filtered AO
    HistorySSAO   = 34,  // R8     — SSAO temporal history (ping-pong)
    SSAOHalfRes   = 35,  // R8     — half-res raw ambient occlusion
    SSAOHalfResFiltered = 36, // R8  — half-res bilateral-filtered AO
    VolumetricFog = 32,  // RGBA16F — fog scattered light.rgb + transmittance.a
    HalfRes       = 33,  // RGBA16F — generic half-resolution scratch buffer

    Count
};

// Per-pass I/O contract: which targets each pass reads and writes.
// This is the authoritative reference for the buffer dependency graph.
struct PassIO {
    const char* name;
    Target reads[12];
    Target writes[8];
};

// clang-format off
inline constexpr PassIO kPassTable[] = {
    { "SkyCapture",
      { Target::Count },
      { Target::SkyCapture } },

    { "GBuffer",
      { Target::Count },
      { Target::GAlbedo, Target::GNormalAo, Target::GVoxelLight,
        Target::GMaterial, Target::GMaterialAux, Target::GF0Metallic,
        Target::GObjectMaterialId, Target::GDepth } },

    { "Velocity",
      { Target::GDepth },
      { Target::Velocity } },

    { "Shadow",
      { Target::Count },
      { Target::ShadowDepth, Target::ShadowColor, Target::ShadowNormal } },

    { "SSAO",
      { Target::GDepth, Target::GNormalAo },
      { Target::SSAOHalfRes } },

    { "SSAOFilter",
      { Target::SSAOHalfRes, Target::GDepth, Target::GNormalAo },
      { Target::SSAOHalfResFiltered } },

    { "SSAOUpsample",
      { Target::SSAOHalfResFiltered, Target::GDepth },
      { Target::SSAOFiltered } },

    { "DeferredLighting",
      { Target::GAlbedo, Target::GNormalAo, Target::GVoxelLight,
        Target::GMaterial, Target::GMaterialAux, Target::GF0Metallic,
        Target::GDepth,
        Target::ShadowDepth, Target::ShadowColor, Target::ShadowNormal,
        Target::SSAOFiltered, Target::SkyCapture },
      { Target::SceneLighting } },

    { "Reflection",
      { Target::SceneLighting, Target::GDepth, Target::GNormalAo,
        Target::GMaterial, Target::GMaterialAux, Target::GF0Metallic,
        Target::SkyCapture },
      { Target::ReflectionData } },

    { "ReflectionFilter",
      { Target::ReflectionData, Target::GDepth, Target::GNormalAo,
        Target::GMaterial },
      { Target::ReflectionData } },

    { "ReflectionTemporal",
      { Target::ReflectionData, Target::HistoryReflect, Target::Velocity,
        Target::GDepth, Target::GNormalAo, Target::GMaterial },
      { Target::ReflectionData } },

    { "Cloud",
      { Target::GDepth, Target::SkyCapture },
      { Target::CloudData } },

    { "SceneComposite",
      { Target::SceneLighting, Target::CloudData, Target::ReflectionData,
        Target::GDepth, Target::GMaterial, Target::GMaterialAux,
        Target::SkyCapture, Target::GAlbedo },
      { Target::SceneComposite } },

    { "VolumetricFog",
      { Target::GDepth, Target::SkyCapture, Target::ShadowDepth,
        Target::ShadowColor },
      { Target::VolumetricFog } },

    { "VolumetricComposite",
      { Target::SceneComposite, Target::VolumetricFog, Target::GDepth },
      { Target::SceneComposite } },

    { "TemporalResolve",
      { Target::SceneComposite, Target::HistoryScene, Target::Velocity,
        Target::GDepth, Target::HistoryDepth },
      { Target::SceneResolved } },

    { "MotionBlur",
      { Target::SceneResolved, Target::Velocity, Target::GDepth },
      { Target::SceneResolved } },

    { "DoF",
      { Target::SceneResolved, Target::GDepth },
      { Target::SceneResolved } },

    { "WaterComposite",
      { Target::GDepth, Target::SceneResolved, Target::ReflectionData,
        Target::SkyCapture, Target::VolumetricFog },
      { Target::SceneResolved } },

    { "TransparentComposite",
      { Target::GDepth, Target::SceneResolved, Target::SkyCapture },
      { Target::Count } },  // writes to default framebuffer

    { "SSAOTemporal",
      { Target::SSAOFiltered, Target::Velocity, Target::GDepth, Target::HistorySSAO },
      { Target::HistorySSAO } },

    { "PostProcess",
      { Target::SceneResolved },
      { Target::Count } },  // writes to default framebuffer
};
// clang-format on

inline constexpr int kPassCount = sizeof(kPassTable) / sizeof(kPassTable[0]);

} // namespace render

#endif // MECRAFT_RENDER_TARGETS_H
