#include "renderer/contracts/BindlessDescriptorContract.h"
#include "renderer/contracts/GpuSceneContract.h"
#include "renderer/core/BindlessDescriptorSlotAllocator.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[gpu_scene_contract_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool near(const glm::vec3& lhs, const glm::vec3& rhs, const float tolerance = 1.0e-5f) {
    return std::abs(lhs.x - rhs.x) <= tolerance && std::abs(lhs.y - rhs.y) <= tolerance &&
           std::abs(lhs.z - rhs.z) <= tolerance;
}

bool testBindlessHandleDomains() {
    using namespace renderer::contracts;

    static_assert(!std::is_same_v<BindlessTexture2DHandle, BindlessTextureCubeHandle>);
    static_assert(!std::is_same_v<BindlessTexture2DHandle, BindlessSamplerHandle>);
    static_assert(!std::is_convertible_v<BindlessTexture2DHandle, BindlessStorageBufferHandle>);

    const renderer::core::BindlessDescriptorSlotAllocationResult<BindlessTexture2DTag> defaultAllocation;
    const BindlessTexture2DHandle invalid;
    const BindlessTexture2DHandle firstSlot{0u, 1u};
    const BindlessTexture2DHandle sameFirstSlot{0u, 1u};
    const BindlessTexture2DHandle newerFirstSlot{0u, 2u};
    return requireTrue(static_cast<uint32_t>(GlobalBindlessBinding::SampledTexture2D) == 0u &&
                           static_cast<uint32_t>(GlobalBindlessBinding::SampledTextureCube) == 1u &&
                           static_cast<uint32_t>(GlobalBindlessBinding::Sampler) == 2u &&
                           static_cast<uint32_t>(GlobalBindlessBinding::StorageBuffer) == 3u &&
                           static_cast<uint32_t>(GlobalBindlessBinding::AccelerationStructure) == 4u &&
                           kGlobalBindlessBindingCount == 5u,
                       "global bindless bindings must preserve their frozen set order") &&
           requireTrue(!invalid.isValid(), "default bindless handles must be invalid") &&
           requireTrue(!defaultAllocation.succeeded(), "default allocation results must not report success") &&
           requireTrue(firstSlot.isValid(), "zero must remain a valid descriptor-array slot") &&
           requireTrue(firstSlot == sameFirstSlot && firstSlot != newerFirstSlot,
                       "bindless handle equality must include generation");
}

bool testBindlessSlotLifecycle() {
    using namespace renderer::contracts;
    using namespace renderer::core;

    BindlessDescriptorSlotAllocator<BindlessTexture2DTag> allocator(3u);
    const auto first = allocator.allocate();
    const auto second = allocator.allocate();
    const auto third = allocator.allocate();
    const auto full = allocator.allocate();
    if (!requireTrue(first.succeeded() && second.succeeded() && third.succeeded(),
                     "bindless allocator must fill every configured slot") ||
        !requireTrue(first.handle == BindlessTexture2DHandle{0u, 1u} &&
                         second.handle == BindlessTexture2DHandle{1u, 1u} &&
                         third.handle == BindlessTexture2DHandle{2u, 1u},
                     "fresh bindless slots must use ascending zero-based indices") ||
        !requireTrue(!full.succeeded() && full.error == BindlessDescriptorSlotError::CapacityExceeded &&
                         std::string(bindlessDescriptorSlotErrorStableId(full.error)) ==
                             "BindlessDescriptorCapacityExceeded",
                     "full bindless tables must return the stable capacity error")) {
        return false;
    }

    BindlessDescriptorSlotAllocator<BindlessSamplerTag> transactionalAllocator(1u);
    const auto rejected = transactionalAllocator.allocateAndPublish(
        [](const BindlessSamplerHandle handle) { return handle.index != 0u; });
    const BindlessDescriptorSlotStats rejectedStats = transactionalAllocator.stats();
    const auto published = transactionalAllocator.allocateAndPublish(
        [](const BindlessSamplerHandle handle) { return handle == BindlessSamplerHandle{0u, 1u}; });
    if (!requireTrue(rejected.error == BindlessDescriptorSlotError::PublicationRejected &&
                         rejectedStats.availableCount == 1u && rejectedStats.liveCount == 0u && published.succeeded(),
                     "rejected descriptor publication must preserve the candidate slot for the next transaction")) {
        return false;
    }

    if (!requireTrue(allocator.retire(second.handle, 9u) == BindlessDescriptorSlotError::None &&
                         allocator.retire(first.handle, 5u) == BindlessDescriptorSlotError::None,
                     "live slots must retire with independent submission sequences") ||
        !requireTrue(allocator.retire(first.handle, 5u) == BindlessDescriptorSlotError::SlotNotLive,
                     "retiring the same generation twice must be rejected") ||
        !requireTrue(allocator.retire({}, 5u) == BindlessDescriptorSlotError::InvalidHandle,
                     "invalid handles must not mutate the retirement heap") ||
        !requireTrue(allocator.reclaim(4u).reclaimedCount == 0u,
                     "slots must remain unavailable before their last submission completes") ||
        !requireTrue(!allocator.allocate().succeeded(), "retired slots must still consume table capacity")) {
        return false;
    }

    const BindlessDescriptorSlotReclaimResult firstReclaim = allocator.reclaim(5u);
    const auto reusedFirst = allocator.allocate();
    if (!requireTrue(firstReclaim.reclaimedCount == 1u && firstReclaim.exhaustedCount == 0u,
                     "the earliest completed retirement must reclaim independently") ||
        !requireTrue(reusedFirst.succeeded() && reusedFirst.handle == BindlessTexture2DHandle{0u, 2u},
                     "reused descriptor slots must increment generation") ||
        !requireTrue(!allocator.isLive(first.handle) && allocator.isLive(reusedFirst.handle),
                     "stale generations must never validate after slot reuse") ||
        !requireTrue(allocator.retire(first.handle, 10u) == BindlessDescriptorSlotError::StaleGeneration,
                     "stale handles must not retire the current live generation")) {
        return false;
    }

    const BindlessDescriptorSlotReclaimResult secondReclaim = allocator.reclaim(9u);
    const auto reusedSecond = allocator.allocate();
    const BindlessDescriptorSlotStats stats = allocator.stats();
    return requireTrue(secondReclaim.reclaimedCount == 1u && reusedSecond.succeeded() &&
                           reusedSecond.handle == BindlessTexture2DHandle{1u, 2u},
                       "out-of-order retirements must reclaim by completed sequence") &&
           requireTrue(stats.capacity == 3u && stats.liveCount == 3u && stats.retiredCount == 0u &&
                           stats.availableCount == 0u && stats.exhaustedCount == 0u && stats.peakLiveCount == 3u,
                       "bindless occupancy statistics must remain constant-time and exact");
}

