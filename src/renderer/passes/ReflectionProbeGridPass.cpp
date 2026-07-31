#include "ReflectionProbeGridPass.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiResources.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace {

[[nodiscard]] uint64_t alignBufferSize(const uint64_t size) {
    constexpr uint64_t kAlignment = 256u;
    if (size > std::numeric_limits<uint64_t>::max() - (kAlignment - 1u)) {
        return 0u;
    }
    return (size + kAlignment - 1u) & ~(kAlignment - 1u);
}

[[nodiscard]] bool multiplyBytes(const uint64_t count,
                                 const uint64_t stride,
                                 uint64_t& bytes) {
    if (stride == 0u || count > std::numeric_limits<uint64_t>::max() / stride) {
        return false;
    }
    bytes = count * stride;
    return true;
}

[[nodiscard]] bool sameHandle(const RhiTextureHandle lhs,
                              const RhiTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameHandle(const RhiTextureViewHandle lhs,
                              const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

} // namespace

void ReflectionProbeGridPass::setSceneProbes(
    std::vector<renderer::contracts::GpuReflectionProbe> probes) {
    m_sceneProbes = std::move(probes);
    ++m_sceneRevision;
    m_prepared = false;
}

void ReflectionProbeGridPass::setPrefilteredCubeArray(
    const RhiTextureHandle texture,
    const RhiTextureViewHandle view) {
    if (sameHandle(m_externalCubeArrayTexture, texture) &&
        sameHandle(m_externalCubeArrayView, view)) {
        return;
    }
    m_externalCubeArrayTexture = texture;
    m_externalCubeArrayView = view;
    ++m_sceneRevision;
    m_prepared = false;
}

bool ReflectionProbeGridPass::prepareGraphFrame(RhiDevice& rhiDevice) {
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        destroyOwnedResources();
        m_externalCubeArrayTexture = {};
        m_externalCubeArrayView = {};
        ++m_sceneRevision;
    }
    m_rhiDevice = &rhiDevice;
    m_lastError.clear();

    if (m_prepared && m_preparedRevision == m_sceneRevision) {
        return true;
    }
    const renderer::contracts::ReflectionProbeGridBuildResult built =
        renderer::contracts::buildReflectionProbeGrid(m_sceneProbes);
    if (!built.succeeded()) {
        m_lastError = renderer::contracts::reflectionProbeGridErrorStableId(
            built.error);
        if (built.error ==
            renderer::contracts::ReflectionProbeGridError::InvalidProbe) {
            m_lastError += ":";
            m_lastError += renderer::contracts::reflectionProbeErrorStableId(
                built.probeError);
            m_lastError += ":";
            m_lastError += renderer::contracts::reflectionProbeFieldStableId(
                built.probeField);
        }
        m_prepared = false;
        return false;
    }
    m_grid = built.grid;
    if (!ensureResources(rhiDevice) || !selectCubeArray(rhiDevice)) {
        m_prepared = false;
        return false;
    }
    m_preparedRevision = m_sceneRevision;
    m_prepared = true;
    m_uploadRequired = true;
    return true;
}

bool ReflectionProbeGridPass::ensureResources(RhiDevice& rhiDevice) {
    uint64_t probeBytes = 0u;
    uint64_t cellBytes = 0u;
    uint64_t indexBytes = 0u;
    if (!multiplyBytes(std::max<size_t>(m_grid.probes.size(), 1u),
                       sizeof(renderer::contracts::GpuReflectionProbe),
                       probeBytes) ||
        !multiplyBytes(std::max<size_t>(m_grid.cells.size(), 1u),
                       sizeof(renderer::contracts::GpuReflectionProbeGridCell),
                       cellBytes) ||
        !multiplyBytes(std::max<size_t>(m_grid.probeIndices.size(), 1u),
                       sizeof(uint32_t), indexBytes)) {
        m_lastError = "BufferSizeOverflow";
        return false;
    }
    return ensureBuffer(rhiDevice, m_probeBuffer, probeBytes,
                        "ReflectionProbeGrid.Probes") &&
           ensureBuffer(rhiDevice, m_metadataBuffer,
                        sizeof(m_grid.metadata),
                        "ReflectionProbeGrid.Metadata") &&
           ensureBuffer(rhiDevice, m_cellBuffer, cellBytes,
                        "ReflectionProbeGrid.Cells") &&
           ensureBuffer(rhiDevice, m_indexBuffer, indexBytes,
                        "ReflectionProbeGrid.Indices") &&
           ensureEmptyCubeArray(rhiDevice);
}

