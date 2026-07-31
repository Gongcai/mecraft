#include "renderer/rhi/RhiRenderGraph.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiCommandListPool.h"
#include "renderer/rhi/RhiDevice.h"
#include "thread/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <type_traits>
#include <utility>

namespace {
[[nodiscard]] bool includesRead(const RgAccessType access) {
    return access == RgAccessType::Read || access == RgAccessType::ReadWrite;
}

[[nodiscard]] bool includesWrite(const RgAccessType access) {
    return access == RgAccessType::Write || access == RgAccessType::ReadWrite;
}

[[nodiscard]] bool textureQueuesShareConcurrently(const RhiTextureDesc& desc, const RhiQueueType lhs,
                                                  const RhiQueueType rhs) {
    if (desc.queueSharing != RhiTextureQueueSharing::GraphicsComputeConcurrent) {
        return false;
    }
    const auto isSharedQueue = [](const RhiQueueType queue) {
        return queue == RhiQueueType::Graphics || queue == RhiQueueType::Compute;
    };
    return isSharedQueue(lhs) && isSharedQueue(rhs);
}

[[nodiscard]] bool textureStateAcceptsAccess(const RhiResourceState state, const RgAccessType access) {
    switch (state) {
    case RhiResourceState::ShaderRead:
    case RhiResourceState::DepthRead:
    case RhiResourceState::TransferSrc: return access == RgAccessType::Read;
    case RhiResourceState::RenderTarget:
    case RhiResourceState::DepthWrite:
    case RhiResourceState::TransferDst: return access == RgAccessType::Write || access == RgAccessType::ReadWrite;
    case RhiResourceState::ShaderWrite: return access == RgAccessType::Write || access == RgAccessType::ReadWrite;
    default: return false;
    }
}

[[nodiscard]] bool bufferStateAcceptsAccess(const RhiResourceState state, const RgAccessType access) {
    switch (state) {
    case RhiResourceState::TransferSrc:
    case RhiResourceState::VertexBuffer:
    case RhiResourceState::IndexBuffer:
    case RhiResourceState::IndirectArgument:
    case RhiResourceState::UniformBuffer:
    case RhiResourceState::HostRead: return access == RgAccessType::Read;
    case RhiResourceState::TransferDst:
    case RhiResourceState::HostWrite: return access == RgAccessType::Write;
    case RhiResourceState::StorageBuffer: return true;
    default: return false;
    }
}

[[nodiscard]] uint32_t resolvedCount(const uint32_t base, const uint32_t requested, const uint32_t total) {
    if (base >= total)
        return 0u;
    if (requested == kRhiRemainingMipLevels || requested == kRhiRemainingArrayLayers) {
        return total - base;
    }
    return requested <= total - base ? requested : 0u;
}

[[nodiscard]] uint32_t textureSubresourceLayerCount(const RhiTextureDesc& desc) {
    return desc.dimension == RhiTextureDimension::Texture3D ? 1u : desc.depthOrLayers;
}

struct ResolvedTextureRange {
    uint32_t baseMip = 0u;
    uint32_t mipCount = 0u;
    uint32_t baseLayer = 0u;
    uint32_t layerCount = 0u;
    RhiTextureAspectFlags aspect = 0u;
};

[[nodiscard]] RhiTextureAspectFlags formatAspect(const RhiTextureFormat format) {
    switch (format) {
    case RhiTextureFormat::Depth16:
    case RhiTextureFormat::Depth24:
    case RhiTextureFormat::Depth32Float: return rhiFlag(RhiTextureAspect::Depth);
    case RhiTextureFormat::Depth24Stencil8:
        return rhiFlag(RhiTextureAspect::Depth) | rhiFlag(RhiTextureAspect::Stencil);
    default: return rhiFlag(RhiTextureAspect::Color);
    }
}

[[nodiscard]] bool resolveTextureRange(const RhiTextureDesc& desc, const RgTextureSubresourceRange& range,
                                       ResolvedTextureRange& resolved) {
    resolved.baseMip = range.baseMip;
    resolved.mipCount = resolvedCount(range.baseMip, range.mipCount, desc.mipLevels);
    resolved.baseLayer = range.baseLayer;
    resolved.layerCount = resolvedCount(range.baseLayer, range.layerCount, textureSubresourceLayerCount(desc));
    resolved.aspect = range.aspect != 0u ? range.aspect : formatAspect(desc.format);
    const RhiTextureAspectFlags validAspects = formatAspect(desc.format);
    return resolved.mipCount != 0u && resolved.layerCount != 0u && (resolved.aspect & ~validAspects) == 0u;
}

[[nodiscard]] bool textureRangesOverlap(const ResolvedTextureRange& lhs, const ResolvedTextureRange& rhs) {
    const bool mipOverlap = lhs.baseMip < rhs.baseMip + rhs.mipCount && rhs.baseMip < lhs.baseMip + lhs.mipCount;
    const bool layerOverlap =
        lhs.baseLayer < rhs.baseLayer + rhs.layerCount && rhs.baseLayer < lhs.baseLayer + lhs.layerCount;
    return mipOverlap && layerOverlap && (lhs.aspect & rhs.aspect) != 0u;
}

[[nodiscard]] bool resolveBufferRange(const RhiBufferDesc& desc, const RgBufferRange& range, uint64_t& begin,
                                      uint64_t& end) {
    if (range.offset >= desc.size)
        return false;
    begin = range.offset;
    if (range.size == kRhiWholeSize) {
        end = desc.size;
        return true;
    }
    if (range.size == 0u || range.size > desc.size - range.offset)
        return false;
    end = range.offset + range.size;
    return true;
}

[[nodiscard]] bool bufferRangesOverlap(const uint64_t lhsBegin, const uint64_t lhsEnd, const uint64_t rhsBegin,
                                       const uint64_t rhsEnd) {
    return lhsBegin < rhsEnd && rhsBegin < lhsEnd;
}

void addUniqueEdge(std::vector<std::vector<uint32_t>>& edges, std::vector<std::vector<uint32_t>>& dependencies,
                   const uint32_t source, const uint32_t destination) {
    if (source == destination)
        return;
    auto& outgoing = edges[source];
    if (std::find(outgoing.begin(), outgoing.end(), destination) != outgoing.end())
        return;
    outgoing.push_back(destination);
    dependencies[destination].push_back(source);
}

[[nodiscard]] bool hasDependencyPath(const std::vector<std::vector<uint32_t>>& edges, const uint32_t source,
                                     const uint32_t destination) {
    if (source == destination)
        return true;
    std::vector<uint8_t> visited(edges.size(), 0u);
    std::vector<uint32_t> pending{source};
    visited[source] = 1u;
    while (!pending.empty()) {
        const uint32_t current = pending.back();
        pending.pop_back();
        for (const uint32_t next : edges[current]) {
            if (next == destination)
                return true;
            if (visited[next] == 0u) {
                visited[next] = 1u;
                pending.push_back(next);
            }
        }
    }
    return false;
}

[[nodiscard]] bool queueCompatibleWithPass(const RgPassType type, const RhiQueueType queue) {
    switch (type) {
    case RgPassType::Graphics: return queue == RhiQueueType::Graphics;
    case RgPassType::Compute: return queue == RhiQueueType::Graphics || queue == RhiQueueType::Compute;
    case RgPassType::Copy:
        return queue == RhiQueueType::Graphics || queue == RhiQueueType::Compute || queue == RhiQueueType::Transfer;
    case RgPassType::External: return queue != RhiQueueType::Present;
    }
    return false;
}

[[nodiscard]] bool stateCompatibleWithQueue(const RhiResourceState state, const RhiQueueType queue) {
    if (queue == RhiQueueType::Graphics)
        return true;
    if (queue == RhiQueueType::Present)
        return false;
    switch (state) {
    case RhiResourceState::Undefined:
    case RhiResourceState::TransferSrc:
    case RhiResourceState::TransferDst:
    case RhiResourceState::HostRead:
    case RhiResourceState::HostWrite: return true;
    case RhiResourceState::ShaderRead:
    case RhiResourceState::ShaderWrite:
    case RhiResourceState::DepthRead:
    case RhiResourceState::IndirectArgument:
    case RhiResourceState::UniformBuffer:
    case RhiResourceState::StorageBuffer: return queue == RhiQueueType::Compute;
    case RhiResourceState::Present:
    case RhiResourceState::RenderTarget:
    case RhiResourceState::DepthWrite:
    case RhiResourceState::VertexBuffer:
    case RhiResourceState::IndexBuffer: return false;
    }
    return false;
}

[[nodiscard]] RgTextureSubresourceRange singleSubresourceRange(const uint32_t mip, const uint32_t layer,
                                                               const RhiTextureAspectFlags aspect) {
    return {mip, 1u, layer, 1u, aspect};
}

[[nodiscard]] bool sameTextureDesc(const RhiTextureDesc& lhs, const RhiTextureDesc& rhs) {
    return lhs.dimension == rhs.dimension && lhs.format == rhs.format && lhs.width == rhs.width &&
           lhs.height == rhs.height && lhs.depthOrLayers == rhs.depthOrLayers && lhs.mipLevels == rhs.mipLevels &&
           lhs.sampleCount == rhs.sampleCount && lhs.usage == rhs.usage && lhs.memoryCategory == rhs.memoryCategory &&
           lhs.queueSharing == rhs.queueSharing;
}

[[nodiscard]] bool sameBufferDesc(const RhiBufferDesc& lhs, const RhiBufferDesc& rhs) {
    return lhs.size == rhs.size && lhs.usage == rhs.usage && lhs.memoryUsage == rhs.memoryUsage &&
           lhs.memoryCategory == rhs.memoryCategory;
}

[[nodiscard]] RhiTextureViewType defaultViewType(const RhiTextureDimension dimension) {
    switch (dimension) {
    case RhiTextureDimension::Texture2D: return RhiTextureViewType::Texture2D;
    case RhiTextureDimension::Texture2DArray: return RhiTextureViewType::Texture2DArray;
    case RhiTextureDimension::Texture3D: return RhiTextureViewType::Texture3D;
    case RhiTextureDimension::Cube: return RhiTextureViewType::Cube;
    case RhiTextureDimension::CubeArray: return RhiTextureViewType::CubeArray;
    }
    return RhiTextureViewType::Texture2D;
}

[[nodiscard]] RhiCommandListType commandListType(const RhiQueueType queue) {
    switch (queue) {
    case RhiQueueType::Graphics:
    case RhiQueueType::Present: return RhiCommandListType::Graphics;
    case RhiQueueType::Compute: return RhiCommandListType::Compute;
    case RhiQueueType::Transfer: return RhiCommandListType::Transfer;
    }
    return RhiCommandListType::Graphics;
}

[[nodiscard]] uint32_t queueFamily(const RhiCapabilities& capabilities, const RhiQueueType queue) {
    switch (queue) {
    case RhiQueueType::Graphics: return capabilities.graphicsQueueFamilyIndex;
    case RhiQueueType::Compute: return capabilities.computeQueueFamilyIndex;
    case RhiQueueType::Transfer: return capabilities.transferQueueFamilyIndex;
    case RhiQueueType::Present: return capabilities.presentQueueFamilyIndex;
    }
    return kRhiQueueFamilyIgnored;
}

[[nodiscard]] bool queueSupportsTimestamps(const RhiCapabilities& capabilities, const RhiQueueType queue) {
    switch (queue) {
    case RhiQueueType::Graphics: return capabilities.graphicsTimestampQuery;
    case RhiQueueType::Compute: return capabilities.computeTimestampQuery;
    case RhiQueueType::Transfer: return capabilities.transferTimestampQuery;
    case RhiQueueType::Present: return false;
    }
    return false;
}

[[nodiscard]] uint32_t queryCapacityFor(const uint32_t required) {
    uint32_t capacity = 2u;
    while (capacity < required && capacity <= std::numeric_limits<uint32_t>::max() / 2u) {
        capacity *= 2u;
    }
    return capacity >= required ? capacity : required;
}
} // namespace

struct RenderGraph::TextureRecord {
    std::string name;
    RhiTextureHandle importedHandle;
    RhiTextureDesc desc;
    RhiResourceState initialState = RhiResourceState::Undefined;
    RhiResourceState finalState = RhiResourceState::Undefined;
    RhiTextureViewHandle importedView;
    RhiQueueType initialQueue = RhiQueueType::Graphics;
    RhiQueueType finalQueue = RhiQueueType::Graphics;
    bool imported = false;
    uint32_t generation = 1u;
};

struct RenderGraph::BufferRecord {
    std::string name;
    RhiBufferHandle importedHandle;
    RhiBufferDesc desc;
    RhiResourceState initialState = RhiResourceState::Undefined;
    RhiResourceState finalState = RhiResourceState::Undefined;
    RhiQueueType initialQueue = RhiQueueType::Graphics;
    RhiQueueType finalQueue = RhiQueueType::Graphics;
    bool imported = false;
    uint32_t generation = 1u;
};

struct RenderGraph::TransientTexture {
    RhiTextureDesc desc;
    RhiTextureHandle texture;
    RhiTextureViewHandle view;
    std::vector<RhiResourceState> states;
    std::vector<RhiQueueType> queues;
    std::vector<RhiSubmissionToken> lastUses;
    uint32_t claimedGeneration = 0u;
    /// Index into m_aliasPages when the texture is placed on a shared memory
    /// page; dedicated allocations keep the invalid sentinel.
    uint32_t pageIndex = kRgInvalidPassIndex;
};

