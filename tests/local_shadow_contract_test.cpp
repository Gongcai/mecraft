#include "renderer/contracts/LocalShadowContract.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[local_shadow_contract_test] FAIL: "
                  << message << '\n';
        return false;
    }
    return true;
}

renderer::contracts::SceneLight makeSceneLight(
    const uint32_t stableId,
    const renderer::contracts::GpuLightType type,
    const renderer::contracts::GpuLightShadowPolicy policy) {
    using namespace renderer::contracts;
    SceneLight result;
    result.light.positionAndRange = {0.0f, 1.0f, -4.0f, 8.0f};
    result.light.direction = type == GpuLightType::Spot
        ? glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)
        : glm::vec4(0.0f);
    result.light.colorAndIntensity = {1.0f, 0.8f, 0.6f, 100.0f};
    result.light.spotCosinesAndRectSize = type == GpuLightType::Spot
        ? glm::vec4(0.95f, 0.8f, 0.0f, 0.0f)
        : glm::vec4(0.0f);
    result.light.classificationAndIdentity = {
        static_cast<uint32_t>(type), stableId,
        static_cast<uint32_t>(GpuLightShadowPolicy::None),
        kGpuLightInvalidResourceIndex};
    result.light.resourcesAndFlags = {
        kGpuLightInvalidResourceIndex, kGpuLightInvalidResourceIndex,
        gpuLightContributionFlagBit(GpuLightContributionFlag::Diffuse) |
            gpuLightContributionFlagBit(GpuLightContributionFlag::Specular),
        kGpuLightContractVersion};
    result.requestedShadowPolicy = policy;
    return result;
}

std::unordered_map<uint32_t, renderer::contracts::LocalShadowAllocation>
allocationsById(
    const std::vector<renderer::contracts::LocalShadowAllocation>&
        allocations) {
    std::unordered_map<
        uint32_t, renderer::contracts::LocalShadowAllocation> result;
    for (const auto& allocation : allocations) {
        result.emplace(allocation.lightId.value, allocation);
    }
    return result;
}

bool testStableSlotsAndDeterministicInsertion() {
    using namespace renderer::contracts;
    LocalShadowStableAllocator allocator;
    std::vector<LocalShadowAllocation> allocations;
    std::vector<SceneLight> first{
        makeSceneLight(30u, GpuLightType::Point,
                       GpuLightShadowPolicy::RasterDynamic),
        makeSceneLight(10u, GpuLightType::Point,
                       GpuLightShadowPolicy::RasterCached),
        makeSceneLight(20u, GpuLightType::Point,
                       GpuLightShadowPolicy::RasterDynamic),
        makeSceneLight(40u, GpuLightType::Spot,
                       GpuLightShadowPolicy::RasterCached)};
    if (!requireTrue(allocator.allocate(first, allocations),
                     "the first complete snapshot must allocate")) {
        return false;
    }
    auto indexed = allocationsById(allocations);
    if (!requireTrue(
            indexed.at(10u).resourceSlot == 0u &&
                indexed.at(20u).resourceSlot == 1u &&
                indexed.at(30u).resourceSlot == 2u &&
                indexed.at(40u).resourceSlot == 0u,
            "new IDs must receive the lowest free type-local slot in ID order") ||
        !requireTrue(
            indexed.at(10u).metadataIndex ==
                    kLocalShadowPointMetadataBase &&
                indexed.at(40u).metadataIndex == 0u,
            "Point and Spot slots must map to disjoint metadata ranges")) {
        return false;
    }

    std::vector<SceneLight> reordered{
        first[3], first[0], first[2], first[1]};
    if (!requireTrue(allocator.allocate(reordered, allocations),
                     "a reordered complete snapshot must allocate")) {
        return false;
    }
    indexed = allocationsById(allocations);
    return requireTrue(
        indexed.at(10u).resourceSlot == 0u &&
            indexed.at(20u).resourceSlot == 1u &&
            indexed.at(30u).resourceSlot == 2u &&
            indexed.at(40u).resourceSlot == 0u,
        "existing stable IDs must retain their resource slots");
}