bool ReflectionProbeGridPass::ensureBuffer(
    RhiDevice& rhiDevice,
    BufferResource& resource,
    const uint64_t requiredBytes,
    const char* const debugName) {
    if (resource.handle.isValid() &&
        resource.capacityBytes >= requiredBytes) {
        return true;
    }
    const uint64_t capacity = alignBufferSize(requiredBytes);
    if (capacity == 0u) {
        m_lastError = "BufferSizeOverflow";
        return false;
    }
    RhiBufferDesc desc;
    desc.debugName = debugName;
    desc.size = capacity;
    desc.usage = rhiFlag(RhiBufferUsage::Storage) |
                 rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::StorageBuffer;
    desc.memoryCategory = RhiMemoryCategory::SceneData;
    const RhiBufferHandle created =
        rhiDevice.createBuffer(desc, nullptr, 0u);
    if (!created.isValid()) {
        m_lastError = "BufferCreationFailed";
        return false;
    }
    if (resource.handle.isValid()) {
        rhiDevice.destroyBuffer(resource.handle);
    }
    resource.handle = created;
    resource.capacityBytes = capacity;
    return true;
}

bool ReflectionProbeGridPass::ensureEmptyCubeArray(RhiDevice& rhiDevice) {
    if (m_emptyCubeArrayTexture.isValid() &&
        m_emptyCubeArrayView.isValid()) {
        return true;
    }
    constexpr std::array<uint16_t, 24> kTransparentBlack{};
    RhiTextureDesc desc;
    desc.debugName = "ReflectionProbeGrid.EmptyCubeArray";
    desc.dimension = RhiTextureDimension::CubeArray;
    desc.format = RhiTextureFormat::Rgba16Float;
    desc.width = 1u;
    desc.height = 1u;
    desc.depthOrLayers = 6u;
    desc.mipLevels = 1u;
    desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
                 rhiFlag(RhiTextureUsage::TransferDst);
    desc.memoryCategory = RhiMemoryCategory::SceneData;
    RhiTextureInitialData initialData;
    initialData.pixels = kTransparentBlack.data();
    initialData.sizeBytes = sizeof(kTransparentBlack);
    initialData.layerCount = 6u;
    initialData.finalState = RhiResourceState::ShaderRead;
    const RhiTextureHandle texture =
        rhiDevice.createTexture(desc, &initialData);
    if (!texture.isValid()) {
        m_lastError = "EmptyCubeArrayCreationFailed";
        return false;
    }
    RhiTextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.viewType = RhiTextureViewType::CubeArray;
    viewDesc.format = desc.format;
    viewDesc.layerCount = desc.depthOrLayers;
    const RhiTextureViewHandle view = rhiDevice.createTextureView(viewDesc);
    if (!view.isValid()) {
        rhiDevice.destroyTexture(texture);
        m_lastError = "EmptyCubeArrayViewCreationFailed";
        return false;
    }
    m_emptyCubeArrayTexture = texture;
    m_emptyCubeArrayView = view;
    return true;
}