struct RenderGraph::TransientBuffer {
    RhiBufferDesc desc;
    RhiBufferHandle buffer;
    RhiResourceState state = RhiResourceState::Undefined;
    RhiQueueType queue = RhiQueueType::Graphics;
    RhiSubmissionToken lastUse;
    uint32_t claimedGeneration = 0u;
};

struct RenderGraph::TimingSlot {
    RhiQueryPoolHandle queryPool;
    uint32_t queryCapacity = 0u;
    uint64_t execution = 0u;
    std::vector<std::string> passNames;
    std::vector<RhiQueueType> passQueues;
    std::vector<RhiSubmissionToken> submissions;
    uint32_t issuedPassCount = 0u;
    bool pending = false;
};

struct RenderGraph::PassRecord {
    std::string name;
    RgPassType type = RgPassType::Graphics;
    RhiQueueType queue = RhiQueueType::Graphics;
    bool threadSafeRecord = false;
    std::vector<RgTextureAccess> textureAccesses;
    std::vector<RgBufferAccess> bufferAccesses;
    std::vector<RgPassHandle> explicitDependencies;
    RgExecuteCallback execute;
    uint32_t generation = 1u;
};

RgPassContext::RgPassContext(RhiCommandList& commandList, const std::vector<RhiTextureHandle>& textures,
                             const std::vector<RhiTextureViewHandle>& textureViews,
                             const std::vector<RhiBufferHandle>& buffers)
    : m_commandList(&commandList), m_textures(&textures), m_textureViews(&textureViews), m_buffers(&buffers) {}

RhiCommandList& RgPassContext::commandList() const {
    return *m_commandList;
}

RhiTextureHandle RgPassContext::texture(const RgTextureHandle handle) const {
    if (!handle.isValid() || m_textures == nullptr || handle.index > m_textures->size())
        return {};
    return (*m_textures)[handle.index - 1u];
}

RhiTextureViewHandle RgPassContext::textureView(const RgTextureHandle handle) const {
    if (!handle.isValid() || m_textureViews == nullptr || handle.index > m_textureViews->size())
        return {};
    return (*m_textureViews)[handle.index - 1u];
}

RhiBufferHandle RgPassContext::buffer(const RgBufferHandle handle) const {
    if (!handle.isValid() || m_buffers == nullptr || handle.index > m_buffers->size())
        return {};
    return (*m_buffers)[handle.index - 1u];
}

RenderGraphPassBuilder::RenderGraphPassBuilder(RenderGraph& graph, const RgPassHandle handle)
    : m_graph(&graph), m_handle(handle) {}

RenderGraphPassBuilder& RenderGraphPassBuilder::readTexture(const RgTextureHandle texture, const RhiResourceState state,
                                                            const RgTextureSubresourceRange range) {
    static_cast<void>(m_graph->addTextureAccess(m_handle, {texture, RgAccessType::Read, state, range}));
    return *this;
}

RenderGraphPassBuilder& RenderGraphPassBuilder::writeTexture(const RgTextureHandle texture,
                                                             const RhiResourceState state,
                                                             const RgTextureSubresourceRange range) {
    static_cast<void>(m_graph->addTextureAccess(m_handle, {texture, RgAccessType::Write, state, range}));
    return *this;
}

RenderGraphPassBuilder& RenderGraphPassBuilder::readWriteTexture(const RgTextureHandle texture,
                                                                 const RhiResourceState state,
                                                                 const RgTextureSubresourceRange range) {
    static_cast<void>(m_graph->addTextureAccess(m_handle, {texture, RgAccessType::ReadWrite, state, range}));
    return *this;
}

RenderGraphPassBuilder& RenderGraphPassBuilder::readBuffer(const RgBufferHandle buffer, const RhiResourceState state,
                                                           const RgBufferRange range) {
    static_cast<void>(m_graph->addBufferAccess(m_handle, {buffer, RgAccessType::Read, state, range}));
    return *this;
}

RenderGraphPassBuilder& RenderGraphPassBuilder::writeBuffer(const RgBufferHandle buffer, const RhiResourceState state,
                                                            const RgBufferRange range) {
    static_cast<void>(m_graph->addBufferAccess(m_handle, {buffer, RgAccessType::Write, state, range}));
    return *this;
}

RenderGraphPassBuilder& RenderGraphPassBuilder::readWriteBuffer(const RgBufferHandle buffer,
                                                                const RhiResourceState state,
                                                                const RgBufferRange range) {
    static_cast<void>(m_graph->addBufferAccess(m_handle, {buffer, RgAccessType::ReadWrite, state, range}));
    return *this;
}

RenderGraphPassBuilder& RenderGraphPassBuilder::dependsOn(const RgPassHandle dependency) {
    static_cast<void>(m_graph->addDependency(m_handle, dependency));
    return *this;
}

RenderGraphPassBuilder& RenderGraphPassBuilder::setExecute(RgExecuteCallback callback) {
    static_cast<void>(m_graph->setExecute(m_handle, std::move(callback)));
    return *this;
}

RenderGraph::RenderGraph() = default;

RenderGraph::~RenderGraph() {
    if (m_runtimeDevice != nullptr)
        releaseTransientResources(*m_runtimeDevice);
}

RgTextureHandle RenderGraph::importTexture(const RgImportedTextureDesc& desc) {
    if (desc.name == nullptr || desc.name[0] == '\0') {
        setBuilderError(RgCompileError::EmptyName, "imported render graph texture requires a name");
        return {};
    }
    if (!desc.texture.isValid() || desc.desc.width == 0u || desc.desc.height == 0u || desc.desc.depthOrLayers == 0u ||
        desc.desc.mipLevels == 0u) {
        setBuilderError(RgCompileError::InvalidTextureHandle, "invalid imported texture description");
        return {};
    }
    m_textures.push_back({desc.name, desc.texture, desc.desc, desc.initialState, desc.finalState, desc.defaultView,
                          desc.initialQueue, desc.finalQueue, true, m_generation});
    return {static_cast<uint32_t>(m_textures.size()), m_generation};
}

RgTextureHandle RenderGraph::createTexture(const RgTransientTextureDesc& desc) {
    if (desc.name == nullptr || desc.name[0] == '\0') {
        setBuilderError(RgCompileError::EmptyName, "transient render graph texture requires a name");
        return {};
    }
    if (desc.desc.width == 0u || desc.desc.height == 0u || desc.desc.depthOrLayers == 0u || desc.desc.mipLevels == 0u) {
        setBuilderError(RgCompileError::InvalidTextureAccess, "invalid transient texture description");
        return {};
    }
    RhiTextureDesc resourceDesc = desc.desc;
    resourceDesc.memoryCategory = RhiMemoryCategory::Transient;
    m_textures.push_back({desc.name,
                          {},
                          resourceDesc,
                          RhiResourceState::Undefined,
                          desc.finalState,
                          {},
                          RhiQueueType::Graphics,
                          RhiQueueType::Graphics,
                          false,
                          m_generation});
    return {static_cast<uint32_t>(m_textures.size()), m_generation};
}

RgBufferHandle RenderGraph::importBuffer(const RgImportedBufferDesc& desc) {
    if (desc.name == nullptr || desc.name[0] == '\0') {
        setBuilderError(RgCompileError::EmptyName, "imported render graph buffer requires a name");
        return {};
    }
    if (!desc.buffer.isValid() || desc.desc.size == 0u) {
        setBuilderError(RgCompileError::InvalidBufferHandle, "invalid imported buffer description");
        return {};
    }
    m_buffers.push_back({desc.name, desc.buffer, desc.desc, desc.initialState, desc.finalState, desc.initialQueue,
                         desc.finalQueue, true, m_generation});
    return {static_cast<uint32_t>(m_buffers.size()), m_generation};
}

RgBufferHandle RenderGraph::createBuffer(const RgTransientBufferDesc& desc) {
    if (desc.name == nullptr || desc.name[0] == '\0') {
        setBuilderError(RgCompileError::EmptyName, "transient render graph buffer requires a name");
        return {};
    }
    if (desc.desc.size == 0u) {
        setBuilderError(RgCompileError::InvalidBufferAccess, "invalid transient buffer description");
        return {};
    }
    RhiBufferDesc resourceDesc = desc.desc;
    resourceDesc.memoryCategory = RhiMemoryCategory::Transient;
    m_buffers.push_back({desc.name,
                         {},
                         resourceDesc,
                         RhiResourceState::Undefined,
                         desc.finalState,
                         RhiQueueType::Graphics,
                         RhiQueueType::Graphics,
                         false,
                         m_generation});
    return {static_cast<uint32_t>(m_buffers.size()), m_generation};
}

RenderGraphPassBuilder RenderGraph::addPass(const RgPassDesc& desc) {
    if (desc.name == nullptr || desc.name[0] == '\0') {
        setBuilderError(RgCompileError::EmptyName, "render graph pass requires a name");
        return {*this, {}};
    }
    if (!queueCompatibleWithPass(desc.type, desc.queue)) {
        setBuilderError(RgCompileError::InvalidPassHandle, "render graph pass type is incompatible with its queue");
        return {*this, {}};
    }
    m_passes.push_back({desc.name, desc.type, desc.queue, desc.threadSafeRecord, {}, {}, {}, {}, m_generation});
    return {*this, {static_cast<uint32_t>(m_passes.size()), m_generation}};
}

RenderGraphPassBuilder RenderGraph::editPass(const RgPassHandle pass) {
    if (passRecord(pass) == nullptr) {
        setBuilderError(RgCompileError::InvalidPassHandle, "cannot edit an invalid render graph pass");
        return {*this, {}};
    }
    return {*this, pass};
}

bool RenderGraph::addTextureAccess(const RgPassHandle pass, const RgTextureAccess& access) {
    PassRecord* const record = passRecord(pass);
    const TextureRecord* const texture = textureRecord(access.texture);
    if (record == nullptr || texture == nullptr) {
        setBuilderError(RgCompileError::InvalidTextureHandle, "render graph texture access contains an invalid handle");
        return false;
    }
    ResolvedTextureRange resolved;
    if (!textureStateAcceptsAccess(access.state, access.access) ||
        !stateCompatibleWithQueue(access.state, record->queue) ||
        !resolveTextureRange(texture->desc, access.range, resolved)) {
        setBuilderError(RgCompileError::InvalidTextureAccess,
                        "render graph texture access is incompatible with its state or range");
        return false;
    }
    for (const RgTextureAccess& existing : record->textureAccesses) {
        if (existing.texture.index != access.texture.index)
            continue;
        ResolvedTextureRange existingRange;
        static_cast<void>(resolveTextureRange(texture->desc, existing.range, existingRange));
        if (textureRangesOverlap(existingRange, resolved)) {
            setBuilderError(RgCompileError::DuplicateResourceAccess,
                            "render graph pass declares overlapping texture accesses");
            return false;
        }
    }
    record->textureAccesses.push_back(access);
    m_compiled = false;
    return true;
}

bool RenderGraph::addBufferAccess(const RgPassHandle pass, const RgBufferAccess& access) {
    PassRecord* const record = passRecord(pass);
    const BufferRecord* const buffer = bufferRecord(access.buffer);
    if (record == nullptr || buffer == nullptr) {
        setBuilderError(RgCompileError::InvalidBufferHandle, "render graph buffer access contains an invalid handle");
        return false;
    }
    uint64_t begin = 0u;
    uint64_t end = 0u;
    if (!bufferStateAcceptsAccess(access.state, access.access) ||
        !stateCompatibleWithQueue(access.state, record->queue) ||
        !resolveBufferRange(buffer->desc, access.range, begin, end)) {
        setBuilderError(RgCompileError::InvalidBufferAccess,
                        "render graph buffer access is incompatible with its state or range");
        return false;
    }
    for (const RgBufferAccess& existing : record->bufferAccesses) {
        if (existing.buffer.index != access.buffer.index)
            continue;
        uint64_t existingBegin = 0u;
        uint64_t existingEnd = 0u;
        static_cast<void>(resolveBufferRange(buffer->desc, existing.range, existingBegin, existingEnd));
        if (bufferRangesOverlap(existingBegin, existingEnd, begin, end)) {
            setBuilderError(RgCompileError::DuplicateResourceAccess,
                            "render graph pass declares overlapping buffer accesses");
            return false;
        }
    }
    record->bufferAccesses.push_back(access);
    m_compiled = false;
    return true;
}

bool RenderGraph::addDependency(const RgPassHandle pass, const RgPassHandle dependency) {
    PassRecord* const record = passRecord(pass);
    if (record == nullptr || passRecord(dependency) == nullptr ||
        (pass.index == dependency.index && pass.generation == dependency.generation)) {
        setBuilderError(RgCompileError::InvalidPassHandle, "render graph dependency contains an invalid pass handle");
        return false;
    }
    if (std::find_if(record->explicitDependencies.begin(), record->explicitDependencies.end(),
                     [dependency](const RgPassHandle candidate) {
                         return candidate.index == dependency.index && candidate.generation == dependency.generation;
                     }) == record->explicitDependencies.end()) {
        record->explicitDependencies.push_back(dependency);
    }
    m_compiled = false;
    return true;
}

bool RenderGraph::setExecute(const RgPassHandle pass, RgExecuteCallback callback) {
    PassRecord* const record = passRecord(pass);
    if (record == nullptr || !callback) {
        setBuilderError(RgCompileError::MissingExecuteCallback, "render graph pass execute callback is invalid");
        return false;
    }
    record->execute = std::move(callback);
    m_compiled = false;
    return true;
}

void RenderGraph::setBuilderError(const RgCompileError error, const char* const message) {
    if (m_builderError)
        return;
    m_builderError = true;
    m_builderErrorCode = error;
    m_builderErrorMessage = message;
}