bool testDeletionAndLowestFreeReuse() {
    using namespace renderer::contracts;
    LocalShadowStableAllocator allocator;
    std::vector<LocalShadowAllocation> allocations;
    const SceneLight id10 = makeSceneLight(
        10u, GpuLightType::Point, GpuLightShadowPolicy::RasterDynamic);
    const SceneLight id20 = makeSceneLight(
        20u, GpuLightType::Point, GpuLightShadowPolicy::RasterDynamic);
    const SceneLight id30 = makeSceneLight(
        30u, GpuLightType::Point, GpuLightShadowPolicy::RasterDynamic);
    if (!allocator.allocate({id10, id20, id30}, allocations) ||
        !allocator.allocate({id10, id30}, allocations)) {
        return requireTrue(false, "deletion snapshots must allocate");
    }
    const SceneLight id25 = makeSceneLight(
        25u, GpuLightType::Point, GpuLightShadowPolicy::RasterDynamic);
    if (!requireTrue(allocator.allocate({id30, id25, id10}, allocations),
                     "a new ID must allocate after deletion")) {
        return false;
    }
    auto indexed = allocationsById(allocations);
    if (!requireTrue(
            indexed.at(10u).resourceSlot == 0u &&
                indexed.at(25u).resourceSlot == 1u &&
                indexed.at(30u).resourceSlot == 2u,
            "a new ID must reuse the lowest released slot")) {
        return false;
    }

    const SceneLight id40 = makeSceneLight(
        40u, GpuLightType::Point, GpuLightShadowPolicy::RasterDynamic);
    if (!requireTrue(allocator.allocate({id30, id40}, allocations),
                     "a second deletion snapshot must allocate")) {
        return false;
    }
    indexed = allocationsById(allocations);
    return requireTrue(
        indexed.at(30u).resourceSlot == 2u &&
            indexed.at(40u).resourceSlot == 0u,
        "surviving IDs must retain slots while new IDs reuse the lowest gap");
}

bool testTransactionalCapacityFailure() {
    using namespace renderer::contracts;
    LocalShadowStableAllocator allocator;
    std::vector<LocalShadowAllocation> allocations;
    const SceneLight id100 = makeSceneLight(
        100u, GpuLightType::Point, GpuLightShadowPolicy::RasterDynamic);
    const SceneLight id200 = makeSceneLight(
        200u, GpuLightType::Point, GpuLightShadowPolicy::RasterDynamic);
    const SceneLight id300 = makeSceneLight(
        300u, GpuLightType::Point, GpuLightShadowPolicy::RasterDynamic);
    if (!allocator.allocate({id100, id200, id300}, allocations) ||
        !allocator.allocate({id300}, allocations)) {
        return requireTrue(false, "capacity precondition snapshots must allocate");
    }

    std::vector<SceneLight> overflow;
    overflow.reserve(kLocalShadowMaxPointLightCount + 1u);
    for (uint32_t index = 0u;
         index <= kLocalShadowMaxPointLightCount; ++index) {
        overflow.push_back(makeSceneLight(
            1000u + index, GpuLightType::Point,
            GpuLightShadowPolicy::RasterDynamic));
    }
    std::vector<LocalShadowAllocation> unchanged{{
        99u, StableLightId{999u}, LocalShadowType::Spot,
        GpuLightShadowPolicy::RasterCached, 7u, 7u}};
    if (!requireTrue(!allocator.allocate(overflow, unchanged),
                     "Point capacity overflow must fail") ||
        !requireTrue(
            allocator.failure().error ==
                    LocalShadowAllocationError::PointCapacityExceeded &&
                unchanged.size() == 1u &&
                unchanged[0].lightId.value == 999u,
            "capacity failure must not publish partial allocations")) {
        return false;
    }

    const SceneLight id50 = makeSceneLight(
        50u, GpuLightType::Point, GpuLightShadowPolicy::RasterDynamic);
    if (!requireTrue(allocator.allocate({id300, id50}, allocations),
                     "allocation must continue after a failed transaction")) {
        return false;
    }
    const auto indexed = allocationsById(allocations);
    return requireTrue(
        indexed.at(300u).resourceSlot == 2u &&
            indexed.at(50u).resourceSlot == 0u,
        "capacity failure must preserve the previously committed slot map");
}

