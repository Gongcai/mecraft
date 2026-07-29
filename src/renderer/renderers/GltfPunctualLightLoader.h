#ifndef MECRAFT_GLTF_PUNCTUAL_LIGHT_LOADER_H
#define MECRAFT_GLTF_PUNCTUAL_LIGHT_LOADER_H

#include "renderer/contracts/GpuLightContract.h"

#include <cstdint>

struct cgltf_node;

namespace renderer::assets {

/// Identifies deterministic failures while decoding one
/// KHR_lights_punctual node from a glTF default scene.
enum class GltfPunctualLightDecodeError : uint8_t {
    None,
    MissingLight,
    InvalidType,
    InvalidTransform,
    InvalidPhysicalValue
};

/// Returns one asset-local analytic light or the exact failed contract field.
struct GltfPunctualLightDecodeResult final {
    renderer::contracts::AnalyticLightSourceDefinition source;
    GltfPunctualLightDecodeError error =
        GltfPunctualLightDecodeError::None;
    renderer::contracts::GpuLightNormalizationError normalizationError =
        renderer::contracts::GpuLightNormalizationError::None;
    renderer::contracts::GpuLightField normalizationField =
        renderer::contracts::GpuLightField::None;
    renderer::contracts::AnalyticLightInstantiationError instantiationError =
        renderer::contracts::AnalyticLightInstantiationError::None;

    /// Reports whether the glTF node produced a complete physical light.
    /// @return True only when source contains a validated light definition.
    [[nodiscard]] bool succeeded() const;
};

/// Decodes one glTF node carrying KHR_lights_punctual. The node's complete
/// hierarchy transform is baked into asset-local position and direction;
/// glTF scale does not alter range or intensity.
/// @param node Default-scene node whose light pointer was resolved by cgltf.
/// @return Validated analytic source or a structured decode failure.
[[nodiscard]] GltfPunctualLightDecodeResult decodeGltfPunctualLight(
    const cgltf_node& node);

/// Returns the stable identifier for one glTF punctual-light decode error.
/// @param error Error to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* gltfPunctualLightDecodeErrorStableId(
    GltfPunctualLightDecodeError error);

} // namespace renderer::assets

#endif // MECRAFT_GLTF_PUNCTUAL_LIGHT_LOADER_H