renderer::contracts::GpuSceneInstanceNormalizationInput validInstanceInput() {
    using namespace renderer::contracts;

    GpuSceneInstanceNormalizationInput input;
    input.worldFromObject = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, -2.0f, 7.0f)) *
                            glm::rotate(glm::mat4(1.0f), 0.35f, glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f))) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(1.5f, 0.75f, 2.0f));
    input.previousWorldFromObject = glm::translate(glm::mat4(1.0f), glm::vec3(3.5f, -2.0f, 7.0f));
    input.worldBoundsCenterAndRadius = {4.0f, -1.0f, 7.0f, 3.0f};
    input.geometryBase = 12u;
    input.geometryCount = 4u;
    input.materialBase = 20u;
    input.flags = gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::Enabled) |
                  gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::ShadowCaster) |
                  gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::RayTracingVisible);
    input.stableObjectId = StableObjectId{71u};
    input.rayTracingInstanceId = 19u;
    return input;
}

bool testInstanceNormalization() {
    using namespace renderer::contracts;

    const GpuSceneInstanceNormalizationInput input = validInstanceInput();
    const GpuSceneInstanceNormalizationResult result = normalizeGpuSceneInstance(input);
    const glm::vec3 objectPoint{0.25f, -0.5f, 1.5f};
    const glm::vec3 expectedWorld = glm::vec3(input.worldFromObject * glm::vec4(objectPoint, 1.0f));
    const glm::vec3 packedWorld = transformGpuScenePoint(result.instance.worldFromObject, objectPoint);
    const glm::vec3 roundTrip = transformGpuScenePoint(result.instance.objectFromWorld, packedWorld);
    if (!requireTrue(result.succeeded(), "valid affine instance data must normalize") ||
        !requireTrue(near(packedWorld, expectedWorld), "packed affine rows must preserve point transforms") ||
        !requireTrue(near(roundTrip, objectPoint), "packed inverse rows must reconstruct object-space points") ||
        !requireTrue(result.instance.geometryMaterialAndFlags ==
                         glm::uvec4(input.geometryBase, input.geometryCount, input.materialBase, input.flags),
                     "instance table ranges and flags must preserve source values") ||
        !requireTrue(result.instance.identityAndVersion == glm::uvec4(input.stableObjectId.value,
                                                                      input.rayTracingInstanceId,
                                                                      kGpuSceneContractVersion, 0u),
                     "instance identity must carry the stable ID, RT index, version, and reserved zero")) {
        return false;
    }

    GpuSceneInstanceNormalizationInput invalid = input;
    invalid.worldFromObject[0][3] = 0.25f;
    GpuSceneInstanceNormalizationResult failure = normalizeGpuSceneInstance(invalid);
    if (!requireTrue(failure.error == GpuSceneNormalizationError::NonAffineTransform &&
                         failure.field == GpuSceneField::WorldFromObject,
                     "projective instance transforms must be rejected") ||
        !requireTrue(std::string(gpuSceneNormalizationErrorStableId(failure.error)) == "NonAffineTransform" &&
                         std::string(gpuSceneFieldStableId(failure.field)) == "WorldFromObject",
                     "instance failures must expose stable error and field identifiers")) {
        return false;
    }

    invalid = input;
    invalid.worldFromObject = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 1.0f));
    failure = normalizeGpuSceneInstance(invalid);
    if (!requireTrue(failure.error == GpuSceneNormalizationError::SingularTransform,
                     "singular instance transforms must be rejected before inverse packing")) {
        return false;
    }

    invalid = input;
    invalid.flags &= ~gpuSceneInstanceFlagBit(GpuSceneInstanceFlag::RayTracingVisible);
    failure = normalizeGpuSceneInstance(invalid);
    if (!requireTrue(failure.error == GpuSceneNormalizationError::RayTracingStateConflict,
                     "non-RT instances must carry the explicit invalid custom index")) {
        return false;
    }

    invalid = input;
    invalid.rayTracingInstanceId = kGpuSceneMaxRayTracingInstanceId + 1u;
    failure = normalizeGpuSceneInstance(invalid);
    return requireTrue(failure.error == GpuSceneNormalizationError::InvalidRayTracingInstanceId,
                       "RT instance custom indices must remain within Vulkan's 24-bit field");
}

