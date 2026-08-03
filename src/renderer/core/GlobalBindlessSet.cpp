#include "renderer/core/GlobalBindlessSet.h"

#include "renderer/rhi/RhiDevice.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <vector>

namespace renderer::core {
namespace {

std::atomic<uint64_t> g_nextGlobalBindlessIdentity{1u};

constexpr RhiBindingFlags kGlobalBindingFlags = rhiFlag(RhiBindingFlag::PartiallyBound) |
                                                rhiFlag(RhiBindingFlag::UpdateAfterBind) |
                                                rhiFlag(RhiBindingFlag::UpdateUnusedWhilePending);
constexpr RhiBindingFlags kGlobalAccelerationStructureBindingFlags =
    rhiFlag(RhiBindingFlag::PartiallyBound) | rhiFlag(RhiBindingFlag::UpdateAfterBind) |
    rhiFlag(RhiBindingFlag::UpdateUnusedWhilePending);
constexpr RhiShaderStageFlags kGlobalShaderStages =
    rhiFlag(RhiShaderStage::Vertex) | rhiFlag(RhiShaderStage::Fragment) | rhiFlag(RhiShaderStage::Compute);

[[nodiscard]] bool checkedAdd(const uint32_t lhs, const uint32_t rhs, uint32_t& result) {
    if (rhs > std::numeric_limits<uint32_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

[[nodiscard]] bool requiredCapabilitiesPresent(const RhiCapabilities& capabilities) {
    return capabilities.descriptorIndexing && capabilities.descriptorBindingPartiallyBound &&
           capabilities.descriptorBindingVariableDescriptorCount &&
           capabilities.descriptorBindingUpdateUnusedWhilePending &&
           capabilities.descriptorBindingSampledImageUpdateAfterBind &&
           capabilities.descriptorBindingStorageBufferUpdateAfterBind &&
           capabilities.descriptorBindingAccelerationStructureUpdateAfterBind && capabilities.accelerationStructure &&
           capabilities.runtimeDescriptorArray && capabilities.shaderSampledImageArrayNonUniformIndexing &&
           capabilities.shaderStorageBufferArrayNonUniformIndexing;
}

[[nodiscard]] bool capacitiesFitLimits(const GlobalBindlessSetConfig& config, const RhiCapabilities& capabilities) {
    uint32_t sampledImageCount = 0u;
    uint32_t sampledAndSamplerCount = 0u;
    uint32_t arrayResourceCount = 0u;
    uint32_t totalResourceCount = 0u;
    return checkedAdd(config.sampledTexture2DCapacity, config.sampledTextureCubeCapacity, sampledImageCount) &&
           checkedAdd(sampledImageCount, config.samplerCapacity, sampledAndSamplerCount) &&
           checkedAdd(sampledAndSamplerCount, config.storageBufferCapacity, arrayResourceCount) &&
           checkedAdd(arrayResourceCount, 1u, totalResourceCount) &&
           sampledImageCount <= capabilities.maxDescriptorSetUpdateAfterBindSampledImages &&
           sampledImageCount <= capabilities.maxPerStageDescriptorUpdateAfterBindSampledImages &&
           config.samplerCapacity <= capabilities.maxDescriptorSetUpdateAfterBindSamplers &&
           config.samplerCapacity <= capabilities.maxPerStageDescriptorUpdateAfterBindSamplers &&
           config.storageBufferCapacity <= capabilities.maxDescriptorSetUpdateAfterBindStorageBuffers &&
           config.storageBufferCapacity <= capabilities.maxPerStageDescriptorUpdateAfterBindStorageBuffers &&
           capabilities.maxDescriptorSetUpdateAfterBindAccelerationStructures >= 1u &&
           capabilities.maxPerStageDescriptorUpdateAfterBindAccelerationStructures >= 1u &&
           totalResourceCount <= capabilities.maxPerStageUpdateAfterBindResources;
}

[[nodiscard]] GlobalBindlessSetError mapSlotError(const BindlessDescriptorSlotError error) {
    switch (error) {
    case BindlessDescriptorSlotError::None: return GlobalBindlessSetError::None;
    case BindlessDescriptorSlotError::CapacityExceeded: return GlobalBindlessSetError::CapacityExceeded;
    case BindlessDescriptorSlotError::PublicationRejected: return GlobalBindlessSetError::DescriptorPublicationFailed;
    case BindlessDescriptorSlotError::InvalidHandle: return GlobalBindlessSetError::InvalidHandle;
    case BindlessDescriptorSlotError::StaleGeneration: return GlobalBindlessSetError::StaleGeneration;
    case BindlessDescriptorSlotError::SlotNotLive: return GlobalBindlessSetError::SlotNotLive;
    }
    std::abort();
}

template <typename Tag>
[[nodiscard]] GlobalBindlessPublicationResult<Tag>
publicationResult(const BindlessDescriptorSlotAllocationResult<Tag>& allocation) {
    return {allocation.handle, mapSlotError(allocation.error)};
}

} // namespace

GlobalBindlessSet::~GlobalBindlessSet() {
    shutdown();
}

GlobalBindlessSetError GlobalBindlessSet::initialize(RhiDevice& rhiDevice, const GlobalBindlessSetConfig& config) {
    if (initialized()) {
        return GlobalBindlessSetError::AlreadyInitialized;
    }
    if (rhiDevice.backend() != RhiBackend::Vulkan) {
        return GlobalBindlessSetError::BackendUnsupported;
    }
    const RhiCapabilities& capabilities = rhiDevice.capabilities();
    if (!requiredCapabilitiesPresent(capabilities)) {
        return GlobalBindlessSetError::CapabilityMissing;
    }
    if (config.sampledTexture2DCapacity == 0u || config.sampledTextureCubeCapacity == 0u ||
        config.samplerCapacity == 0u || config.storageBufferCapacity == 0u) {
        return GlobalBindlessSetError::InvalidCapacity;
    }
    if (!capacitiesFitLimits(config, capabilities)) {
        return GlobalBindlessSetError::DescriptorLimitExceeded;
    }

    // Vulkan permits a variable descriptor count only on the highest binding. Fixed counts preserve
    // binding 4 for the TLAS while all four resource arrays remain runtime-indexed and partially bound.
    RhiBindGroupLayoutDesc layoutDesc;
    layoutDesc.debugName = "GlobalBindlessSet.Layout";
    layoutDesc.entries = {
        {static_cast<uint32_t>(renderer::contracts::GlobalBindlessBinding::SampledTexture2D),
         RhiBindingType::SampledTexture, kGlobalShaderStages, config.sampledTexture2DCapacity, kGlobalBindingFlags},
        {static_cast<uint32_t>(renderer::contracts::GlobalBindlessBinding::SampledTextureCube),
         RhiBindingType::SampledTexture, kGlobalShaderStages, config.sampledTextureCubeCapacity, kGlobalBindingFlags},
        {static_cast<uint32_t>(renderer::contracts::GlobalBindlessBinding::Sampler), RhiBindingType::Sampler,
         kGlobalShaderStages, config.samplerCapacity, kGlobalBindingFlags},
        {static_cast<uint32_t>(renderer::contracts::GlobalBindlessBinding::StorageBuffer),
         RhiBindingType::StorageBuffer, kGlobalShaderStages, config.storageBufferCapacity, kGlobalBindingFlags},
        {static_cast<uint32_t>(renderer::contracts::GlobalBindlessBinding::AccelerationStructure),
         RhiBindingType::AccelerationStructure, kGlobalShaderStages, 1u, kGlobalAccelerationStructureBindingFlags}};
    const RhiBindGroupLayoutHandle layout = rhiDevice.createBindGroupLayout(layoutDesc);
    if (!layout.isValid()) {
        return GlobalBindlessSetError::LayoutCreationFailed;
    }

    RhiBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = layout;
    const RhiBindGroupHandle bindGroup = rhiDevice.createBindGroup(bindGroupDesc);
    if (!bindGroup.isValid()) {
        rhiDevice.destroyBindGroupLayout(layout);
        return GlobalBindlessSetError::BindGroupCreationFailed;
    }

    m_device = &rhiDevice;
    m_layout = layout;
    m_bindGroup = bindGroup;
    m_sampledTexture2DSlots =
        BindlessDescriptorSlotAllocator<renderer::contracts::BindlessTexture2DTag>(config.sampledTexture2DCapacity);
    m_sampledTextureCubeSlots =
        BindlessDescriptorSlotAllocator<renderer::contracts::BindlessTextureCubeTag>(config.sampledTextureCubeCapacity);
    m_samplerSlots = BindlessDescriptorSlotAllocator<renderer::contracts::BindlessSamplerTag>(config.samplerCapacity);
    m_storageBufferSlots =
        BindlessDescriptorSlotAllocator<renderer::contracts::BindlessStorageBufferTag>(config.storageBufferCapacity);
    m_identity = g_nextGlobalBindlessIdentity.fetch_add(1u, std::memory_order_relaxed);
    if (m_identity == 0u) {
        shutdown();
        return GlobalBindlessSetError::InvalidResource;
    }
    return GlobalBindlessSetError::None;
}

void GlobalBindlessSet::shutdown() {
    if (m_device != nullptr) {
        if (m_bindGroup.isValid()) {
            m_device->destroyBindGroup(m_bindGroup);
        }
        if (m_layout.isValid()) {
            m_device->destroyBindGroupLayout(m_layout);
        }
    }
    m_retainedLifetimes.clear();
    m_device = nullptr;
    m_layout = {};
    m_bindGroup = {};
    m_sampledTexture2DSlots = BindlessDescriptorSlotAllocator<renderer::contracts::BindlessTexture2DTag>(0u);
    m_sampledTextureCubeSlots = BindlessDescriptorSlotAllocator<renderer::contracts::BindlessTextureCubeTag>(0u);
    m_samplerSlots = BindlessDescriptorSlotAllocator<renderer::contracts::BindlessSamplerTag>(0u);
    m_storageBufferSlots = BindlessDescriptorSlotAllocator<renderer::contracts::BindlessStorageBufferTag>(0u);
    m_accelerationStructure = {};
    m_identity = 0u;
    m_accelerationStructureUpdateCount = 0u;
}

GlobalBindlessSetError GlobalBindlessSet::retainLifetime(std::shared_ptr<const GlobalBindlessLifetime> lifetime) {
    if (!initialized()) {
        return GlobalBindlessSetError::NotInitialized;
    }
    if (lifetime == nullptr) {
        return GlobalBindlessSetError::InvalidResource;
    }
    m_retainedLifetimes.push_back(std::move(lifetime));
    return GlobalBindlessSetError::None;
}

GlobalBindlessPublicationResult<renderer::contracts::BindlessTexture2DTag>
GlobalBindlessSet::publishTexture2D(const RhiTextureViewHandle textureView) {
    if (!initialized()) {
        return {{}, GlobalBindlessSetError::NotInitialized};
    }
    if (!validTextureView(textureView, RhiTextureViewType::Texture2D)) {
        return {{}, GlobalBindlessSetError::InvalidResource};
    }
    RhiBindingResource resource;
    resource.textureView = textureView;
    return publicationResult(m_sampledTexture2DSlots.allocateAndPublish([&](const auto handle) {
        return updateResource(renderer::contracts::GlobalBindlessBinding::SampledTexture2D, handle.index, resource);
    }));
}

GlobalBindlessPublicationResult<renderer::contracts::BindlessTextureCubeTag>
GlobalBindlessSet::publishTextureCube(const RhiTextureViewHandle textureView) {
    if (!initialized()) {
        return {{}, GlobalBindlessSetError::NotInitialized};
    }
    if (!validTextureView(textureView, RhiTextureViewType::Cube)) {
        return {{}, GlobalBindlessSetError::InvalidResource};
    }
    RhiBindingResource resource;
    resource.textureView = textureView;
    return publicationResult(m_sampledTextureCubeSlots.allocateAndPublish([&](const auto handle) {
        return updateResource(renderer::contracts::GlobalBindlessBinding::SampledTextureCube, handle.index, resource);
    }));
}

GlobalBindlessPublicationResult<renderer::contracts::BindlessSamplerTag>
GlobalBindlessSet::publishSampler(const RhiSamplerHandle sampler) {
    if (!initialized()) {
        return {{}, GlobalBindlessSetError::NotInitialized};
    }
    RhiSamplerDesc samplerDesc;
    if (!m_device->getSamplerDesc(sampler, samplerDesc)) {
        return {{}, GlobalBindlessSetError::InvalidResource};
    }
    RhiBindingResource resource;
    resource.sampler = sampler;
    return publicationResult(m_samplerSlots.allocateAndPublish([&](const auto handle) {
        return updateResource(renderer::contracts::GlobalBindlessBinding::Sampler, handle.index, resource);
    }));
}

GlobalBindlessPublicationResult<renderer::contracts::BindlessStorageBufferTag>
GlobalBindlessSet::publishStorageBuffer(const RhiBufferHandle buffer) {
    if (!initialized()) {
        return {{}, GlobalBindlessSetError::NotInitialized};
    }
    RhiBufferDesc bufferDesc;
    if (!m_device->getBufferDesc(buffer, bufferDesc) || bufferDesc.size == 0u ||
        (bufferDesc.usage & rhiFlag(RhiBufferUsage::Storage)) == 0u) {
        return {{}, GlobalBindlessSetError::InvalidResource};
    }
    RhiBindingResource resource;
    resource.buffer = {buffer, 0u, bufferDesc.size};
    return publicationResult(m_storageBufferSlots.allocateAndPublish([&](const auto handle) {
        return updateResource(renderer::contracts::GlobalBindlessBinding::StorageBuffer, handle.index, resource);
    }));
}

GlobalBindlessSetError
GlobalBindlessSet::setAccelerationStructure(const RhiAccelerationStructureHandle accelerationStructure) {
    if (!initialized()) {
        return GlobalBindlessSetError::NotInitialized;
    }
    RhiAccelerationStructureDesc desc;
    if (!m_device->getAccelerationStructureDesc(accelerationStructure, desc) ||
        desc.type != RhiAccelerationStructureType::TopLevel) {
        return GlobalBindlessSetError::InvalidResource;
    }
    if (m_accelerationStructure.index == accelerationStructure.index &&
        m_accelerationStructure.generation == accelerationStructure.generation) {
        return GlobalBindlessSetError::None;
    }
    RhiBindingResource resource;
    resource.accelerationStructure = accelerationStructure;
    if (!updateResource(renderer::contracts::GlobalBindlessBinding::AccelerationStructure, 0u, resource)) {
        return GlobalBindlessSetError::DescriptorPublicationFailed;
    }
    m_accelerationStructure = accelerationStructure;
    ++m_accelerationStructureUpdateCount;
    return GlobalBindlessSetError::None;
}

GlobalBindlessSetError GlobalBindlessSet::retire(const renderer::contracts::BindlessTexture2DHandle handle,
                                                 const uint64_t lastUseSequence) {
    return initialized() ? mapSlotError(m_sampledTexture2DSlots.retire(handle, lastUseSequence))
                         : GlobalBindlessSetError::NotInitialized;
}

GlobalBindlessSetError GlobalBindlessSet::retire(const renderer::contracts::BindlessTextureCubeHandle handle,
                                                 const uint64_t lastUseSequence) {
    return initialized() ? mapSlotError(m_sampledTextureCubeSlots.retire(handle, lastUseSequence))
                         : GlobalBindlessSetError::NotInitialized;
}

GlobalBindlessSetError GlobalBindlessSet::retire(const renderer::contracts::BindlessSamplerHandle handle,
                                                 const uint64_t lastUseSequence) {
    return initialized() ? mapSlotError(m_samplerSlots.retire(handle, lastUseSequence))
                         : GlobalBindlessSetError::NotInitialized;
}

GlobalBindlessSetError GlobalBindlessSet::retire(const renderer::contracts::BindlessStorageBufferHandle handle,
                                                 const uint64_t lastUseSequence) {
    return initialized() ? mapSlotError(m_storageBufferSlots.retire(handle, lastUseSequence))
                         : GlobalBindlessSetError::NotInitialized;
}

GlobalBindlessSetError
GlobalBindlessSet::retireStorageBuffers(const renderer::contracts::BindlessStorageBufferHandle* handles,
                                        const uint32_t handleCount, const uint64_t lastUseSequence) {
    if (!initialized()) {
        return GlobalBindlessSetError::NotInitialized;
    }
    if (handles == nullptr || handleCount == 0u) {
        return GlobalBindlessSetError::InvalidHandle;
    }
    std::vector<renderer::contracts::BindlessStorageBufferHandle> sortedHandles(handles, handles + handleCount);
    std::sort(sortedHandles.begin(), sortedHandles.end(), [](const auto lhs, const auto rhs) {
        return lhs.index != rhs.index ? lhs.index < rhs.index : lhs.generation < rhs.generation;
    });
    for (size_t index = 0u; index < sortedHandles.size(); ++index) {
        const BindlessDescriptorSlotError validation = m_storageBufferSlots.validateLive(sortedHandles[index]);
        if (validation != BindlessDescriptorSlotError::None) {
            return mapSlotError(validation);
        }
        if (index != 0u && sortedHandles[index - 1u].index == sortedHandles[index].index) {
            return GlobalBindlessSetError::InvalidHandle;
        }
    }
    for (const auto handle : sortedHandles) {
        if (m_storageBufferSlots.retire(handle, lastUseSequence) != BindlessDescriptorSlotError::None) {
            std::abort();
        }
    }
    return GlobalBindlessSetError::None;
}

GlobalBindlessReclaimResult GlobalBindlessSet::reclaim(const uint64_t completedSequence) {
    if (!initialized()) {
        return {};
    }
    return {m_sampledTexture2DSlots.reclaim(completedSequence), m_sampledTextureCubeSlots.reclaim(completedSequence),
            m_samplerSlots.reclaim(completedSequence), m_storageBufferSlots.reclaim(completedSequence)};
}

GlobalBindlessSetStats GlobalBindlessSet::stats() const {
    GlobalBindlessSetStats result;
    result.sampledTexture2D = m_sampledTexture2DSlots.stats();
    result.sampledTextureCube = m_sampledTextureCubeSlots.stats();
    result.samplers = m_samplerSlots.stats();
    result.storageBuffers = m_storageBufferSlots.stats();
    result.accelerationStructureUpdateCount = m_accelerationStructureUpdateCount;
    return result;
}

bool GlobalBindlessSet::updateResource(const renderer::contracts::GlobalBindlessBinding binding, const uint32_t index,
                                       const RhiBindingResource& resource) {
    const RhiBindGroupUpdate update{m_bindGroup, static_cast<uint32_t>(binding), index, &resource, 1u};
    return m_device->updateBindGroups(&update, 1u);
}

bool GlobalBindlessSet::validTextureView(const RhiTextureViewHandle textureView,
                                         const RhiTextureViewType requiredType) const {
    RhiTextureViewDesc viewDesc;
    RhiTextureDesc textureDesc;
    return m_device->getTextureViewDesc(textureView, viewDesc) && viewDesc.viewType == requiredType &&
           m_device->getTextureDesc(viewDesc.texture, textureDesc) &&
           (textureDesc.usage & rhiFlag(RhiTextureUsage::Sampled)) != 0u;
}

const char* globalBindlessSetErrorStableId(const GlobalBindlessSetError error) {
    switch (error) {
    case GlobalBindlessSetError::None: return "None";
    case GlobalBindlessSetError::AlreadyInitialized: return "AlreadyInitialized";
    case GlobalBindlessSetError::NotInitialized: return "NotInitialized";
    case GlobalBindlessSetError::BackendUnsupported: return "BackendUnsupported";
    case GlobalBindlessSetError::CapabilityMissing: return "BindlessDescriptorCapabilityMissing";
    case GlobalBindlessSetError::InvalidCapacity: return "InvalidCapacity";
    case GlobalBindlessSetError::DescriptorLimitExceeded: return "BindlessDescriptorLimitExceeded";
    case GlobalBindlessSetError::LayoutCreationFailed: return "BindlessLayoutCreationFailed";
    case GlobalBindlessSetError::BindGroupCreationFailed: return "BindlessSetCreationFailed";
    case GlobalBindlessSetError::InvalidResource: return "InvalidResource";
    case GlobalBindlessSetError::DescriptorPublicationFailed: return "BindlessDescriptorPublicationFailed";
    case GlobalBindlessSetError::CapacityExceeded: return "BindlessDescriptorCapacityExceeded";
    case GlobalBindlessSetError::InvalidHandle: return "InvalidHandle";
    case GlobalBindlessSetError::StaleGeneration: return "StaleGeneration";
    case GlobalBindlessSetError::SlotNotLive: return "SlotNotLive";
    }
    std::abort();
}

} // namespace renderer::core
