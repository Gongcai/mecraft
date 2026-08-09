#include "renderer/contracts/OpacityMicromapContract.h"

#include <limits>

namespace renderer::contracts {
namespace {

[[nodiscard]] bool validTriangle(const OpacityMicromapTriangle& triangle, const uint32_t maxTwoState,
                                 const uint32_t maxFourState) {
    const uint32_t maxSubdivision = triangle.format == OpacityMicromapFormat::TwoState ? maxTwoState : maxFourState;
    if (triangle.subdivisionLevel > maxSubdivision) {
        return false;
    }
    if (triangle.format == OpacityMicromapFormat::TwoState) {
        return triangle.state <= 1u;
    }
    if (triangle.format == OpacityMicromapFormat::FourState) {
        return triangle.state <= 3u;
    }
    return false;
}

[[nodiscard]] bool validTransition(const OpacityMicromapAssetState current, const OpacityMicromapAssetState next) {
    switch (current) {
    case OpacityMicromapAssetState::Empty: return next == OpacityMicromapAssetState::CpuReady;
    case OpacityMicromapAssetState::CpuReady: return next == OpacityMicromapAssetState::GpuBuildPending;
    case OpacityMicromapAssetState::GpuBuildPending: return next == OpacityMicromapAssetState::Resident;
    case OpacityMicromapAssetState::Resident: return next == OpacityMicromapAssetState::Retired;
    case OpacityMicromapAssetState::Retired: return false;
    }
    return false;
}

} // namespace

bool validOpacityMicromapAsset(const OpacityMicromapAsset& asset) {
    if (asset.alphaTextureHash == 0u || asset.profileHash == 0u || asset.primitiveCount == 0u ||
        asset.triangles.size() != asset.primitiveCount ||
        asset.triangles.size() > std::numeric_limits<uint32_t>::max() || asset.maxTwoStateSubdivisionLevel > 31u ||
        asset.maxFourStateSubdivisionLevel > 31u) {
        return false;
    }
    for (const OpacityMicromapTriangle& triangle : asset.triangles) {
        if (!validTriangle(triangle, asset.maxTwoStateSubdivisionLevel, asset.maxFourStateSubdivisionLevel)) {
            return false;
        }
    }
    return asset.state == OpacityMicromapAssetState::Empty || asset.state == OpacityMicromapAssetState::CpuReady;
}

bool transitionOpacityMicromapAsset(OpacityMicromapAsset& asset, const OpacityMicromapAssetState next) {
    if (!validTransition(asset.state, next)) {
        return false;
    }
    if (next == OpacityMicromapAssetState::CpuReady && !validOpacityMicromapAsset(asset)) {
        return false;
    }
    asset.state = next;
    return true;
}

} // namespace renderer::contracts
