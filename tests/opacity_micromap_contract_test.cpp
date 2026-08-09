#include "renderer/contracts/OpacityMicromapContract.h"

#include <cstdlib>
#include <iostream>

using namespace renderer::contracts;

int main() {
    OpacityMicromapAsset asset;
    asset.alphaTextureHash = 0x11u;
    asset.profileHash = 0x22u;
    asset.primitiveCount = 2u;
    asset.maxTwoStateSubdivisionLevel = 2u;
    asset.maxFourStateSubdivisionLevel = 3u;
    asset.triangles = {{0u, 2u, OpacityMicromapFormat::TwoState}, {3u, 3u, OpacityMicromapFormat::FourState}};
    if (!validOpacityMicromapAsset(asset) ||
        !transitionOpacityMicromapAsset(asset, OpacityMicromapAssetState::CpuReady) ||
        !transitionOpacityMicromapAsset(asset, OpacityMicromapAssetState::GpuBuildPending) ||
        !transitionOpacityMicromapAsset(asset, OpacityMicromapAssetState::Resident) ||
        !transitionOpacityMicromapAsset(asset, OpacityMicromapAssetState::Retired) ||
        transitionOpacityMicromapAsset(asset, OpacityMicromapAssetState::Resident)) {
        std::cerr << "opacity_micromap_contract_test: lifecycle contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