RenderGraph::TextureRecord* RenderGraph::textureRecord(const RgTextureHandle handle) {
    if (!handle.isValid() || handle.index > m_textures.size())
        return nullptr;
    TextureRecord& record = m_textures[handle.index - 1u];
    return record.generation == handle.generation ? &record : nullptr;
}

const RenderGraph::TextureRecord* RenderGraph::textureRecord(const RgTextureHandle handle) const {
    if (!handle.isValid() || handle.index > m_textures.size())
        return nullptr;
    const TextureRecord& record = m_textures[handle.index - 1u];
    return record.generation == handle.generation ? &record : nullptr;
}

RenderGraph::BufferRecord* RenderGraph::bufferRecord(const RgBufferHandle handle) {
    if (!handle.isValid() || handle.index > m_buffers.size())
        return nullptr;
    BufferRecord& record = m_buffers[handle.index - 1u];
    return record.generation == handle.generation ? &record : nullptr;
}

const RenderGraph::BufferRecord* RenderGraph::bufferRecord(const RgBufferHandle handle) const {
    if (!handle.isValid() || handle.index > m_buffers.size())
        return nullptr;
    const BufferRecord& record = m_buffers[handle.index - 1u];
    return record.generation == handle.generation ? &record : nullptr;
}

RenderGraph::PassRecord* RenderGraph::passRecord(const RgPassHandle handle) {
    if (!handle.isValid() || handle.index > m_passes.size())
        return nullptr;
    PassRecord& record = m_passes[handle.index - 1u];
    return record.generation == handle.generation ? &record : nullptr;
}

const RenderGraph::PassRecord* RenderGraph::passRecord(const RgPassHandle handle) const {
    if (!handle.isValid() || handle.index > m_passes.size())
        return nullptr;
    const PassRecord& record = m_passes[handle.index - 1u];
    return record.generation == handle.generation ? &record : nullptr;
}

namespace {
struct FnvHasher {
    uint64_t state = 1469598103934665603ull;
    void mix(const void* data, const size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0u; i < size; ++i) {
            state ^= bytes[i];
            state *= 1099511628211ull;
        }
    }
    template <typename T> void mixValue(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        mix(&value, sizeof(value));
    }
    void mixString(const std::string& value) {
        mix(value.data(), value.size());
        mixValue<uint64_t>(value.size());
    }
};
} // namespace

uint64_t RenderGraph::computeStructuralFingerprint() const {
    FnvHasher hasher;
    hasher.mixValue<uint64_t>(m_textures.size());
    for (const TextureRecord& record : m_textures) {
        hasher.mixString(record.name);
        hasher.mixValue(record.imported);
        hasher.mixValue(record.desc.dimension);
        hasher.mixValue(record.desc.format);
        hasher.mixValue(record.desc.width);
        hasher.mixValue(record.desc.height);
        hasher.mixValue(record.desc.depthOrLayers);
        hasher.mixValue(record.desc.mipLevels);
        hasher.mixValue(record.desc.sampleCount);
        hasher.mixValue(record.desc.usage);
        hasher.mixValue(record.desc.queueSharing);
        hasher.mixValue(record.initialState);
        hasher.mixValue(record.finalState);
        hasher.mixValue(record.initialQueue);
        hasher.mixValue(record.finalQueue);
    }
    hasher.mixValue<uint64_t>(m_buffers.size());
    for (const BufferRecord& record : m_buffers) {
        hasher.mixString(record.name);
        hasher.mixValue(record.imported);
        hasher.mixValue(record.desc.size);
        hasher.mixValue(record.desc.usage);
        hasher.mixValue(record.initialState);
        hasher.mixValue(record.finalState);
        hasher.mixValue(record.initialQueue);
        hasher.mixValue(record.finalQueue);
    }
    hasher.mixValue<uint64_t>(m_passes.size());
    for (const PassRecord& pass : m_passes) {
        hasher.mixString(pass.name);
        hasher.mixValue(pass.type);
        hasher.mixValue(pass.queue);
        hasher.mixValue(pass.threadSafeRecord);
        hasher.mixValue<uint64_t>(pass.textureAccesses.size());
        for (const RgTextureAccess& access : pass.textureAccesses) {
            hasher.mixValue(access.texture.index);
            hasher.mixValue(access.access);
            hasher.mixValue(access.state);
            hasher.mixValue(access.range.baseMip);
            hasher.mixValue(access.range.mipCount);
            hasher.mixValue(access.range.baseLayer);
            hasher.mixValue(access.range.layerCount);
            hasher.mixValue(access.range.aspect);
        }
        hasher.mixValue<uint64_t>(pass.bufferAccesses.size());
        for (const RgBufferAccess& access : pass.bufferAccesses) {
            hasher.mixValue(access.buffer.index);
            hasher.mixValue(access.access);
            hasher.mixValue(access.state);
            hasher.mixValue(access.range.offset);
            hasher.mixValue(access.range.size);
        }
        hasher.mixValue<uint64_t>(pass.explicitDependencies.size());
        for (const RgPassHandle dependency : pass.explicitDependencies) {
            hasher.mixValue(dependency.index);
        }
    }
    return hasher.state;
}