renderer::contracts::GpuSceneGeometryNormalizationInput validGeometryInput() {
    using namespace renderer::contracts;

    GpuSceneGeometryNormalizationInput input;
    input.vertexAddress = 0x0000001200001000ull;
    input.indexAddress = 0x0000001200002000ull;
    input.primitiveMetadataAddress = 0x0000001200003000ull;
    input.vertexStride = 32u;
    input.positionByteOffset = 0u;
    input.vertexCount = 128u;
    input.firstIndex = 6u;
    input.indexCount = 12u;
    input.indexType = GpuSceneIndexType::Uint32;
    input.materialIndex = 3u;
    input.stableMaterialId = StableMaterialId{101u};
    input.stableGeometryId = StableGeometryId{33u};
    input.primitiveMetadataStride = 16u;
    input.geometryRevision = 9u;
    input.flags = gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::Opaque) |
                  gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::ShadowCaster) |
                  gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::RayTracingVisible);
    input.localBoundsMin = {-1.0f, -2.0f, -3.0f};
    input.localBoundsMax = {4.0f, 5.0f, 6.0f};
    return input;
}

bool testGeometryNormalization() {
    using namespace renderer::contracts;

    const GpuSceneGeometryNormalizationInput input = validGeometryInput();
    const GpuSceneGeometryNormalizationResult result = normalizeGpuSceneGeometry(input);
    if (!requireTrue(result.succeeded(), "valid resident indexed geometry must normalize") ||
        !requireTrue(unpackGpuSceneDeviceAddress(result.geometry.vertexAddress) == input.vertexAddress &&
                         unpackGpuSceneDeviceAddress(result.geometry.indexAddress) == input.indexAddress &&
                         unpackGpuSceneDeviceAddress(result.geometry.primitiveMetadataAddress) ==
                             input.primitiveMetadataAddress,
                     "device addresses must preserve every native bit") ||
        !requireTrue(result.geometry.vertexLayoutAndFlags ==
                         glm::uvec4(input.vertexStride, input.positionByteOffset, input.vertexCount, input.flags),
                     "geometry vertex layout and flags must preserve source values") ||
        !requireTrue(result.geometry.indexRangeAndType == glm::uvec4(input.firstIndex, input.indexCount,
                                                                     static_cast<uint32_t>(input.indexType),
                                                                     kGpuSceneContractVersion),
                     "geometry index range must carry the frozen contract version") ||
        !requireTrue(result.geometry.primitiveMeshletAndRevision == glm::uvec4(4u, 0u, 0u, input.geometryRevision),
                     "triangle count and geometry revision must be derived deterministically")) {
        return false;
    }

    GpuSceneGeometryNormalizationInput invalid = input;
    invalid.flags |= gpuSceneGeometryFlagBit(GpuSceneGeometryFlag::Transparent);
    GpuSceneGeometryNormalizationResult failure = normalizeGpuSceneGeometry(invalid);
    if (!requireTrue(failure.error == GpuSceneNormalizationError::UnknownFlags &&
                         failure.field == GpuSceneField::GeometryFlags,
                     "geometry records must select exactly one surface class")) {
        return false;
    }

    invalid = input;
    invalid.indexCount = 10u;
    failure = normalizeGpuSceneGeometry(invalid);
    if (!requireTrue(failure.error == GpuSceneNormalizationError::InvalidIndexRange,
                     "triangle geometry must contain complete indexed triangles")) {
        return false;
    }

    invalid = input;
    invalid.indexAddress = std::numeric_limits<uint64_t>::max() - 3u;
    failure = normalizeGpuSceneGeometry(invalid);
    if (!requireTrue(failure.error == GpuSceneNormalizationError::ValueOutOfRange &&
                         failure.field == GpuSceneField::IndexRange,
                     "index address ranges must not overflow the native device-address space")) {
        return false;
    }

    invalid = input;
    invalid.meshletAddress = 0x4000u;
    failure = normalizeGpuSceneGeometry(invalid);
    if (!requireTrue(failure.error == GpuSceneNormalizationError::InvalidMeshletRange,
                     "empty meshlet ranges must keep their address and first index zero")) {
        return false;
    }

    invalid = input;
    invalid.meshletAddress = 2u;
    invalid.meshletCount = 1u;
    failure = normalizeGpuSceneGeometry(invalid);
    if (!requireTrue(failure.error == GpuSceneNormalizationError::InvalidDeviceAddress &&
                         failure.field == GpuSceneField::MeshletAddress,
                     "resident meshlet streams must preserve their device-address alignment")) {
        return false;
    }

    invalid = input;
    invalid.localBoundsMin.x = 7.0f;
    failure = normalizeGpuSceneGeometry(invalid);
    return requireTrue(failure.error == GpuSceneNormalizationError::InvalidBounds &&
                           failure.field == GpuSceneField::LocalBounds,
                       "geometry AABB minimum must not exceed its maximum");
}