bool ReflectionProbeGridPass::selectCubeArray(RhiDevice& rhiDevice) {
    if (m_grid.probes.empty()) {
        m_consumerCubeArrayTexture = m_emptyCubeArrayTexture;
        m_consumerCubeArrayView = m_emptyCubeArrayView;
        return true;
    }
    if (!m_externalCubeArrayTexture.isValid() ||
        !m_externalCubeArrayView.isValid()) {
        m_lastError = "CapturedCubeArrayMissing";
        return false;
    }
    RhiTextureDesc desc;
    if (!rhiDevice.getTextureDesc(m_externalCubeArrayTexture, desc) ||
        desc.dimension != RhiTextureDimension::CubeArray ||
        desc.format != RhiTextureFormat::Rgba16Float ||
        desc.width != renderer::contracts::kReflectionProbeCubeExtent ||
        desc.height != renderer::contracts::kReflectionProbeCubeExtent ||
        desc.mipLevels !=
            renderer::contracts::kReflectionProbeCubeMipCount ||
        desc.depthOrLayers % 6u != 0u ||
        (desc.usage & rhiFlag(RhiTextureUsage::Sampled)) == 0u ||
        (desc.usage & rhiFlag(RhiTextureUsage::ColorAttachment)) == 0u) {
        m_lastError = "CapturedCubeArrayContractMismatch";
        return false;
    }
    const uint32_t capacity = desc.depthOrLayers / 6u;
    for (const renderer::contracts::GpuReflectionProbe& probe :
         m_grid.probes) {
        if (probe.resourcesAndIdentity.x >= capacity) {
            m_lastError = "CapturedCubeArrayIndexOutOfRange";
            return false;
        }
    }
    m_consumerCubeArrayTexture = m_externalCubeArrayTexture;
    m_consumerCubeArrayView = m_externalCubeArrayView;
    return true;
}

bool ReflectionProbeGridPass::importGraphResources(
    RenderGraph& graph,
    GraphResources& resources) const {
    if (!m_prepared || m_rhiDevice == nullptr ||
        !m_consumerCubeArrayTexture.isValid() ||
        !m_consumerCubeArrayView.isValid() ||
        !importBuffer(graph, m_probeBuffer, resources.probes) ||
        !importBuffer(graph, m_metadataBuffer, resources.metadata) ||
        !importBuffer(graph, m_cellBuffer, resources.cells) ||
        !importBuffer(graph, m_indexBuffer, resources.indices)) {
        return false;
    }
    RhiTextureDesc desc;
    if (!m_rhiDevice->getTextureDesc(m_consumerCubeArrayTexture, desc)) {
        return false;
    }
    resources.prefilteredCubeArray = graph.importTexture({
        "ReflectionProbeGrid.PrefilteredCubeArray",
        m_consumerCubeArrayTexture,
        desc,
        RhiResourceState::ShaderRead,
        RhiResourceState::ShaderRead,
        m_consumerCubeArrayView});
    return resources.prefilteredCubeArray.isValid();
}

bool ReflectionProbeGridPass::importBuffer(
    RenderGraph& graph,
    const BufferResource& resource,
    RgBufferHandle& graphBuffer) const {
    if (m_rhiDevice == nullptr || !resource.handle.isValid()) {
        return false;
    }
    RhiBufferDesc desc;
    if (!m_rhiDevice->getBufferDesc(resource.handle, desc)) {
        return false;
    }
    RgImportedBufferDesc imported;
    imported.name = desc.debugName;
    imported.buffer = resource.handle;
    imported.desc = desc;
    imported.initialState = RhiResourceState::StorageBuffer;
    imported.finalState = RhiResourceState::StorageBuffer;
    graphBuffer = graph.importBuffer(imported);
    return graphBuffer.isValid();
}

RgPassHandle ReflectionProbeGridPass::addGraphPasses(
    RenderGraph& graph,
    const GraphResources& resources,
    const RgPassHandle dependency) {
    if (!m_prepared || !dependency.isValid() ||
        !resources.probes.isValid() || !resources.metadata.isValid() ||
        !resources.cells.isValid() || !resources.indices.isValid() ||
        !resources.prefilteredCubeArray.isValid()) {
        return {};
    }
    m_uploadScheduled = false;
    if (!m_uploadRequired) {
        return dependency;
    }
    RenderGraphPassBuilder upload = graph.addPass(
        {"ReflectionProbeGrid.Upload", RgPassType::Copy,
         RhiQueueType::Graphics});
    upload.dependsOn(dependency)
        .writeBuffer(resources.probes, RhiResourceState::TransferDst)
        .writeBuffer(resources.metadata, RhiResourceState::TransferDst)
        .writeBuffer(resources.cells, RhiResourceState::TransferDst)
        .writeBuffer(resources.indices, RhiResourceState::TransferDst)
        .setExecute([this](RgPassContext& pass) {
            return recordUpload(pass.commandList());
        });
    m_uploadScheduled = true;
    return upload.handle();
}