RgCompileResult RenderGraph::compile() {
    m_compiledPasses.clear();
    m_textureLifetimes.assign(m_textures.size(), {});
    m_bufferLifetimes.assign(m_buffers.size(), {});
    m_epilogueTextureBarriers.clear();
    m_epilogueBufferBarriers.clear();
    m_submissionBatches.clear();
    m_passToBatch.clear();
    m_textureLastPasses.clear();
    m_bufferLastPasses.clear();
    m_compiled = false;

    if (m_builderError) {
        return {m_builderErrorCode, m_builderErrorMessage};
    }

    // Frame graphs alternate between a small set of topologies; replaying a
    // cached plan skips the full dependency and barrier compilation.
    const uint64_t fingerprint = computeStructuralFingerprint();
    const auto cached = m_compiledPlanCache.find(fingerprint);
    if (cached != m_compiledPlanCache.end()) {
        const CompiledPlan& plan = cached->second;
        m_compiledPasses = plan.compiledPasses;
        m_textureLifetimes = plan.textureLifetimes;
        m_bufferLifetimes = plan.bufferLifetimes;
        m_epilogueTextureBarriers = plan.epilogueTextureBarriers;
        m_epilogueBufferBarriers = plan.epilogueBufferBarriers;
        m_submissionBatches = plan.submissionBatches;
        m_passToBatch = plan.passToBatch;
        m_textureLastPasses = plan.textureLastPasses;
        m_bufferLastPasses = plan.bufferLastPasses;
        m_compiled = true;
        return {};
    }
    for (const PassRecord& pass : m_passes) {
        if (!pass.execute) {
            return {RgCompileError::MissingExecuteCallback,
                    "render graph pass '" + pass.name + "' has no execute callback"};
        }
    }

    const uint32_t passCount = static_cast<uint32_t>(m_passes.size());
    std::vector<std::vector<uint32_t>> edges(passCount);
    std::vector<std::vector<uint32_t>> dependencies(passCount);
    for (uint32_t passIndex = 0u; passIndex < passCount; ++passIndex) {
        for (const RgPassHandle dependency : m_passes[passIndex].explicitDependencies) {
            addUniqueEdge(edges, dependencies, dependency.index - 1u, passIndex);
        }
    }

    for (uint32_t textureIndex = 0u; textureIndex < m_textures.size(); ++textureIndex) {
        const TextureRecord& texture = m_textures[textureIndex];
        struct PriorAccess {
            uint32_t pass = 0u;
            RgTextureAccess access;
            ResolvedTextureRange range;
        };
        std::vector<PriorAccess> prior;
        for (uint32_t passIndex = 0u; passIndex < passCount; ++passIndex) {
            for (const RgTextureAccess& access : m_passes[passIndex].textureAccesses) {
                if (access.texture.index != textureIndex + 1u)
                    continue;
                ResolvedTextureRange range;
                static_cast<void>(resolveTextureRange(texture.desc, access.range, range));
                if ((!texture.imported || texture.initialState == RhiResourceState::Undefined) &&
                    includesRead(access.access)) {
                    for (uint32_t mip = range.baseMip; mip < range.baseMip + range.mipCount; ++mip) {
                        for (uint32_t layer = range.baseLayer; layer < range.baseLayer + range.layerCount; ++layer) {
                            bool initialized = false;
                            for (const PriorAccess& candidate : prior) {
                                const bool includesSubresource =
                                    mip >= candidate.range.baseMip &&
                                    mip < candidate.range.baseMip + candidate.range.mipCount &&
                                    layer >= candidate.range.baseLayer &&
                                    layer < candidate.range.baseLayer + candidate.range.layerCount &&
                                    (candidate.range.aspect & range.aspect) != 0u;
                                if (includesSubresource && includesWrite(candidate.access.access)) {
                                    initialized = true;
                                    break;
                                }
                            }
                            if (!initialized) {
                                return {RgCompileError::ReadBeforeWrite,
                                        "transient texture '" + texture.name +
                                            "' is read before the requested subresource is "
                                            "written"};
                            }
                        }
                    }
                }
                for (const PriorAccess& candidate : prior) {
                    const RhiQueueType candidateQueue = m_passes[candidate.pass].queue;
                    const RhiQueueType currentQueue = m_passes[passIndex].queue;
                    const bool queueChanged = candidateQueue != currentQueue;
                    const bool concurrentRead =
                        queueChanged && textureQueuesShareConcurrently(texture.desc, candidateQueue, currentQueue) &&
                        candidate.access.state == access.state && !includesWrite(candidate.access.access) &&
                        !includesWrite(access.access);
                    if (textureRangesOverlap(candidate.range, range) &&
                        (includesWrite(candidate.access.access) || includesWrite(access.access) ||
                         (queueChanged && !concurrentRead))) {
                        addUniqueEdge(edges, dependencies, candidate.pass, passIndex);
                    }
                }
                prior.push_back({passIndex, access, range});
            }
        }
    }

    for (uint32_t bufferIndex = 0u; bufferIndex < m_buffers.size(); ++bufferIndex) {
        const BufferRecord& buffer = m_buffers[bufferIndex];
        struct PriorAccess {
            uint32_t pass = 0u;
            RgBufferAccess access;
            uint64_t begin = 0u;
            uint64_t end = 0u;
        };
        std::vector<PriorAccess> prior;
        for (uint32_t passIndex = 0u; passIndex < passCount; ++passIndex) {
            for (const RgBufferAccess& access : m_passes[passIndex].bufferAccesses) {
                if (access.buffer.index != bufferIndex + 1u)
                    continue;
                uint64_t begin = 0u;
                uint64_t end = 0u;
                static_cast<void>(resolveBufferRange(buffer.desc, access.range, begin, end));
                if ((!buffer.imported || buffer.initialState == RhiResourceState::Undefined) &&
                    includesRead(access.access)) {
                    std::vector<std::pair<uint64_t, uint64_t>> written;
                    for (const PriorAccess& candidate : prior) {
                        if (!includesWrite(candidate.access.access))
                            continue;
                        const uint64_t overlapBegin = std::max(begin, candidate.begin);
                        const uint64_t overlapEnd = std::min(end, candidate.end);
                        if (overlapBegin < overlapEnd)
                            written.emplace_back(overlapBegin, overlapEnd);
                    }
                    std::sort(written.begin(), written.end());
                    uint64_t covered = begin;
                    for (const auto& interval : written) {
                        if (interval.first > covered)
                            break;
                        covered = std::max(covered, interval.second);
                    }
                    if (covered < end) {
                        return {RgCompileError::ReadBeforeWrite,
                                "transient buffer '" + buffer.name + "' is read before the requested range is written"};
                    }
                }
                for (const PriorAccess& candidate : prior) {
                    if (bufferRangesOverlap(candidate.begin, candidate.end, begin, end) &&
                        (includesWrite(candidate.access.access) || includesWrite(access.access) ||
                         m_passes[candidate.pass].queue != m_passes[passIndex].queue)) {
                        addUniqueEdge(edges, dependencies, candidate.pass, passIndex);
                    }
                }
                prior.push_back({passIndex, access, begin, end});
            }
        }
    }

    std::vector<uint32_t> indegree(passCount, 0u);
    for (uint32_t index = 0u; index < passCount; ++index) {
        indegree[index] = static_cast<uint32_t>(dependencies[index].size());
    }
    std::vector<uint32_t> order;
    order.reserve(passCount);
    while (order.size() < passCount) {
        uint32_t selected = passCount;
        for (uint32_t index = 0u; index < passCount; ++index) {
            if (indegree[index] == 0u && std::find(order.begin(), order.end(), index) == order.end()) {
                selected = index;
                break;
            }
        }
        if (selected == passCount) {
            return {RgCompileError::CyclicDependency, "render graph contains a cyclic pass dependency"};
        }
        order.push_back(selected);
        indegree[selected] = std::numeric_limits<uint32_t>::max();
        for (const uint32_t destination : edges[selected]) {
            --indegree[destination];
        }
    }

    std::vector<uint32_t> originalToCompiled(passCount, 0u);
    for (uint32_t compiledIndex = 0u; compiledIndex < order.size(); ++compiledIndex) {
        originalToCompiled[order[compiledIndex]] = compiledIndex;
    }

    std::vector<std::vector<uint32_t>> reducedDependencies = dependencies;
    for (uint32_t passIndex = 0u; passIndex < passCount; ++passIndex) {
        auto& direct = reducedDependencies[passIndex];
        direct.erase(std::remove_if(direct.begin(), direct.end(),
                                    [&](const uint32_t candidate) {
                                        return std::any_of(dependencies[passIndex].begin(),
                                                           dependencies[passIndex].end(), [&](const uint32_t other) {
                                                               return other != candidate &&
                                                                      hasDependencyPath(edges, candidate, other);
                                                           });
                                    }),
                     direct.end());
    }

    struct TextureState {
        RhiResourceState state = RhiResourceState::Undefined;
        RhiQueueType queue = RhiQueueType::Graphics;
        RgAccessType lastAccess = RgAccessType::Read;
        uint32_t lastPass = std::numeric_limits<uint32_t>::max();
        bool accessed = false;
    };
    std::vector<std::vector<TextureState>> textureStates;
    textureStates.reserve(m_textures.size());
    for (const TextureRecord& texture : m_textures) {
        const uint32_t layerCount = textureSubresourceLayerCount(texture.desc);
        const uint32_t subresourceCount = texture.desc.mipLevels * layerCount;
        textureStates.emplace_back(subresourceCount,
                                   TextureState{texture.initialState, texture.initialQueue, RgAccessType::Read,
                                                std::numeric_limits<uint32_t>::max(), false});
    }
    struct BufferState {
        RhiResourceState state = RhiResourceState::Undefined;
        RhiQueueType queue = RhiQueueType::Graphics;
        RgAccessType lastAccess = RgAccessType::Read;
        uint32_t lastPass = std::numeric_limits<uint32_t>::max();
        bool accessed = false;
    };
    std::vector<BufferState> bufferStates;
    bufferStates.reserve(m_buffers.size());
    for (const BufferRecord& buffer : m_buffers) {
        bufferStates.push_back({buffer.initialState, buffer.initialQueue, RgAccessType::Read,
                                std::numeric_limits<uint32_t>::max(), false});
    }

    m_compiledPasses.reserve(passCount);
    for (uint32_t compiledIndex = 0u; compiledIndex < order.size(); ++compiledIndex) {
        const uint32_t originalIndex = order[compiledIndex];
        const PassRecord& pass = m_passes[originalIndex];
        RgCompiledPass compiled;
        compiled.handle = {originalIndex + 1u, pass.generation};
        compiled.name = pass.name;
        compiled.type = pass.type;
        compiled.queue = pass.queue;
        compiled.threadSafeRecord = pass.threadSafeRecord;
        for (const uint32_t dependency : reducedDependencies[originalIndex]) {
            compiled.dependencies.push_back(originalToCompiled[dependency]);
        }
        std::sort(compiled.dependencies.begin(), compiled.dependencies.end());

        for (const RgTextureAccess& access : pass.textureAccesses) {
            const uint32_t resourceIndex = access.texture.index - 1u;
            const TextureRecord& texture = m_textures[resourceIndex];
            ResolvedTextureRange range;
            static_cast<void>(resolveTextureRange(texture.desc, access.range, range));
            RgResourceLifetime& lifetime = m_textureLifetimes[resourceIndex];
            if (!lifetime.used) {
                lifetime = {compiledIndex, compiledIndex, true, pass.queue == RhiQueueType::Graphics};
            } else {
                lifetime.lastPass = compiledIndex;
                lifetime.graphicsOnly = lifetime.graphicsOnly && pass.queue == RhiQueueType::Graphics;
            }
            for (uint32_t mip = range.baseMip; mip < range.baseMip + range.mipCount; ++mip) {
                for (uint32_t layer = range.baseLayer; layer < range.baseLayer + range.layerCount; ++layer) {
                    TextureState& current =
                        textureStates[resourceIndex][mip * textureSubresourceLayerCount(texture.desc) + layer];
                    const bool hazard =
                        current.accessed && (includesWrite(current.lastAccess) || includesWrite(access.access));
                    const bool ownershipTransition =
                        current.state != RhiResourceState::Undefined && current.queue != pass.queue &&
                        !textureQueuesShareConcurrently(texture.desc, current.queue, pass.queue);
                    if (current.accessed && ownershipTransition && current.lastPass < m_compiledPasses.size()) {
                        m_compiledPasses[current.lastPass].releaseTextureBarriers.push_back(
                            {access.texture, current.state, access.state,
                             singleSubresourceRange(mip, layer, range.aspect), current.queue, pass.queue});
                    }
                    if (current.state != access.state || ownershipTransition || hazard) {
                        compiled.textureBarriers.push_back({access.texture, current.state, access.state,
                                                            singleSubresourceRange(mip, layer, range.aspect),
                                                            current.queue, pass.queue,
                                                            current.accessed ? current.lastPass : kRgInvalidPassIndex});
                    }
                    current = {access.state, pass.queue, access.access, compiledIndex, true};
                }
            }
        }

        for (const RgBufferAccess& access : pass.bufferAccesses) {
            const uint32_t resourceIndex = access.buffer.index - 1u;
            RgResourceLifetime& lifetime = m_bufferLifetimes[resourceIndex];
            if (!lifetime.used) {
                lifetime = {compiledIndex, compiledIndex, true, pass.queue == RhiQueueType::Graphics};
            } else {
                lifetime.lastPass = compiledIndex;
                lifetime.graphicsOnly = lifetime.graphicsOnly && pass.queue == RhiQueueType::Graphics;
            }
            BufferState& current = bufferStates[resourceIndex];
            const bool hazard = current.accessed && (includesWrite(current.lastAccess) || includesWrite(access.access));
            if (current.accessed && current.queue != pass.queue && current.lastPass < m_compiledPasses.size()) {
                m_compiledPasses[current.lastPass].releaseBufferBarriers.push_back(
                    {access.buffer, current.state, access.state, access.range, current.queue, pass.queue});
            }
            if (current.state != access.state || current.queue != pass.queue || hazard) {
                compiled.bufferBarriers.push_back({access.buffer, current.state, access.state, access.range,
                                                   current.queue, pass.queue,
                                                   current.accessed ? current.lastPass : kRgInvalidPassIndex});
            }
            current = {access.state, pass.queue, access.access, compiledIndex, true};
        }
        m_compiledPasses.push_back(std::move(compiled));
    }

    for (uint32_t index = 0u; index < m_textures.size(); ++index) {
        const TextureRecord& texture = m_textures[index];
        const uint32_t layerCount = textureSubresourceLayerCount(texture.desc);
        if (texture.finalState == RhiResourceState::Undefined || !m_textureLifetimes[index].used)
            continue;
        const RgTextureHandle handle{index + 1u, texture.generation};
        for (uint32_t mip = 0u; mip < texture.desc.mipLevels; ++mip) {
            for (uint32_t layer = 0u; layer < layerCount; ++layer) {
                const TextureState& current = textureStates[index][mip * layerCount + layer];
                const bool ownershipTransition =
                    current.queue != texture.finalQueue &&
                    !textureQueuesShareConcurrently(texture.desc, current.queue, texture.finalQueue);
                if (current.state == texture.finalState && !ownershipTransition)
                    continue;
                const uint32_t sourcePass = current.accessed ? current.lastPass : m_textureLifetimes[index].lastPass;
                const RgTextureBarrierPlan barrier{
                    handle,
                    current.state,
                    texture.finalState,
                    singleSubresourceRange(mip, layer, formatAspect(texture.desc.format)),
                    current.queue,
                    texture.finalQueue,
                    sourcePass};
                m_epilogueTextureBarriers.push_back(barrier);
                if (current.state != RhiResourceState::Undefined && ownershipTransition &&
                    sourcePass < m_compiledPasses.size()) {
                    m_compiledPasses[sourcePass].releaseTextureBarriers.push_back(barrier);
                }
            }
        }
    }
    for (uint32_t index = 0u; index < m_buffers.size(); ++index) {
        const BufferRecord& buffer = m_buffers[index];
        if (buffer.finalState == RhiResourceState::Undefined || !m_bufferLifetimes[index].used ||
            (bufferStates[index].state == buffer.finalState && bufferStates[index].queue == buffer.finalQueue))
            continue;
        const RgBufferBarrierPlan barrier{
            {index + 1u, buffer.generation}, bufferStates[index].state, buffer.finalState,           {},
            bufferStates[index].queue,       buffer.finalQueue,         bufferStates[index].lastPass};
        m_epilogueBufferBarriers.push_back(barrier);
        if (bufferStates[index].state != RhiResourceState::Undefined &&
            bufferStates[index].queue != buffer.finalQueue && bufferStates[index].lastPass < m_compiledPasses.size()) {
            m_compiledPasses[bufferStates[index].lastPass].releaseBufferBarriers.push_back(barrier);
        }
    }

    m_textureLastPasses.clear();
    m_textureLastPasses.reserve(textureStates.size());
    for (const auto& states : textureStates) {
        std::vector<uint32_t> lastPasses;
        lastPasses.reserve(states.size());
        for (const TextureState& state : states)
            lastPasses.push_back(state.lastPass);
        m_textureLastPasses.push_back(std::move(lastPasses));
    }
    m_bufferLastPasses.clear();
    m_bufferLastPasses.reserve(bufferStates.size());
    for (const BufferState& state : bufferStates)
        m_bufferLastPasses.push_back(state.lastPass);

    m_passToBatch.assign(passCount, kRgInvalidPassIndex);
    for (uint32_t passIndex = 0u; passIndex < m_compiledPasses.size(); ++passIndex) {
        const RgCompiledPass& pass = m_compiledPasses[passIndex];
        bool startsBatch = m_submissionBatches.empty() ||
                           m_submissionBatches.back().passes.size() >= kRgMaxPassesPerSubmissionBatch ||
                           m_submissionBatches.back().queue != pass.queue || pass.type == RgPassType::External ||
                           m_compiledPasses[m_submissionBatches.back().passes.back()].type == RgPassType::External;
        if (!startsBatch) {
            const uint32_t currentBatch = static_cast<uint32_t>(m_submissionBatches.size() - 1u);
            for (const uint32_t dependencyPass : pass.dependencies) {
                const uint32_t dependencyBatch = m_passToBatch[dependencyPass];
                if (dependencyBatch != currentBatch &&
                    std::find(m_submissionBatches.back().dependencies.begin(),
                              m_submissionBatches.back().dependencies.end(),
                              dependencyBatch) == m_submissionBatches.back().dependencies.end()) {
                    startsBatch = true;
                    break;
                }
            }
        }
        if (startsBatch)
            m_submissionBatches.push_back({pass.queue, {}, {}});
        m_passToBatch[passIndex] = static_cast<uint32_t>(m_submissionBatches.size() - 1u);
        RgSubmissionBatchPlan& batch = m_submissionBatches.back();
        batch.passes.push_back(passIndex);
        for (const uint32_t dependencyPass : pass.dependencies) {
            const uint32_t dependencyBatch = m_passToBatch[dependencyPass];
            if (dependencyBatch != m_passToBatch[passIndex] &&
                std::find(batch.dependencies.begin(), batch.dependencies.end(), dependencyBatch) ==
                    batch.dependencies.end()) {
                batch.dependencies.push_back(dependencyBatch);
            }
        }
        std::sort(batch.dependencies.begin(), batch.dependencies.end());
    }

    if (m_compiledPlanCache.size() >= 8u) {
        m_compiledPlanCache.clear();
    }
    CompiledPlan& plan = m_compiledPlanCache[fingerprint];
    plan.compiledPasses = m_compiledPasses;
    plan.textureLifetimes = m_textureLifetimes;
    plan.bufferLifetimes = m_bufferLifetimes;
    plan.epilogueTextureBarriers = m_epilogueTextureBarriers;
    plan.epilogueBufferBarriers = m_epilogueBufferBarriers;
    plan.submissionBatches = m_submissionBatches;
    plan.passToBatch = m_passToBatch;
    plan.textureLastPasses = m_textureLastPasses;
    plan.bufferLastPasses = m_bufferLastPasses;
    m_compiled = true;
    return {};
}

RhiTextureHandle RenderGraph::resolvedTexture(const RgTextureHandle handle) const {
    if (m_currentResolvedTextures == nullptr || handle.index == 0u || handle.index > m_currentResolvedTextures->size())
        return {};
    return (*m_currentResolvedTextures)[handle.index - 1u];
}

RhiTextureViewHandle RenderGraph::resolvedTextureView(const RgTextureHandle handle) const {
    if (m_currentResolvedTextureViews == nullptr || handle.index == 0u ||
        handle.index > m_currentResolvedTextureViews->size())
        return {};
    return (*m_currentResolvedTextureViews)[handle.index - 1u];
}

bool RenderGraph::lookupTextureRequirements(RhiDevice& device, const RhiTextureDesc& desc,
                                            RhiTextureMemoryRequirements& requirements) {
    for (const TextureRequirementsCacheEntry& entry : m_textureRequirementsCache) {
        if (sameTextureDesc(entry.desc, desc)) {
            requirements = entry.requirements;
            return entry.supported;
        }
    }
    TextureRequirementsCacheEntry entry;
    entry.desc = desc;
    // The cached description must not outlive the caller's name storage.
    entry.desc.debugName = nullptr;
    entry.supported = device.getTextureMemoryRequirements(desc, entry.requirements);
    m_textureRequirementsCache.push_back(entry);
    requirements = entry.requirements;
    return entry.supported;
}

