#ifndef MECRAFT_SETTINGS_MAPPER_H
#define MECRAFT_SETTINGS_MAPPER_H

#include "RenderSettings.h"
#include "Renderer.h"

/// Utility to sync Renderer-owned settings into the unified RenderSettings struct.
/// Fog settings are managed separately via Renderer::FogSettings (not part of RenderSettings),
/// so this dedicated sync path bridges the two.
namespace settings_mapper {

/// Sync fog settings from Renderer::FogSettings to RenderSettings::FogSettings.
inline void syncFogToRenderSettings(const Renderer::FogSettings& src, FogSettings& dst) {
    dst.enabled = src.enabled;
    dst.mode = static_cast<int>(src.mode);
    dst.color = src.color;
    dst.startDistance = src.startDistance;
    dst.endDistance = src.endDistance;
    dst.density = src.density;
    dst.autoDistanceByRenderDistance = src.autoDistanceByRenderDistance;
    dst.autoEndOffsetChunks = src.autoEndOffsetChunks;
    dst.autoFadeWidthChunks = src.autoFadeWidthChunks;
}

} // namespace settings_mapper

#endif // MECRAFT_SETTINGS_MAPPER_H