bool ReflectionProbeGridPass::recordUpload(
    RhiCommandList& commandList) const {
    const renderer::contracts::GpuReflectionProbe emptyProbe;
    const renderer::contracts::GpuReflectionProbeGridCell emptyCell;
    constexpr uint32_t emptyIndex = 0u;
    const void* probeData = m_grid.probes.empty()
        ? static_cast<const void*>(&emptyProbe)
        : static_cast<const void*>(m_grid.probes.data());
    const size_t probeBytes = std::max<size_t>(m_grid.probes.size(), 1u) *
        sizeof(renderer::contracts::GpuReflectionProbe);
    const void* cellData = m_grid.cells.empty()
        ? static_cast<const void*>(&emptyCell)
        : static_cast<const void*>(m_grid.cells.data());
    const size_t cellBytes = std::max<size_t>(m_grid.cells.size(), 1u) *
        sizeof(renderer::contracts::GpuReflectionProbeGridCell);
    const void* indexData = m_grid.probeIndices.empty()
        ? static_cast<const void*>(&emptyIndex)
        : static_cast<const void*>(m_grid.probeIndices.data());
    const size_t indexBytes =
        std::max<size_t>(m_grid.probeIndices.size(), 1u) * sizeof(uint32_t);
    commandList.updateBuffer(m_probeBuffer.handle, 0u,
                             probeData, probeBytes);
    commandList.updateBuffer(m_metadataBuffer.handle, 0u,
                             &m_grid.metadata, sizeof(m_grid.metadata));
    commandList.updateBuffer(m_cellBuffer.handle, 0u,
                             cellData, cellBytes);
    commandList.updateBuffer(m_indexBuffer.handle, 0u,
                             indexData, indexBytes);
    return true;
}

void ReflectionProbeGridPass::finishGraphExecution(const bool succeeded) {
    if (m_uploadScheduled && succeeded) {
        m_uploadRequired = false;
    }
    m_uploadScheduled = false;
}

ReflectionProbeGridPass::ConsumerResources
ReflectionProbeGridPass::consumerResources() const {
    ConsumerResources resources;
    if (!m_prepared) {
        return resources;
    }
    resources.probeBuffer = m_probeBuffer.handle;
    resources.probeBufferBytes = m_probeBuffer.capacityBytes;
    resources.metadataBuffer = m_metadataBuffer.handle;
    resources.metadataBufferBytes = m_metadataBuffer.capacityBytes;
    resources.cellBuffer = m_cellBuffer.handle;
    resources.cellBufferBytes = m_cellBuffer.capacityBytes;
    resources.indexBuffer = m_indexBuffer.handle;
    resources.indexBufferBytes = m_indexBuffer.capacityBytes;
    resources.prefilteredCubeArrayView = m_consumerCubeArrayView;
    resources.activeProbeCount =
        static_cast<uint32_t>(m_grid.probes.size());
    return resources;
}

void ReflectionProbeGridPass::destroyOwnedResources() {
    if (m_rhiDevice != nullptr) {
        BufferResource* buffers[] = {
            &m_probeBuffer, &m_metadataBuffer, &m_cellBuffer, &m_indexBuffer};
        for (BufferResource* buffer : buffers) {
            if (buffer->handle.isValid()) {
                m_rhiDevice->destroyBuffer(buffer->handle);
            }
            *buffer = {};
        }
        if (m_emptyCubeArrayView.isValid()) {
            m_rhiDevice->destroyTextureView(m_emptyCubeArrayView);
        }
        if (m_emptyCubeArrayTexture.isValid()) {
            m_rhiDevice->destroyTexture(m_emptyCubeArrayTexture);
        }
    }
    m_emptyCubeArrayTexture = {};
    m_emptyCubeArrayView = {};
    m_consumerCubeArrayTexture = {};
    m_consumerCubeArrayView = {};
    m_prepared = false;
    m_uploadRequired = true;
    m_uploadScheduled = false;
}

void ReflectionProbeGridPass::shutdown() {
    destroyOwnedResources();
    m_rhiDevice = nullptr;
    m_externalCubeArrayTexture = {};
    m_externalCubeArrayView = {};
    m_sceneProbes.clear();
    m_grid = {};
    m_preparedRevision = 0u;
    m_lastError.clear();
}