RenderGraph::TransientTexture* RenderGraph::resolveAliasedTransient(RhiDevice& device, const TextureRecord& record,
                                                                    const RgResourceLifetime& lifetime,
                                                                    const RhiTextureMemoryRequirements& requirements) {
    // Pick the first page whose block satisfies the requirements and whose
    // claims this frame are all disjoint from the requested pass interval.
    uint32_t pageIndex = kRgInvalidPassIndex;
    for (uint32_t candidate = 0u; candidate < m_aliasPages.size(); ++candidate) {
        AliasPage& page = m_aliasPages[candidate];
        if (requirements.sizeBytes > page.requirements.sizeBytes ||
            requirements.alignment > page.requirements.alignment ||
            (requirements.memoryTypeBits & page.requirements.memoryTypeBits) == 0u) {
            continue;
        }
        if (page.claimsGeneration == m_generation) {
            bool overlaps = false;
            for (const AliasPageClaim& claim : page.frameClaims) {
                if (lifetime.firstPass <= claim.lastPass && claim.firstPass <= lifetime.lastPass) {
                    overlaps = true;
                    break;
                }
            }
            if (overlaps)
                continue;
        }
        pageIndex = candidate;
        break;
    }
    if (pageIndex == kRgInvalidPassIndex) {
        const RhiMemoryHandle memory =
            device.allocateTextureMemory(requirements, RhiMemoryCategory::Transient, "RenderGraph.AliasPage");
        if (!memory.isValid())
            return nullptr;
        m_aliasPages.push_back({memory, requirements, {}, 0u});
        pageIndex = static_cast<uint32_t>(m_aliasPages.size() - 1u);
    }

    TransientTexture* member = nullptr;
    for (TransientTexture& candidate : m_transientTextures) {
        if (candidate.pageIndex == pageIndex && sameTextureDesc(candidate.desc, record.desc)) {
            member = &candidate;
            break;
        }
    }
    if (member == nullptr) {
        RhiTextureDesc desc = record.desc;
        desc.debugName = record.name.c_str();
        const RhiTextureHandle texture = device.createPlacedTexture(desc, m_aliasPages[pageIndex].memory);
        if (!texture.isValid())
            return nullptr;
        RhiTextureViewDesc viewDesc;
        viewDesc.texture = texture;
        viewDesc.viewType = defaultViewType(desc.dimension);
        viewDesc.format = desc.format;
        viewDesc.mipCount = desc.mipLevels;
        viewDesc.layerCount = desc.depthOrLayers;
        const RhiTextureViewHandle view = device.createTextureView(viewDesc);
        if (!view.isValid()) {
            device.destroyTexture(texture);
            return nullptr;
        }
        const uint32_t subresourceCount = record.desc.mipLevels * textureSubresourceLayerCount(record.desc);
        m_transientTextures.push_back({record.desc, texture, view,
                                       std::vector<RhiResourceState>(subresourceCount, RhiResourceState::Undefined),
                                       std::vector<RhiQueueType>(subresourceCount, RhiQueueType::Graphics),
                                       std::vector<RhiSubmissionToken>(subresourceCount), 0u, pageIndex});
        member = &m_transientTextures.back();
    }

    AliasPage& page = m_aliasPages[pageIndex];
    if (page.claimsGeneration != m_generation) {
        page.frameClaims.clear();
        page.claimsGeneration = m_generation;
        m_transientMemoryStats.aliasedPageBytes += page.requirements.sizeBytes;
    }
    page.frameClaims.push_back({lifetime.firstPass, lifetime.lastPass});
    // The page's memory may have been written by another member since this
    // texture's last claim; every claim therefore treats the image as freshly
    // uninitialized, and the first barrier performs a discarding Undefined
    // transition (the backend gives it a full execution dependency).
    std::fill(member->states.begin(), member->states.end(), RhiResourceState::Undefined);
    return member;
}

