#ifndef MECRAFT_SKY_IBL_CONTRACT_H
#define MECRAFT_SKY_IBL_CONTRACT_H

#include <algorithm>
#include <cstdint>

namespace renderer::contracts {

inline constexpr uint32_t kSkyIblCubeExtent = 128u;
inline constexpr uint32_t kSkyIblCubeMipCount = 8u;
inline constexpr uint32_t kSkyIblDfgExtent = 256u;
inline constexpr uint32_t kSkyIblGgxSampleCount = 128u;

/// Converts one specular-prefilter mip into perceptual material roughness.
/// @param mip Mip level in the complete sky GGX prefilter chain.
/// @return Roughness in [0, 1], with the last mip representing roughness one.
[[nodiscard]] constexpr float skyIblRoughnessForMip(const uint32_t mip) {
  const uint32_t boundedMip = std::min(mip, kSkyIblCubeMipCount - 1u);
  return static_cast<float>(boundedMip) /
         static_cast<float>(kSkyIblCubeMipCount - 1u);
}

/// Converts perceptual material roughness into the fractional prefilter mip.
/// @param roughness Perceptual roughness supplied by the shared material
/// contract.
/// @return Fractional mip coordinate clamped to the complete prefilter chain.
[[nodiscard]] constexpr float skyIblMipForRoughness(const float roughness) {
  return std::clamp(roughness, 0.0f, 1.0f) *
         static_cast<float>(kSkyIblCubeMipCount - 1u);
}

} // namespace renderer::contracts

#endif // MECRAFT_SKY_IBL_CONTRACT_H