bool testStructuredFailures() {
    using namespace renderer::contracts;
    std::vector<LocalShadowAllocation> allocations;

    LocalShadowStableAllocator duplicateAllocator;
    const SceneLight duplicate = makeSceneLight(
        1u, GpuLightType::Point, GpuLightShadowPolicy::RasterDynamic);
    if (!requireTrue(
            !duplicateAllocator.allocate({duplicate, duplicate}, allocations) &&
                duplicateAllocator.failure().error ==
                    LocalShadowAllocationError::DuplicateStableId,
            "duplicate stable IDs must fail explicitly")) {
        return false;
    }

    LocalShadowStableAllocator typeAllocator;
    const SceneLight point = makeSceneLight(
        2u, GpuLightType::Point, GpuLightShadowPolicy::RasterDynamic);
    const SceneLight spot = makeSceneLight(
        2u, GpuLightType::Spot, GpuLightShadowPolicy::RasterDynamic);
    if (!typeAllocator.allocate({point}, allocations) ||
        !requireTrue(
            !typeAllocator.allocate({spot}, allocations) &&
                typeAllocator.failure().error ==
                    LocalShadowAllocationError::StableIdTypeChanged,
            "one stable ID must not change raster resource type")) {
        return false;
    }

    LocalShadowStableAllocator rayAllocator;
    const SceneLight ray = makeSceneLight(
        3u, GpuLightType::Point, GpuLightShadowPolicy::RayQuery);
    if (!requireTrue(
            !rayAllocator.allocate({ray}, allocations) &&
                rayAllocator.failure().error ==
                    LocalShadowAllocationError::RayQueryUnavailable,
            "Ray Query requests must fail until their resource path exists")) {
        return false;
    }

    LocalShadowStableAllocator unsupportedAllocator;
    const SceneLight directional = makeSceneLight(
        4u, GpuLightType::Directional,
        GpuLightShadowPolicy::RasterDynamic);
    if (!requireTrue(
            !unsupportedAllocator.allocate({directional}, allocations) &&
                unsupportedAllocator.failure().error ==
                    LocalShadowAllocationError::UnsupportedLightType,
            "unsupported raster light types must fail explicitly")) {
        return false;
    }

    LocalShadowStableAllocator invalidAllocator;
    SceneLight invalid = makeSceneLight(
        5u, GpuLightType::Point, GpuLightShadowPolicy::RasterDynamic);
    invalid.light.resourcesAndFlags.w = 0u;
    return requireTrue(
        !invalidAllocator.allocate({invalid}, allocations) &&
            invalidAllocator.failure().error ==
                LocalShadowAllocationError::InvalidSceneLight,
        "invalid unallocated scene records must fail explicitly");
}

bool testCpuAndGlslMirror() {
    using namespace renderer::contracts;
    static_assert(offsetof(LocalShadowMetadata,
                           cameraRelativeViewProjection) == 0u);
    static_assert(offsetof(LocalShadowMetadata, atlasScaleBias) == 384u);
    static_assert(offsetof(LocalShadowMetadata,
                           nearFarDepthBiasNormalOffset) == 400u);
    static_assert(offsetof(LocalShadowMetadata, classification) == 416u);

    const std::string path = std::string(MECRAFT_TEST_SOURCE_DIR) +
        "/assets/shaders/local_shadow_contract.glsl";
    std::ifstream stream(path, std::ios::binary);
    if (!requireTrue(stream.is_open(),
                     "local-shadow GLSL contract must be readable")) {
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
    if (!requireTrue(
            source.find("LOCAL_SHADOW_CONTRACT_VERSION = 1u") !=
                    std::string::npos &&
                source.find("LOCAL_SHADOW_SPOT_METADATA_COUNT = 64u") !=
                    std::string::npos &&
                source.find("LOCAL_SHADOW_POINT_METADATA_COUNT = 64u") !=
                    std::string::npos &&
                source.find("LOCAL_SHADOW_POINT_METADATA_BASE = 64u") !=
                    std::string::npos &&
                source.find("LOCAL_SHADOW_METADATA_COUNT = 128u") !=
                    std::string::npos,
            "GLSL constants must mirror CPU capacities and metadata ranges")) {
        return false;
    }

    constexpr std::array<const char*, 4> fields{
        "mat4 cameraRelativeViewProjection[6];",
        "vec4 atlasScaleBias;",
        "vec4 nearFarDepthBiasNormalOffset;",
        "uvec4 classification;"};
    std::size_t offset = 0u;
    for (const char* field : fields) {
        const std::size_t found = source.find(field, offset);
        if (!requireTrue(found != std::string::npos,
                         "GLSL metadata fields must mirror CPU order")) {
            return false;
        }
        offset = found + std::string(field).size();
    }
    return true;
}

} // namespace

int main() {
    if (!testStableSlotsAndDeterministicInsertion() ||
        !testDeletionAndLowestFreeReuse() ||
        !testTransactionalCapacityFailure() ||
        !testStructuredFailures() || !testCpuAndGlslMirror()) {
        return 1;
    }
    std::cout << "[local_shadow_contract_test] PASS\n";
    return 0;
}