RgExecuteResult RenderGraph::execute(RhiDevice& device, RhiCommandListPool& commandListPool) {
    const auto executeStart = std::chrono::steady_clock::now();
    if (!m_compiled) {
        return {RgExecuteError::NotCompiled, "render graph must compile before execution", {}};
    }
    if (m_runtimeDevice != nullptr && m_runtimeDevice != &device) {
        return {RgExecuteError::DeviceMismatch, "render graph transient resources belong to another RHI device", {}};
    }
    m_runtimeDevice = &device;

    TimingSlot* timingSlot = nullptr;
    if (!m_compiledPasses.empty()) {
        if (m_compiledPasses.size() > std::numeric_limits<uint32_t>::max() / 2u) {
            return {RgExecuteError::TimestampQueryFailed,
                    "render graph contains too many passes for timestamp queries",
                    {}};
        }
        for (const RgCompiledPass& pass : m_compiledPasses) {
            if (!queueSupportsTimestamps(device.capabilities(), pass.queue)) {
                return {RgExecuteError::TimestampUnsupported,
                        "render graph pass queue does not support timestamp queries",
                        {}};
            }
        }
        if (pollTimings(device) == RgTimingPollResult::Error) {
            return {RgExecuteError::TimestampQueryFailed, "failed to collect completed render graph timestamps", {}};
        }
        const uint32_t requiredQueries = static_cast<uint32_t>(m_compiledPasses.size()) * 2u;
        for (TimingSlot& slot : m_timingSlots) {
            if (!slot.pending && slot.queryCapacity >= requiredQueries) {
                timingSlot = &slot;
                break;
            }
        }
        if (timingSlot == nullptr) {
            for (TimingSlot& slot : m_timingSlots) {
                if (!slot.pending) {
                    timingSlot = &slot;
                    break;
                }
            }
        }
        if (timingSlot == nullptr) {
            m_timingSlots.emplace_back();
            timingSlot = &m_timingSlots.back();
        }
        if (timingSlot->queryCapacity < requiredQueries) {
            if (timingSlot->queryPool.isValid())
                device.destroyQueryPool(timingSlot->queryPool);
            const uint32_t capacity = queryCapacityFor(requiredQueries);
            timingSlot->queryPool =
                device.createQueryPool({"RenderGraph.PassTimestamps", RhiQueryType::Timestamp, capacity});
            if (!timingSlot->queryPool.isValid()) {
                timingSlot->queryCapacity = 0u;
                return {
                    RgExecuteError::TimestampQueryFailed, "failed to create a render graph timestamp query pool", {}};
            }
            timingSlot->queryCapacity = capacity;
        }
        ++m_execution;
        if (m_execution == 0u)
            ++m_execution;
        timingSlot->execution = m_execution;
        timingSlot->passNames.clear();
        timingSlot->passQueues.clear();
        timingSlot->passNames.reserve(m_compiledPasses.size());
        timingSlot->passQueues.reserve(m_compiledPasses.size());
        for (const RgCompiledPass& pass : m_compiledPasses) {
            timingSlot->passNames.push_back(pass.name);
            timingSlot->passQueues.push_back(pass.queue);
        }
        timingSlot->submissions.clear();
        timingSlot->issuedPassCount = 0u;
        if (!device.resetQueryPool(timingSlot->queryPool, 0u, requiredQueries)) {
            return {RgExecuteError::TimestampQueryFailed, "failed to reset render graph timestamp queries", {}};
        }
    }

    std::vector<RhiTextureHandle> resolvedTextures(m_textures.size());
    std::vector<RhiTextureViewHandle> resolvedTextureViews(m_textures.size());
    std::vector<RhiBufferHandle> resolvedBuffers(m_buffers.size());
    std::vector<uint32_t> transientTextureIndices(m_textures.size(), std::numeric_limits<uint32_t>::max());
    std::vector<uint32_t> transientBufferIndices(m_buffers.size(), std::numeric_limits<uint32_t>::max());

    // Trim pooled transients that no frame graph has claimed for many
    // generations so a temporarily larger graph does not pin its peak memory
    // forever. A 300-generation lag also guarantees the GPU finished with the
    // resource long ago. This must run before the resolution loop below:
    // erasing pool entries would otherwise invalidate the allocation indices
    // stored in transientTextureIndices/transientBufferIndices.
    {
        constexpr uint32_t kTransientTrimGenerationLag = 300u;
        // claimedGeneration 0 marks entries that never carry a claim stamp
        // (placed page members); m_generation wrapping below a stored stamp is
        // treated as fresh rather than risking a bogus huge distance.
        const auto isStale = [this](const uint32_t generation) {
            return generation != 0u && m_generation > generation &&
                   m_generation - generation > kTransientTrimGenerationLag;
        };
        // Alias pages first: placed members never update claimedGeneration, so
        // staleness is judged by the page's claimsGeneration. A trimmed page
        // keeps its slot (pageIndex references in other members stay valid) but
        // is marked dead by zeroed requirements, which can never satisfy a
        // placement request again. Its placed members are destroyed with it.
        for (uint32_t pageIndex = 0u; pageIndex < m_aliasPages.size(); ++pageIndex) {
            AliasPage& page = m_aliasPages[pageIndex];
            if (!page.memory.isValid() || !isStale(page.claimsGeneration))
                continue;
            for (auto it = m_transientTextures.begin(); it != m_transientTextures.end();) {
                if (it->pageIndex == pageIndex) {
                    if (it->view.isValid())
                        device.destroyTextureView(it->view);
                    if (it->texture.isValid())
                        device.destroyTexture(it->texture);
                    it = m_transientTextures.erase(it);
                } else {
                    ++it;
                }
            }
            device.destroyTextureMemory(page.memory);
            page.memory = {};
            page.requirements = {};
            page.frameClaims.clear();
            page.claimsGeneration = 0u;
        }
        for (auto it = m_transientTextures.begin(); it != m_transientTextures.end();) {
            if (it->pageIndex == kRgInvalidPassIndex && isStale(it->claimedGeneration)) {
                if (it->view.isValid())
                    device.destroyTextureView(it->view);
                if (it->texture.isValid())
                    device.destroyTexture(it->texture);
                it = m_transientTextures.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = m_transientBuffers.begin(); it != m_transientBuffers.end();) {
            if (isStale(it->claimedGeneration)) {
                if (it->buffer.isValid())
                    device.destroyBuffer(it->buffer);
                it = m_transientBuffers.erase(it);
            } else {
                ++it;
            }
        }
    }

    const bool aliasingActive = m_textureAliasingEnabled && device.capabilities().textureAliasing;
    m_transientMemoryStats.aliasedRequestBytes = 0u;
    m_transientMemoryStats.aliasedPageBytes = 0u;
    m_transientMemoryStats.aliasedTextureCount = 0u;

    for (uint32_t index = 0u; index < m_textures.size(); ++index) {
        const TextureRecord& record = m_textures[index];
        if (record.imported) {
            resolvedTextures[index] = record.importedHandle;
            resolvedTextureViews[index] = record.importedView;
            continue;
        }
        TransientTexture* allocation = nullptr;
        const bool requiresAliasing = aliasingActive && index < m_textureLifetimes.size() &&
                                      m_textureLifetimes[index].used && m_textureLifetimes[index].graphicsOnly;
        if (requiresAliasing) {
            RhiTextureMemoryRequirements requirements;
            if (!lookupTextureRequirements(device, record.desc, requirements)) {
                return {RgExecuteError::TextureAliasingFailed,
                        "failed to query aliasing requirements for transient texture '" + record.name + "'",
                        {}};
            }
            allocation = resolveAliasedTransient(device, record, m_textureLifetimes[index], requirements);
            if (allocation == nullptr) {
                return {RgExecuteError::TextureAliasingFailed,
                        "failed to allocate aliased transient texture '" + record.name + "'",
                        {}};
            }
            m_transientMemoryStats.aliasedRequestBytes += requirements.sizeBytes;
            ++m_transientMemoryStats.aliasedTextureCount;
        } else {
            for (TransientTexture& candidate : m_transientTextures) {
                if (candidate.pageIndex == kRgInvalidPassIndex && candidate.claimedGeneration != m_generation &&
                    sameTextureDesc(candidate.desc, record.desc)) {
                    allocation = &candidate;
                    candidate.claimedGeneration = m_generation;
                    break;
                }
            }
        }
        if (allocation == nullptr) {
            RhiTextureDesc desc = record.desc;
            desc.debugName = record.name.c_str();
            const RhiTextureHandle texture = device.createTexture(desc, nullptr);
            if (!texture.isValid()) {
                return {RgExecuteError::ResourceCreationFailed,
                        "failed to create transient texture '" + record.name + "'",
                        {}};
            }
            RhiTextureViewDesc viewDesc;
            viewDesc.texture = texture;
            viewDesc.viewType = defaultViewType(desc.dimension);
            viewDesc.format = desc.format;
            viewDesc.mipCount = desc.mipLevels;
            viewDesc.layerCount = desc.depthOrLayers;
            const RhiTextureViewHandle view = device.createTextureView(viewDesc);
            if (!view.isValid()) {
                device.destroyTexture(texture);
                return {RgExecuteError::ResourceCreationFailed,
                        "failed to create transient texture view '" + record.name + "'",
                        {}};
            }
            const uint32_t subresourceCount = record.desc.mipLevels * textureSubresourceLayerCount(record.desc);
            m_transientTextures.push_back({record.desc, texture, view,
                                           std::vector<RhiResourceState>(subresourceCount, RhiResourceState::Undefined),
                                           std::vector<RhiQueueType>(subresourceCount, RhiQueueType::Graphics),
                                           std::vector<RhiSubmissionToken>(subresourceCount), m_generation,
                                           kRgInvalidPassIndex});
            allocation = &m_transientTextures.back();
        }
        resolvedTextures[index] = allocation->texture;
        resolvedTextureViews[index] = allocation->view;
        transientTextureIndices[index] = static_cast<uint32_t>(allocation - m_transientTextures.data());
    }

    m_transientMemoryStats.totalPageBytes = 0u;
    m_transientMemoryStats.pageCount = 0u;
    for (const AliasPage& page : m_aliasPages) {
        // Trimmed page slots stay in the vector as dead markers; only live
        // pages count toward the memory statistics.
        if (!page.memory.isValid())
            continue;
        ++m_transientMemoryStats.pageCount;
        m_transientMemoryStats.totalPageBytes += page.requirements.sizeBytes;
    }

    for (uint32_t index = 0u; index < m_buffers.size(); ++index) {
        const BufferRecord& record = m_buffers[index];
        if (record.imported) {
            resolvedBuffers[index] = record.importedHandle;
            continue;
        }
        TransientBuffer* allocation = nullptr;
        for (TransientBuffer& candidate : m_transientBuffers) {
            if (candidate.claimedGeneration != m_generation && sameBufferDesc(candidate.desc, record.desc)) {
                allocation = &candidate;
                break;
            }
        }
        if (allocation == nullptr) {
            RhiBufferDesc desc = record.desc;
            desc.debugName = record.name.c_str();
            desc.initialState = RhiResourceState::Undefined;
            const RhiBufferHandle buffer = device.createBuffer(desc, nullptr, 0u);
            if (!buffer.isValid()) {
                return {RgExecuteError::ResourceCreationFailed,
                        "failed to create transient buffer '" + record.name + "'",
                        {}};
            }
            m_transientBuffers.push_back(
                {record.desc, buffer, RhiResourceState::Undefined, RhiQueueType::Graphics, {}, m_generation});
            allocation = &m_transientBuffers.back();
        } else {
            allocation->claimedGeneration = m_generation;
        }
        resolvedBuffers[index] = allocation->buffer;
        transientBufferIndices[index] = static_cast<uint32_t>(allocation - m_transientBuffers.data());
    }

    // Expose the resolved transient handles for the pre-record hook and pass
    // callbacks; cleared on every exit path from this function.
    struct ResolvedScope {
        RenderGraph& graph;
        ~ResolvedScope() {
            graph.m_currentResolvedTextures = nullptr;
            graph.m_currentResolvedTextureViews = nullptr;
        }
    } resolvedScope{*this};
    m_currentResolvedTextures = &resolvedTextures;
    m_currentResolvedTextureViews = &resolvedTextureViews;
    if (m_preRecordCallback) {
        m_preRecordCallback();
    }

    // Indexed by submission batch so worker-recorded lists land in submission
    // order regardless of recording order.
    std::vector<RhiCommandList*> commandLists(m_submissionBatches.size(), nullptr);
    const RhiCapabilities& capabilities = device.capabilities();
    struct RuntimeResourceState {
        RhiResourceState state = RhiResourceState::Undefined;
        RhiQueueType queue = RhiQueueType::Graphics;
        RhiSubmissionToken lastUse;
    };
    const auto textureRuntimeState = [&](const RgTextureBarrierPlan& barrier) {
        RuntimeResourceState state{barrier.oldState, barrier.sourceQueue, {}};
        const uint32_t textureIndex = barrier.texture.index - 1u;
        const uint32_t allocationIndex = transientTextureIndices[textureIndex];
        if (allocationIndex == std::numeric_limits<uint32_t>::max())
            return state;
        const TransientTexture& allocation = m_transientTextures[allocationIndex];
        const uint32_t layerCount = textureSubresourceLayerCount(allocation.desc);
        const uint32_t subresource = barrier.range.baseMip * layerCount + barrier.range.baseLayer;
        state.state = allocation.states[subresource];
        state.queue = allocation.queues[subresource];
        state.lastUse = allocation.lastUses[subresource];
        return state;
    };
    const auto bufferRuntimeState = [&](const RgBufferBarrierPlan& barrier) {
        RuntimeResourceState state{barrier.oldState, barrier.sourceQueue, {}};
        const uint32_t bufferIndex = barrier.buffer.index - 1u;
        const uint32_t allocationIndex = transientBufferIndices[bufferIndex];
        if (allocationIndex == std::numeric_limits<uint32_t>::max())
            return state;
        const TransientBuffer& allocation = m_transientBuffers[allocationIndex];
        state.state = allocation.state;
        state.queue = allocation.queue;
        state.lastUse = allocation.lastUse;
        return state;
    };
    // Barrier resolution consumes and updates the live transient subresource
    // states, so it must run serially in declaration order on the calling
    // thread. Recording replays the pre-resolved barriers and can therefore be
    // distributed across worker threads without touching shared graph state.
    const auto resolveTextureBarrier = [&](const RgTextureBarrierPlan& barrier,
                                           const RhiBarrierPhase requestedPhase) -> RhiTextureBarrier {
        const uint32_t textureIndex = barrier.texture.index - 1u;
        const RuntimeResourceState runtime = textureRuntimeState(barrier);
        TransientTexture* allocation = nullptr;
        const uint32_t allocationIndex = transientTextureIndices[textureIndex];
        if (allocationIndex != std::numeric_limits<uint32_t>::max()) {
            allocation = &m_transientTextures[allocationIndex];
        }
        const uint32_t sourceFamily = queueFamily(capabilities, runtime.queue);
        const uint32_t destinationFamily = queueFamily(capabilities, barrier.destinationQueue);
        const bool concurrentQueues =
            textureQueuesShareConcurrently(m_textures[textureIndex].desc, runtime.queue, barrier.destinationQueue);
        const bool ownershipTransfer = runtime.state != RhiResourceState::Undefined &&
                                       runtime.queue != barrier.destinationQueue && sourceFamily != destinationFamily &&
                                       !concurrentQueues;
        const bool queueTransition =
            runtime.state != RhiResourceState::Undefined && runtime.queue != barrier.destinationQueue;
        const RhiBarrierPhase phase = (requestedPhase == RhiBarrierPhase::Acquire && queueTransition) ||
                                              (requestedPhase == RhiBarrierPhase::Release && ownershipTransfer)
                                          ? requestedPhase
                                          : RhiBarrierPhase::Full;
        if (allocation != nullptr && phase != RhiBarrierPhase::Release) {
            const uint32_t layerCount = textureSubresourceLayerCount(allocation->desc);
            const uint32_t subresource = barrier.range.baseMip * layerCount + barrier.range.baseLayer;
            allocation->states[subresource] = barrier.newState;
            allocation->queues[subresource] = barrier.destinationQueue;
        }
        return {resolvedTextures[textureIndex],
                runtime.state,
                barrier.newState,
                barrier.range.baseMip,
                barrier.range.mipCount,
                barrier.range.baseLayer,
                barrier.range.layerCount,
                barrier.range.aspect,
                ownershipTransfer ? sourceFamily : kRhiQueueFamilyIgnored,
                ownershipTransfer ? destinationFamily : kRhiQueueFamilyIgnored,
                phase};
    };
    const auto resolveBufferBarrier = [&](const RgBufferBarrierPlan& barrier,
                                          const RhiBarrierPhase requestedPhase) -> RhiBufferBarrier {
        const uint32_t bufferIndex = barrier.buffer.index - 1u;
        const RuntimeResourceState runtime = bufferRuntimeState(barrier);
        TransientBuffer* allocation = nullptr;
        const uint32_t allocationIndex = transientBufferIndices[bufferIndex];
        if (allocationIndex != std::numeric_limits<uint32_t>::max()) {
            allocation = &m_transientBuffers[allocationIndex];
        }
        const uint32_t sourceFamily = queueFamily(capabilities, runtime.queue);
        const uint32_t destinationFamily = queueFamily(capabilities, barrier.destinationQueue);
        const bool ownershipTransfer = runtime.state != RhiResourceState::Undefined &&
                                       runtime.queue != barrier.destinationQueue && sourceFamily != destinationFamily;
        const bool queueTransition =
            runtime.state != RhiResourceState::Undefined && runtime.queue != barrier.destinationQueue;
        const RhiBarrierPhase phase = (requestedPhase == RhiBarrierPhase::Acquire && queueTransition) ||
                                              (requestedPhase == RhiBarrierPhase::Release && ownershipTransfer)
                                          ? requestedPhase
                                          : RhiBarrierPhase::Full;
        if (allocation != nullptr && phase != RhiBarrierPhase::Release) {
            allocation->state = barrier.newState;
            allocation->queue = barrier.destinationQueue;
        }
        return {resolvedBuffers[bufferIndex],
                runtime.state,
                barrier.newState,
                barrier.range.offset,
                barrier.range.size,
                ownershipTransfer ? sourceFamily : kRhiQueueFamilyIgnored,
                ownershipTransfer ? destinationFamily : kRhiQueueFamilyIgnored,
                phase};
    };
    const auto recordTextureBarrier = [&](RhiCommandList& commandList, const RgTextureBarrierPlan& barrier,
                                          const RhiBarrierPhase requestedPhase) {
        commandList.textureBarrier(resolveTextureBarrier(barrier, requestedPhase));
    };
    const auto recordBufferBarrier = [&](RhiCommandList& commandList, const RgBufferBarrierPlan& barrier,
                                         const RhiBarrierPhase requestedPhase) {
        commandList.bufferBarrier(resolveBufferBarrier(barrier, requestedPhase));
    };

    struct TransitionBatch {
        RhiQueueType queue = RhiQueueType::Graphics;
        std::vector<RgTextureBarrierPlan> textureBarriers;
        std::vector<RgBufferBarrierPlan> bufferBarriers;
        std::vector<uint32_t> relatedBatches;
        RhiCommandList* commandList = nullptr;
        RhiSubmissionToken token;
    };
    std::vector<TransitionBatch> prologueBatches;
    std::vector<std::vector<uint32_t>> batchPrologueDependencies(m_submissionBatches.size());
    const auto prologueBatchForQueue = [&](const RhiQueueType queue) {
        for (uint32_t index = 0u; index < prologueBatches.size(); ++index) {
            if (prologueBatches[index].queue == queue)
                return index;
        }
        prologueBatches.emplace_back();
        prologueBatches.back().queue = queue;
        return static_cast<uint32_t>(prologueBatches.size() - 1u);
    };
    const auto registerPrologueDependency = [&](TransitionBatch& prologue, const uint32_t prologueIndex,
                                                const uint32_t destinationBatch) {
        if (std::find(prologue.relatedBatches.begin(), prologue.relatedBatches.end(), destinationBatch) ==
            prologue.relatedBatches.end()) {
            prologue.relatedBatches.push_back(destinationBatch);
        }
        auto& dependencies = batchPrologueDependencies[destinationBatch];
        if (std::find(dependencies.begin(), dependencies.end(), prologueIndex) == dependencies.end()) {
            dependencies.push_back(prologueIndex);
        }
    };

    for (uint32_t passIndex = 0u; passIndex < m_compiledPasses.size(); ++passIndex) {
        const uint32_t destinationBatch = m_passToBatch[passIndex];
        const RgCompiledPass& pass = m_compiledPasses[passIndex];
        for (const RgTextureBarrierPlan& barrier : pass.textureBarriers) {
            if (barrier.sourcePass != kRgInvalidPassIndex)
                continue;
            const RuntimeResourceState runtime = textureRuntimeState(barrier);
            if (runtime.state == RhiResourceState::Undefined || runtime.queue == barrier.destinationQueue)
                continue;
            if (runtime.lastUse.isValid() && runtime.lastUse.queue != runtime.queue) {
                return {
                    RgExecuteError::SubmissionFailed, "transient texture state does not match its last submission", {}};
            }
            const uint32_t prologueIndex = prologueBatchForQueue(runtime.queue);
            TransitionBatch& prologue = prologueBatches[prologueIndex];
            registerPrologueDependency(prologue, prologueIndex, destinationBatch);
            if (queueFamily(capabilities, runtime.queue) != queueFamily(capabilities, barrier.destinationQueue)) {
                RgTextureBarrierPlan runtimeBarrier = barrier;
                runtimeBarrier.oldState = runtime.state;
                runtimeBarrier.sourceQueue = runtime.queue;
                prologue.textureBarriers.push_back(runtimeBarrier);
            }
        }
        for (const RgBufferBarrierPlan& barrier : pass.bufferBarriers) {
            if (barrier.sourcePass != kRgInvalidPassIndex)
                continue;
            const RuntimeResourceState runtime = bufferRuntimeState(barrier);
            if (runtime.state == RhiResourceState::Undefined || runtime.queue == barrier.destinationQueue)
                continue;
            if (runtime.lastUse.isValid() && runtime.lastUse.queue != runtime.queue) {
                return {
                    RgExecuteError::SubmissionFailed, "transient buffer state does not match its last submission", {}};
            }
            const uint32_t prologueIndex = prologueBatchForQueue(runtime.queue);
            TransitionBatch& prologue = prologueBatches[prologueIndex];
            registerPrologueDependency(prologue, prologueIndex, destinationBatch);
            if (queueFamily(capabilities, runtime.queue) != queueFamily(capabilities, barrier.destinationQueue)) {
                RgBufferBarrierPlan runtimeBarrier = barrier;
                runtimeBarrier.oldState = runtime.state;
                runtimeBarrier.sourceQueue = runtime.queue;
                prologue.bufferBarriers.push_back(runtimeBarrier);
            }
        }
    }

    for (TransitionBatch& prologue : prologueBatches) {
        const RhiCommandListType listType = commandListType(prologue.queue);
        prologue.commandList = commandListPool.acquire(listType);
        if (prologue.commandList == nullptr || !prologue.commandList->begin({"RenderGraph.Prologue", listType})) {
            return {RgExecuteError::CommandListUnavailable, "failed to begin a render graph prologue command list", {}};
        }
        for (const RgTextureBarrierPlan& barrier : prologue.textureBarriers)
            recordTextureBarrier(*prologue.commandList, barrier, RhiBarrierPhase::Release);
        for (const RgBufferBarrierPlan& barrier : prologue.bufferBarriers)
            recordBufferBarrier(*prologue.commandList, barrier, RhiBarrierPhase::Release);
        if (!prologue.commandList->end()) {
            return {RgExecuteError::CommandRecordingFailed, "failed to end a render graph prologue command list", {}};
        }
    }

    // Phase 1: resolve every batch's barriers serially. This keeps the
    // transient state bookkeeping identical to the previous fully serial
    // recording while making the per-batch recording bodies read-only.
    struct PassRecordPlan {
        uint32_t passIndex = 0u;
        std::vector<RhiTextureBarrier> acquireTextureBarriers;
        std::vector<RhiBufferBarrier> acquireBufferBarriers;
        /// Post-pass barriers: cross-family releases followed by same-queue
        /// epilogue transitions, in the order the serial recorder used.
        std::vector<RhiTextureBarrier> postTextureBarriers;
        std::vector<RhiBufferBarrier> postBufferBarriers;
    };
    struct BatchRecordPlan {
        std::vector<PassRecordPlan> passes;
        bool threadSafe = true;
    };
    std::vector<BatchRecordPlan> batchPlans(m_submissionBatches.size());
    for (uint32_t batchIndex = 0u; batchIndex < m_submissionBatches.size(); ++batchIndex) {
        const RgSubmissionBatchPlan& batch = m_submissionBatches[batchIndex];
        BatchRecordPlan& plan = batchPlans[batchIndex];
        plan.passes.reserve(batch.passes.size());
        for (const uint32_t passIndex : batch.passes) {
            const RgCompiledPass& compiledPass = m_compiledPasses[passIndex];
            plan.threadSafe = plan.threadSafe && compiledPass.threadSafeRecord;
            PassRecordPlan passPlan;
            passPlan.passIndex = passIndex;
            for (const RgTextureBarrierPlan& barrier : compiledPass.textureBarriers) {
                passPlan.acquireTextureBarriers.push_back(resolveTextureBarrier(barrier, RhiBarrierPhase::Acquire));
            }
            for (const RgBufferBarrierPlan& barrier : compiledPass.bufferBarriers) {
                passPlan.acquireBufferBarriers.push_back(resolveBufferBarrier(barrier, RhiBarrierPhase::Acquire));
            }
            for (const RgTextureBarrierPlan& barrier : compiledPass.releaseTextureBarriers) {
                if (queueFamily(capabilities, barrier.sourceQueue) !=
                    queueFamily(capabilities, barrier.destinationQueue)) {
                    passPlan.postTextureBarriers.push_back(resolveTextureBarrier(barrier, RhiBarrierPhase::Release));
                }
            }
            for (const RgBufferBarrierPlan& barrier : compiledPass.releaseBufferBarriers) {
                if (queueFamily(capabilities, barrier.sourceQueue) !=
                    queueFamily(capabilities, barrier.destinationQueue)) {
                    passPlan.postBufferBarriers.push_back(resolveBufferBarrier(barrier, RhiBarrierPhase::Release));
                }
            }
            for (const RgTextureBarrierPlan& barrier : m_epilogueTextureBarriers) {
                if (barrier.sourcePass != passIndex || barrier.sourceQueue != barrier.destinationQueue)
                    continue;
                passPlan.postTextureBarriers.push_back(resolveTextureBarrier(barrier, RhiBarrierPhase::Full));
            }
            for (const RgBufferBarrierPlan& barrier : m_epilogueBufferBarriers) {
                if (barrier.sourcePass != passIndex || barrier.sourceQueue != barrier.destinationQueue)
                    continue;
                passPlan.postBufferBarriers.push_back(resolveBufferBarrier(barrier, RhiBarrierPhase::Full));
            }
            plan.passes.push_back(std::move(passPlan));
        }
    }

    // Phase 2: record batch command lists. The body below is shared by the
    // serial path and the worker tasks; it only reads state resolved above.
    const auto recordBatchCommands = [&](const uint32_t batchIndex, RhiCommandListPool& pool, RgExecuteError& error,
                                         std::string& message) -> bool {
        const RgSubmissionBatchPlan& batch = m_submissionBatches[batchIndex];
        const BatchRecordPlan& plan = batchPlans[batchIndex];
        const RhiCommandListType listType = commandListType(batch.queue);
        RhiCommandList* const commandList = pool.acquire(listType);
        if (commandList == nullptr || !commandList->begin({"RenderGraph.Batch", listType})) {
            error = RgExecuteError::CommandListUnavailable;
            message = "failed to begin a render graph command list";
            return false;
        }
        for (const PassRecordPlan& passPlan : plan.passes) {
            const uint32_t passIndex = passPlan.passIndex;
            const RgCompiledPass& compiledPass = m_compiledPasses[passIndex];
            for (const RhiTextureBarrier& barrier : passPlan.acquireTextureBarriers)
                commandList->textureBarrier(barrier);
            for (const RhiBufferBarrier& barrier : passPlan.acquireBufferBarriers)
                commandList->bufferBarrier(barrier);

            const uint32_t firstTimestampQuery = passIndex * 2u;
            commandList->writeTimestamp(timingSlot->queryPool, firstTimestampQuery);

            commandList->beginDebugLabel(compiledPass.name.c_str(), {0.18f, 0.55f, 0.82f, 1.0f});
            PassRecord& pass = m_passes[compiledPass.handle.index - 1u];
            RgPassContext context(*commandList, resolvedTextures, resolvedTextureViews, resolvedBuffers);
            if (!pass.execute(context)) {
                commandList->endDebugLabel();
                static_cast<void>(commandList->end());
                error = RgExecuteError::PassExecutionFailed;
                message = "render graph pass '" + compiledPass.name + "' failed";
                return false;
            }
            commandList->writeTimestamp(timingSlot->queryPool, firstTimestampQuery + 1u);
            commandList->endDebugLabel();

            for (const RhiTextureBarrier& barrier : passPlan.postTextureBarriers)
                commandList->textureBarrier(barrier);
            for (const RhiBufferBarrier& barrier : passPlan.postBufferBarriers)
                commandList->bufferBarrier(barrier);
        }
        if (!commandList->end()) {
            error = RgExecuteError::CommandRecordingFailed;
            message = "failed to end a render graph command list";
            return false;
        }
        commandLists[batchIndex] = commandList;
        return true;
    };

    // Worker-eligible batches are dispatched first so they overlap the main
    // thread's recording of the remaining batches. GL backends require the
    // device thread for all recording, so threading is Vulkan-only.
    const bool workerRecordingEnabled = m_recordThreadPool != nullptr && m_recordThreadPool->isRunning() &&
                                        m_recordThreadPool->numWorkers() > 0 && device.backend() == RhiBackend::Vulkan;
    std::vector<uint8_t> recordedByWorker(m_submissionBatches.size(), 0u);
    std::atomic<uint32_t> pendingWorkerBatches{0u};
    std::mutex workerMutex;
    std::condition_variable workerCondition;
    std::atomic<bool> workerFailed{false};
    RgExecuteError workerError = RgExecuteError::None;
    std::string workerErrorMessage;
    uint32_t workerBatchCount = 0u;
    if (workerRecordingEnabled) {
        for (uint32_t batchIndex = 0u; batchIndex < m_submissionBatches.size(); ++batchIndex) {
            if (!batchPlans[batchIndex].threadSafe)
                continue;
            recordedByWorker[batchIndex] = 1u;
            ++workerBatchCount;
            pendingWorkerBatches.fetch_add(1u, std::memory_order_relaxed);
            m_recordThreadPool->submit(
                [&, batchIndex]() {
                    RgExecuteError error = RgExecuteError::None;
                    std::string message;
                    RhiCommandListPool* const workerPool = acquireWorkerCommandListPool(device);
                    bool recorded = false;
                    if (workerPool == nullptr) {
                        error = RgExecuteError::CommandListUnavailable;
                        message = "failed to create a worker command list pool";
                    } else {
                        recorded = recordBatchCommands(batchIndex, *workerPool, error, message);
                    }
                    if (!recorded && !workerFailed.exchange(true, std::memory_order_acq_rel)) {
                        const std::lock_guard<std::mutex> lock(workerMutex);
                        workerError = error;
                        workerErrorMessage = std::move(message);
                    }
                    {
                        const std::lock_guard<std::mutex> lock(workerMutex);
                        pendingWorkerBatches.fetch_sub(1u, std::memory_order_acq_rel);
                    }
                    workerCondition.notify_all();
                },
                0);
        }
    }

    bool mainRecordingSucceeded = true;
    RgExecuteError mainError = RgExecuteError::None;
    std::string mainErrorMessage;
    for (uint32_t batchIndex = 0u; batchIndex < m_submissionBatches.size(); ++batchIndex) {
        if (recordedByWorker[batchIndex] != 0u)
            continue;
        if (!recordBatchCommands(batchIndex, commandListPool, mainError, mainErrorMessage)) {
            mainRecordingSucceeded = false;
            break;
        }
    }
    // Always join outstanding workers before leaving this scope: their tasks
    // reference stack state owned by this execute call.
    if (workerBatchCount != 0u) {
        std::unique_lock<std::mutex> lock(workerMutex);
        workerCondition.wait(lock, [&]() { return pendingWorkerBatches.load(std::memory_order_acquire) == 0u; });
    }
    if (!mainRecordingSucceeded) {
        return {mainError, std::move(mainErrorMessage), {}};
    }
    if (workerFailed.load(std::memory_order_acquire)) {
        return {workerError, std::move(workerErrorMessage), {}};
    }

    std::vector<TransitionBatch> epilogueBatches;
    const auto epilogueBatchForQueue = [&](const RhiQueueType queue) {
        for (uint32_t index = 0u; index < epilogueBatches.size(); ++index) {
            if (epilogueBatches[index].queue == queue)
                return index;
        }
        epilogueBatches.emplace_back();
        epilogueBatches.back().queue = queue;
        return static_cast<uint32_t>(epilogueBatches.size() - 1u);
    };
    for (const RgTextureBarrierPlan& barrier : m_epilogueTextureBarriers) {
        if (barrier.sourceQueue == barrier.destinationQueue)
            continue;
        const uint32_t epilogueIndex = epilogueBatchForQueue(barrier.destinationQueue);
        TransitionBatch& epilogue = epilogueBatches[epilogueIndex];
        epilogue.textureBarriers.push_back(barrier);
        const uint32_t sourceBatch = m_passToBatch[barrier.sourcePass];
        if (std::find(epilogue.relatedBatches.begin(), epilogue.relatedBatches.end(), sourceBatch) ==
            epilogue.relatedBatches.end()) {
            epilogue.relatedBatches.push_back(sourceBatch);
        }
    }
    for (const RgBufferBarrierPlan& barrier : m_epilogueBufferBarriers) {
        if (barrier.sourceQueue == barrier.destinationQueue)
            continue;
        const uint32_t epilogueIndex = epilogueBatchForQueue(barrier.destinationQueue);
        TransitionBatch& epilogue = epilogueBatches[epilogueIndex];
        epilogue.bufferBarriers.push_back(barrier);
        const uint32_t sourceBatch = m_passToBatch[barrier.sourcePass];
        if (std::find(epilogue.relatedBatches.begin(), epilogue.relatedBatches.end(), sourceBatch) ==
            epilogue.relatedBatches.end()) {
            epilogue.relatedBatches.push_back(sourceBatch);
        }
    }
    for (TransitionBatch& epilogue : epilogueBatches) {
        const RhiCommandListType listType = commandListType(epilogue.queue);
        epilogue.commandList = commandListPool.acquire(listType);
        if (epilogue.commandList == nullptr || !epilogue.commandList->begin({"RenderGraph.Epilogue", listType})) {
            return {RgExecuteError::CommandListUnavailable, "failed to begin a render graph epilogue command list", {}};
        }
        for (const RgTextureBarrierPlan& barrier : epilogue.textureBarriers)
            recordTextureBarrier(*epilogue.commandList, barrier, RhiBarrierPhase::Acquire);
        for (const RgBufferBarrierPlan& barrier : epilogue.bufferBarriers)
            recordBufferBarrier(*epilogue.commandList, barrier, RhiBarrierPhase::Acquire);
        if (!epilogue.commandList->end()) {
            return {RgExecuteError::CommandRecordingFailed, "failed to end a render graph epilogue command list", {}};
        }
    }

    const auto submitStart = std::chrono::steady_clock::now();
    RgExecuteResult result;
    result.recordMilliseconds = std::chrono::duration<double, std::milli>(submitStart - executeStart).count();
    result.workerRecordedBatchCount = workerBatchCount;
    result.submissions.reserve(prologueBatches.size() + m_submissionBatches.size() + epilogueBatches.size());
    for (TransitionBatch& prologue : prologueBatches) {
        RhiCommandList* submitted[] = {prologue.commandList};
        if (!device.submit({"RenderGraph.PrologueSubmit", submitted, 1u, prologue.queue, nullptr, 0u},
                           &prologue.token)) {
            return {RgExecuteError::SubmissionFailed, "RHI rejected a render graph prologue submission",
                    result.submissions};
        }
        result.submissions.push_back(prologue.token);
    }

    // Coalesce consecutive same-queue batches into one submission. Queue
    // submission order already serializes same-queue work, and every pass
    // records its own acquire barriers, so timeline waits are only required
    // when a dependency was submitted to a different queue. This collapses a
    // single-queue frame into one submit and removes the GPU idle bubbles
    // that per-batch semaphore chains introduce at batch boundaries.
    std::vector<RhiSubmissionToken> batchTokens(m_submissionBatches.size());
    // A submission-level wait blocks every command in the submit, so a batch
    // that depends on another queue must START its own group: coalescing it
    // into the running group would stall earlier same-queue batches that have
    // no such dependency and erase any cross-queue overlap.
    const auto batchNeedsCrossQueueWait = [&](const uint32_t batchIndex, const RhiQueueType queue) {
        const RgSubmissionBatchPlan& batch = m_submissionBatches[batchIndex];
        for (const uint32_t dependency : batch.dependencies) {
            if (dependency < m_submissionBatches.size() && m_submissionBatches[dependency].queue != queue) {
                return true;
            }
        }
        for (const uint32_t dependency : batchPrologueDependencies[batchIndex]) {
            if (dependency < prologueBatches.size() && prologueBatches[dependency].queue != queue) {
                return true;
            }
        }
        return false;
    };
    uint32_t groupBegin = 0u;
    while (groupBegin < m_submissionBatches.size()) {
        const RhiQueueType groupQueue = m_submissionBatches[groupBegin].queue;
        uint32_t groupEnd = groupBegin + 1u;
        while (groupEnd < m_submissionBatches.size() && m_submissionBatches[groupEnd].queue == groupQueue &&
               !batchNeedsCrossQueueWait(groupEnd, groupQueue)) {
            ++groupEnd;
        }

        std::vector<RhiQueueDependency> waits;
        const auto appendWait = [&](const RhiSubmissionToken token) {
            if (std::find_if(waits.begin(), waits.end(), [token](const RhiQueueDependency& wait) {
                    return wait.token.deviceId == token.deviceId && wait.token.sequence == token.sequence;
                }) == waits.end()) {
                waits.push_back({token, 0u});
            }
        };
        std::vector<RhiCommandList*> submitted;
        submitted.reserve(groupEnd - groupBegin);
        uint32_t issuedPassCount = 0u;
        for (uint32_t batchIndex = groupBegin; batchIndex < groupEnd; ++batchIndex) {
            const RgSubmissionBatchPlan& batch = m_submissionBatches[batchIndex];
            for (const uint32_t dependency : batch.dependencies) {
                if (dependency >= batchIndex) {
                    return {RgExecuteError::SubmissionFailed, "render graph submission dependency is not available",
                            result.submissions};
                }
                if (m_submissionBatches[dependency].queue == groupQueue) {
                    continue;
                }
                if (!batchTokens[dependency].isValid()) {
                    return {RgExecuteError::SubmissionFailed, "render graph submission dependency is not available",
                            result.submissions};
                }
                appendWait(batchTokens[dependency]);
            }
            for (const uint32_t dependency : batchPrologueDependencies[batchIndex]) {
                if (dependency >= prologueBatches.size() || !prologueBatches[dependency].token.isValid()) {
                    return {RgExecuteError::SubmissionFailed, "render graph prologue dependency is not available",
                            result.submissions};
                }
                if (prologueBatches[dependency].queue == groupQueue) {
                    continue;
                }
                appendWait(prologueBatches[dependency].token);
            }
            submitted.push_back(commandLists[batchIndex]);
            issuedPassCount = std::max(issuedPassCount, batch.passes.back() + 1u);
        }

        RhiSubmissionToken token;
        if (!device.submit({"RenderGraph.Submit", submitted.data(), static_cast<uint32_t>(submitted.size()), groupQueue,
                            waits.empty() ? nullptr : waits.data(), static_cast<uint32_t>(waits.size())},
                           &token)) {
            return {RgExecuteError::SubmissionFailed, "RHI rejected a render graph submission", result.submissions};
        }
        for (uint32_t batchIndex = groupBegin; batchIndex < groupEnd; ++batchIndex) {
            batchTokens[batchIndex] = token;
        }
        result.submissions.push_back(token);
        timingSlot->pending = true;
        timingSlot->issuedPassCount = std::max(timingSlot->issuedPassCount, issuedPassCount);
        timingSlot->submissions = result.submissions;
        groupBegin = groupEnd;
    }

    for (TransitionBatch& epilogue : epilogueBatches) {
        std::vector<RhiQueueDependency> waits;
        waits.reserve(epilogue.relatedBatches.size());
        for (const uint32_t dependency : epilogue.relatedBatches) {
            if (dependency >= batchTokens.size() || !batchTokens[dependency].isValid()) {
                return {RgExecuteError::SubmissionFailed, "render graph epilogue dependency is not available",
                        result.submissions};
            }
            waits.push_back({batchTokens[dependency], 0u});
        }
        RhiCommandList* submitted[] = {epilogue.commandList};
        if (!device.submit({"RenderGraph.EpilogueSubmit", submitted, 1u, epilogue.queue,
                            waits.empty() ? nullptr : waits.data(), static_cast<uint32_t>(waits.size())},
                           &epilogue.token)) {
            return {RgExecuteError::SubmissionFailed, "RHI rejected a render graph epilogue submission",
                    result.submissions};
        }
        result.submissions.push_back(epilogue.token);
        timingSlot->submissions = result.submissions;
    }

    for (uint32_t textureIndex = 0u; textureIndex < m_textures.size(); ++textureIndex) {
        const uint32_t allocationIndex = transientTextureIndices[textureIndex];
        if (allocationIndex == std::numeric_limits<uint32_t>::max())
            continue;
        TransientTexture& allocation = m_transientTextures[allocationIndex];
        for (uint32_t subresource = 0u; subresource < m_textureLastPasses[textureIndex].size(); ++subresource) {
            const uint32_t lastPass = m_textureLastPasses[textureIndex][subresource];
            if (lastPass != kRgInvalidPassIndex) {
                allocation.lastUses[subresource] = batchTokens[m_passToBatch[lastPass]];
            }
        }
    }
    for (uint32_t bufferIndex = 0u; bufferIndex < m_buffers.size(); ++bufferIndex) {
        const uint32_t allocationIndex = transientBufferIndices[bufferIndex];
        if (allocationIndex == std::numeric_limits<uint32_t>::max() ||
            m_bufferLastPasses[bufferIndex] == kRgInvalidPassIndex)
            continue;
        m_transientBuffers[allocationIndex].lastUse = batchTokens[m_passToBatch[m_bufferLastPasses[bufferIndex]]];
    }
    const auto setTextureLastUse = [&](const RgTextureBarrierPlan& barrier, const RhiSubmissionToken token) {
        const uint32_t textureIndex = barrier.texture.index - 1u;
        const uint32_t allocationIndex = transientTextureIndices[textureIndex];
        if (allocationIndex == std::numeric_limits<uint32_t>::max())
            return;
        TransientTexture& allocation = m_transientTextures[allocationIndex];
        const uint32_t layerCount = textureSubresourceLayerCount(allocation.desc);
        const uint32_t subresource = barrier.range.baseMip * layerCount + barrier.range.baseLayer;
        allocation.lastUses[subresource] = token;
    };
    const auto setBufferLastUse = [&](const RgBufferBarrierPlan& barrier, const RhiSubmissionToken token) {
        const uint32_t bufferIndex = barrier.buffer.index - 1u;
        const uint32_t allocationIndex = transientBufferIndices[bufferIndex];
        if (allocationIndex != std::numeric_limits<uint32_t>::max())
            m_transientBuffers[allocationIndex].lastUse = token;
    };
    for (const RgTextureBarrierPlan& barrier : m_epilogueTextureBarriers) {
        if (barrier.sourceQueue == barrier.destinationQueue) {
            setTextureLastUse(barrier, batchTokens[m_passToBatch[barrier.sourcePass]]);
        }
    }
    for (const RgBufferBarrierPlan& barrier : m_epilogueBufferBarriers) {
        if (barrier.sourceQueue == barrier.destinationQueue) {
            setBufferLastUse(barrier, batchTokens[m_passToBatch[barrier.sourcePass]]);
        }
    }
    for (const TransitionBatch& epilogue : epilogueBatches) {
        for (const RgTextureBarrierPlan& barrier : epilogue.textureBarriers)
            setTextureLastUse(barrier, epilogue.token);
        for (const RgBufferBarrierPlan& barrier : epilogue.bufferBarriers)
            setBufferLastUse(barrier, epilogue.token);
    }
    result.submitMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submitStart).count();
    return result;
}