bool testShaderLayoutMirror() {
    const std::string shaderPath = std::string(MECRAFT_TEST_SOURCE_DIR) + "/assets/shaders/gpu_scene_contract.glsl";
    std::ifstream stream(shaderPath, std::ios::binary);
    if (!requireTrue(stream.is_open(), "GPU scene GLSL contract must be readable")) {
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (!requireTrue(source.find("GPU_SCENE_CONTRACT_VERSION = 1u") != std::string::npos,
                     "GLSL scene contract must mirror the CPU version") ||
        !requireTrue(source.find("GPU_SCENE_MAX_RAY_TRACING_INSTANCE_ID = 0x00ffffffu") != std::string::npos,
                     "GLSL scene contract must mirror the 24-bit RT instance range") ||
        !requireTrue(source.find("GPU_SCENE_GEOMETRY_FLAG_DYNAMIC_VERTICES") != std::string::npos,
                     "GLSL scene flags must mirror dynamic geometry classification")) {
        return false;
    }

    constexpr std::array<const char*, 6u> kInstanceFields{
        {"GpuSceneAffineTransform worldFromObject;", "GpuSceneAffineTransform previousWorldFromObject;",
         "GpuSceneAffineTransform objectFromWorld;", "vec4 worldBoundsCenterAndRadius;",
         "uvec4 geometryMaterialAndFlags;", "uvec4 identityAndVersion;"}};
    size_t offset = source.find("struct GpuSceneInstance");
    for (const char* field : kInstanceFields) {
        const size_t found = source.find(field, offset);
        if (!requireTrue(found != std::string::npos, "GLSL instance layout must mirror every CPU field")) {
            return false;
        }
        offset = found + std::string(field).size();
    }

    constexpr std::array<const char*, 10u> kGeometryFields{
        {"uvec2 vertexAddress;", "uvec2 indexAddress;", "uvec2 primitiveMetadataAddress;", "uvec2 meshletAddress;",
         "uvec4 vertexLayoutAndFlags;", "uvec4 indexRangeAndType;", "uvec4 materialAndIdentity;",
         "uvec4 primitiveMeshletAndRevision;", "vec4 localBoundsMin;", "vec4 localBoundsMax;"}};
    offset = source.find("struct GpuSceneGeometry");
    for (const char* field : kGeometryFields) {
        const size_t found = source.find(field, offset);
        if (!requireTrue(found != std::string::npos, "GLSL geometry layout must mirror every CPU field")) {
            return false;
        }
        offset = found + std::string(field).size();
    }
    return true;
}

} // namespace

int main() {
    if (!testBindlessHandleDomains() || !testBindlessSlotLifecycle() || !testInstanceNormalization() ||
        !testGeometryNormalization() || !testShaderLayoutMirror()) {
        return 1;
    }
    std::cout << "[gpu_scene_contract_test] PASS\n";
    return 0;
}
