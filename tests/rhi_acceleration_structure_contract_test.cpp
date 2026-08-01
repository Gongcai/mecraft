#include "renderer/rhi/RhiDescriptor.h"
#include "renderer/rhi/RhiResources.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {
[[nodiscard]] bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}
} // namespace

int main() {
    const RhiAccelerationStructureHandle handle{7u, 3u};
    RhiBindingResource descriptorResource;
    descriptorResource.accelerationStructure = handle;

    const auto packedCustomIndex = rhiPackAccelerationStructureInstanceCustomIndexAndMask(0x00543210u, 0xa5u);
    const auto rejectedCustomIndex = rhiPackAccelerationStructureInstanceCustomIndexAndMask(0x01000000u, 0xffu);
    const RhiAccelerationStructureInstanceFlags instanceFlags =
        rhiFlag(RhiAccelerationStructureInstanceFlag::TriangleFacingCullDisable) |
        rhiFlag(RhiAccelerationStructureInstanceFlag::ForceOpaque);
    const auto packedShaderRecord =
        rhiPackAccelerationStructureInstanceShaderBindingTableOffsetAndFlags(0x00123456u, instanceFlags);
    const auto rejectedShaderRecord =
        rhiPackAccelerationStructureInstanceShaderBindingTableOffsetAndFlags(0x01000000u, instanceFlags);
    const auto rejectedUnknownInstanceFlag =
        rhiPackAccelerationStructureInstanceShaderBindingTableOffsetAndFlags(0u, 1u << 4u);

    RhiAccelerationStructureInstance instance;
    if (packedCustomIndex.has_value()) {
        instance.customIndexAndMask = *packedCustomIndex;
    }
    if (packedShaderRecord.has_value()) {
        instance.shaderBindingTableOffsetAndFlags = *packedShaderRecord;
    }

    const RhiBufferUsageFlags accelerationStructureBufferUsages =
        rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureStorage) |
        rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
    const RhiAccelerationStructureBuildFlags buildFlags = rhiFlag(RhiAccelerationStructureBuildFlag::AllowUpdate) |
                                                          rhiFlag(RhiAccelerationStructureBuildFlag::AllowCompaction) |
                                                          rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace);

    return requireTrue(sizeof(RhiAccelerationStructureInstance) == 64u,
                       "top-level instance layout must remain exactly 64 bytes") &&
                   requireTrue(alignof(RhiAccelerationStructureInstance) == alignof(uint64_t),
                               "top-level instance layout must preserve 64-bit address alignment") &&
                   requireTrue(handle.isValid() && descriptorResource.accelerationStructure.index == 7u &&
                                   descriptorResource.accelerationStructure.generation == 3u,
                               "acceleration-structure descriptors must preserve typed handle identity") &&
                   requireTrue(packedCustomIndex.has_value() && *packedCustomIndex == 0xa5543210u &&
                                   !rejectedCustomIndex.has_value(),
                               "custom-index packing must preserve its fixed 24-bit field") &&
                   requireTrue(packedShaderRecord.has_value() && *packedShaderRecord == 0x05123456u &&
                                   !rejectedShaderRecord.has_value() && !rejectedUnknownInstanceFlag.has_value(),
                               "shader-record packing must preserve its fixed fields and reject unknown flags") &&
                   requireTrue(instance.transform[0] == 1.0f && instance.transform[5] == 1.0f &&
                                   instance.transform[10] == 1.0f && instance.accelerationStructureReference == 0u,
                               "default top-level instances must contain an identity transform") &&
                   requireTrue((accelerationStructureBufferUsages & rhiFlag(RhiBufferUsage::DeviceAddress)) != 0u &&
                                   (accelerationStructureBufferUsages &
                                    rhiFlag(RhiBufferUsage::AccelerationStructureStorage)) != 0u &&
                                   (accelerationStructureBufferUsages &
                                    rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput)) != 0u,
                               "acceleration-structure buffer usages must remain independently composable") &&
                   requireTrue((buildFlags & rhiFlag(RhiAccelerationStructureBuildFlag::AllowUpdate)) != 0u &&
                                   (buildFlags & rhiFlag(RhiAccelerationStructureBuildFlag::AllowCompaction)) != 0u &&
                                   (buildFlags & rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace)) != 0u,
                               "acceleration-structure build flags must remain independently composable")
               ? 0
               : 1;
}