RhiCommandListPool* RenderGraph::acquireWorkerCommandListPool(RhiDevice& device) {
    const std::thread::id thread = std::this_thread::get_id();
    {
        const std::lock_guard<std::mutex> lock(m_workerPoolMutex);
        const auto existing = m_workerCommandListPools.find(thread);
        if (existing != m_workerCommandListPools.end())
            return existing->second.get();
    }
    // Create outside the map lock: backend pools bind their owner thread at
    // construction, which is exactly the calling worker thread here.
    RhiCommandListPoolDesc desc;
    desc.debugName = "RenderGraph.WorkerCommandListPool";
    std::unique_ptr<RhiCommandListPool> pool = device.createCommandListPool(desc);
    if (pool == nullptr)
        return nullptr;
    RhiCommandListPool* const created = pool.get();
    const std::lock_guard<std::mutex> lock(m_workerPoolMutex);
    m_workerCommandListPools.emplace(thread, std::move(pool));
    return created;
}

void RenderGraph::destroyWorkerCommandListPools() {
    const std::lock_guard<std::mutex> lock(m_workerPoolMutex);
    m_workerCommandListPools.clear();
}

RgTimingPollResult RenderGraph::pollTimings(RhiDevice& device) {
    if (m_runtimeDevice != nullptr && m_runtimeDevice != &device)
        return RgTimingPollResult::Error;
    bool collected = false;
    for (TimingSlot& slot : m_timingSlots) {
        if (!slot.pending)
            continue;
        bool complete = true;
        for (const RhiSubmissionToken token : slot.submissions) {
            bool submissionComplete = false;
            if (!device.isSubmissionComplete(token, submissionComplete))
                return RgTimingPollResult::Error;
            if (!submissionComplete) {
                complete = false;
                break;
            }
        }
        if (!complete)
            continue;
        const uint32_t queryCount = slot.issuedPassCount * 2u;
        if (queryCount == 0u || !device.areQueryResultsAvailable(slot.queryPool, 0u, queryCount)) {
            continue;
        }
        std::vector<uint64_t> timestamps(queryCount);
        if (!device.getQueryResults(slot.queryPool, 0u, queryCount, timestamps.data())) {
            return RgTimingPollResult::Error;
        }
        RgTimingSnapshot snapshot;
        snapshot.execution = slot.execution;
        snapshot.passes.reserve(slot.issuedPassCount);
        for (uint32_t passIndex = 0u; passIndex < slot.issuedPassCount; ++passIndex) {
            const uint64_t start = timestamps[passIndex * 2u];
            const uint64_t end = timestamps[passIndex * 2u + 1u];
            if (end < start)
                return RgTimingPollResult::Error;
            snapshot.passes.push_back({slot.passNames[passIndex], slot.passQueues[passIndex], end - start, start, end});
        }
        if (snapshot.execution >= m_latestTimings.execution)
            m_latestTimings = std::move(snapshot);
        slot.pending = false;
        slot.execution = 0u;
        slot.passNames.clear();
        slot.passQueues.clear();
        slot.submissions.clear();
        slot.issuedPassCount = 0u;
        collected = true;
    }
    return collected ? RgTimingPollResult::Available : RgTimingPollResult::Unavailable;
}

