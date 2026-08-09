#ifndef MECRAFT_OPACITY_MICROMAP_CONTRACT_H
#define MECRAFT_OPACITY_MICROMAP_CONTRACT_H

#include <cstdint>
#include <optional>
#include <vector>

namespace renderer::contracts {

enum class OpacityMicromapFormat : uint8_t { TwoState = 0u, FourState = 1u };
enum class OpacityMicromapAssetState : uint8_t {
    Empty = 0u,
    CpuReady = 1u,
    GpuBuildPending = 2u,
    Resident = 3u,
    Retired = 4u
};

struct OpacityMicromapTriangle final {
    uint8_t state = 0u;
    uint8_t subdivisionLevel = 0u;
    OpacityMicromapFormat format = OpacityMicromapFormat::TwoState;
};

struct OpacityMicromapAsset final {
    uint64_t alphaTextureHash = 0u;
    uint64_t profileHash = 0u;
    uint32_t primitiveCount = 0u;
    uint32_t maxTwoStateSubdivisionLevel = 0u;
    uint32_t maxFourStateSubdivisionLevel = 0u;
    std::vector<OpacityMicromapTriangle> triangles;
    OpacityMicromapAssetState state = OpacityMicromapAssetState::Empty;
};

/// Validates the immutable Alpha identity and one-to-one triangle mapping used by an OMM build.
[[nodiscard]] bool validOpacityMicromapAsset(const OpacityMicromapAsset& asset);

/// Applies one explicit lifecycle transition to a micromap asset.
/// @return False when the transition would expose incomplete data to a GPU build or BLAS.
[[nodiscard]] bool transitionOpacityMicromapAsset(OpacityMicromapAsset& asset, OpacityMicromapAssetState next);

} // namespace renderer::contracts

#endif // MECRAFT_OPACITY_MICROMAP_CONTRACT_H