void RenderGraph::releaseTransientResources(RhiDevice& device) {
    if (m_runtimeDevice != nullptr && m_runtimeDevice != &device)
        return;
    // Worker pools are device objects: destroy them before the device can be
    // shut down. They are recreated lazily if the graph executes again.
    destroyWorkerCommandListPools();
    for (const TransientTexture& texture : m_transientTextures) {
        if (texture.view.isValid())
            device.destroyTextureView(texture.view);
        if (texture.texture.isValid())
            device.destroyTexture(texture.texture);
    }
    for (const TransientBuffer& buffer : m_transientBuffers) {
        if (buffer.buffer.isValid())
            device.destroyBuffer(buffer.buffer);
    }
    for (const TimingSlot& slot : m_timingSlots) {
        if (slot.queryPool.isValid())
            device.destroyQueryPool(slot.queryPool);
    }
    // Placed member textures were destroyed above; their shared pages follow.
    for (const AliasPage& page : m_aliasPages) {
        if (page.memory.isValid())
            device.destroyTextureMemory(page.memory);
    }
    m_transientTextures.clear();
    m_transientBuffers.clear();
    m_aliasPages.clear();
    m_textureRequirementsCache.clear();
    m_transientMemoryStats = {};
    m_timingSlots.clear();
    m_latestTimings = {};
    m_execution = 0u;
    m_runtimeDevice = nullptr;
}

void RenderGraph::reset() {
    m_textures.clear();
    m_buffers.clear();
    m_passes.clear();
    m_compiledPasses.clear();
    m_textureLifetimes.clear();
    m_bufferLifetimes.clear();
    m_epilogueTextureBarriers.clear();
    m_epilogueBufferBarriers.clear();
    m_submissionBatches.clear();
    m_passToBatch.clear();
    m_textureLastPasses.clear();
    m_bufferLastPasses.clear();
    m_compiled = false;
    m_builderError = false;
    m_builderErrorCode = RgCompileError::None;
    m_builderErrorMessage.clear();
    ++m_generation;
    if (m_generation == 0u)
        m_generation = 1u;
}
