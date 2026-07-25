#include "renderer/rhi/RhiRenderGraph.h"

#include "renderer/rhi/RhiCommandList.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace {
[[nodiscard]] bool includesRead(const RgAccessType access) {
  return access == RgAccessType::Read || access == RgAccessType::ReadWrite;
}

[[nodiscard]] bool includesWrite(const RgAccessType access) {
  return access == RgAccessType::Write || access == RgAccessType::ReadWrite;
}

[[nodiscard]] bool textureStateAcceptsAccess(const RhiResourceState state,
                                             const RgAccessType access) {
  switch (state) {
  case RhiResourceState::ShaderRead:
  case RhiResourceState::DepthRead:
  case RhiResourceState::TransferSrc:
    return access == RgAccessType::Read;
  case RhiResourceState::RenderTarget:
  case RhiResourceState::DepthWrite:
  case RhiResourceState::TransferDst:
    return access == RgAccessType::Write || access == RgAccessType::ReadWrite;
  case RhiResourceState::ShaderWrite:
    return access == RgAccessType::Write || access == RgAccessType::ReadWrite;
  default:
    return false;
  }
}

[[nodiscard]] bool bufferStateAcceptsAccess(const RhiResourceState state,
                                            const RgAccessType access) {
  switch (state) {
  case RhiResourceState::TransferSrc:
  case RhiResourceState::VertexBuffer:
  case RhiResourceState::IndexBuffer:
  case RhiResourceState::IndirectArgument:
  case RhiResourceState::UniformBuffer:
  case RhiResourceState::HostRead:
    return access == RgAccessType::Read;
  case RhiResourceState::TransferDst:
  case RhiResourceState::HostWrite:
    return access == RgAccessType::Write;
  case RhiResourceState::StorageBuffer:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] uint32_t resolvedCount(const uint32_t base,
                                     const uint32_t requested,
                                     const uint32_t total) {
  if (base >= total)
    return 0u;
  if (requested == kRhiRemainingMipLevels ||
      requested == kRhiRemainingArrayLayers) {
    return total - base;
  }
  return requested <= total - base ? requested : 0u;
}

struct ResolvedTextureRange {
  uint32_t baseMip = 0u;
  uint32_t mipCount = 0u;
  uint32_t baseLayer = 0u;
  uint32_t layerCount = 0u;
  RhiTextureAspectFlags aspect = 0u;
};

[[nodiscard]] RhiTextureAspectFlags
formatAspect(const RhiTextureFormat format) {
  switch (format) {
  case RhiTextureFormat::Depth16:
  case RhiTextureFormat::Depth24:
  case RhiTextureFormat::Depth32Float:
    return rhiFlag(RhiTextureAspect::Depth);
  case RhiTextureFormat::Depth24Stencil8:
    return rhiFlag(RhiTextureAspect::Depth) |
           rhiFlag(RhiTextureAspect::Stencil);
  default:
    return rhiFlag(RhiTextureAspect::Color);
  }
}

[[nodiscard]] bool resolveTextureRange(const RhiTextureDesc &desc,
                                       const RgTextureSubresourceRange &range,
                                       ResolvedTextureRange &resolved) {
  resolved.baseMip = range.baseMip;
  resolved.mipCount =
      resolvedCount(range.baseMip, range.mipCount, desc.mipLevels);
  resolved.baseLayer = range.baseLayer;
  resolved.layerCount =
      resolvedCount(range.baseLayer, range.layerCount, desc.depthOrLayers);
  resolved.aspect =
      range.aspect != 0u ? range.aspect : formatAspect(desc.format);
  const RhiTextureAspectFlags validAspects = formatAspect(desc.format);
  return resolved.mipCount != 0u && resolved.layerCount != 0u &&
         (resolved.aspect & ~validAspects) == 0u;
}

[[nodiscard]] bool textureRangesOverlap(const ResolvedTextureRange &lhs,
                                        const ResolvedTextureRange &rhs) {
  const bool mipOverlap = lhs.baseMip < rhs.baseMip + rhs.mipCount &&
                          rhs.baseMip < lhs.baseMip + lhs.mipCount;
  const bool layerOverlap = lhs.baseLayer < rhs.baseLayer + rhs.layerCount &&
                            rhs.baseLayer < lhs.baseLayer + lhs.layerCount;
  return mipOverlap && layerOverlap && (lhs.aspect & rhs.aspect) != 0u;
}

[[nodiscard]] bool resolveBufferRange(const RhiBufferDesc &desc,
                                      const RgBufferRange &range,
                                      uint64_t &begin, uint64_t &end) {
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

[[nodiscard]] bool bufferRangesOverlap(const uint64_t lhsBegin,
                                       const uint64_t lhsEnd,
                                       const uint64_t rhsBegin,
                                       const uint64_t rhsEnd) {
  return lhsBegin < rhsEnd && rhsBegin < lhsEnd;
}

void addUniqueEdge(std::vector<std::vector<uint32_t>> &edges,
                   std::vector<std::vector<uint32_t>> &dependencies,
                   const uint32_t source, const uint32_t destination) {
  if (source == destination)
    return;
  auto &outgoing = edges[source];
  if (std::find(outgoing.begin(), outgoing.end(), destination) !=
      outgoing.end())
    return;
  outgoing.push_back(destination);
  dependencies[destination].push_back(source);
}

[[nodiscard]] bool queueCompatibleWithPass(const RgPassType type,
                                           const RhiQueueType queue) {
  switch (type) {
  case RgPassType::Graphics:
    return queue == RhiQueueType::Graphics;
  case RgPassType::Compute:
    return queue == RhiQueueType::Graphics || queue == RhiQueueType::Compute;
  case RgPassType::Copy:
    return queue == RhiQueueType::Graphics || queue == RhiQueueType::Compute ||
           queue == RhiQueueType::Transfer;
  case RgPassType::External:
    return queue != RhiQueueType::Present;
  }
  return false;
}

[[nodiscard]] RgTextureSubresourceRange
singleSubresourceRange(const uint32_t mip, const uint32_t layer,
                       const RhiTextureAspectFlags aspect) {
  return {mip, 1u, layer, 1u, aspect};
}
} // namespace

struct RenderGraph::TextureRecord {
  std::string name;
  RhiTextureHandle importedHandle;
  RhiTextureDesc desc;
  RhiResourceState initialState = RhiResourceState::Undefined;
  RhiResourceState finalState = RhiResourceState::Undefined;
  bool imported = false;
  uint32_t generation = 1u;
};

struct RenderGraph::BufferRecord {
  std::string name;
  RhiBufferHandle importedHandle;
  RhiBufferDesc desc;
  RhiResourceState initialState = RhiResourceState::Undefined;
  RhiResourceState finalState = RhiResourceState::Undefined;
  bool imported = false;
  uint32_t generation = 1u;
};

struct RenderGraph::PassRecord {
  std::string name;
  RgPassType type = RgPassType::Graphics;
  RhiQueueType queue = RhiQueueType::Graphics;
  std::vector<RgTextureAccess> textureAccesses;
  std::vector<RgBufferAccess> bufferAccesses;
  std::vector<RgPassHandle> explicitDependencies;
  RgExecuteCallback execute;
  uint32_t generation = 1u;
};

RgPassContext::RgPassContext(RhiCommandList &commandList,
                             const std::vector<RhiTextureHandle> &textures,
                             const std::vector<RhiBufferHandle> &buffers)
    : m_commandList(&commandList), m_textures(&textures), m_buffers(&buffers) {}

RhiCommandList &RgPassContext::commandList() const { return *m_commandList; }

RhiTextureHandle RgPassContext::texture(const RgTextureHandle handle) const {
  if (!handle.isValid() || m_textures == nullptr ||
      handle.index > m_textures->size())
    return {};
  return (*m_textures)[handle.index - 1u];
}

RhiBufferHandle RgPassContext::buffer(const RgBufferHandle handle) const {
  if (!handle.isValid() || m_buffers == nullptr ||
      handle.index > m_buffers->size())
    return {};
  return (*m_buffers)[handle.index - 1u];
}

RenderGraphPassBuilder::RenderGraphPassBuilder(RenderGraph &graph,
                                               const RgPassHandle handle)
    : m_graph(&graph), m_handle(handle) {}

RenderGraphPassBuilder &
RenderGraphPassBuilder::readTexture(const RgTextureHandle texture,
                                    const RhiResourceState state,
                                    const RgTextureSubresourceRange range) {
  static_cast<void>(m_graph->addTextureAccess(
      m_handle, {texture, RgAccessType::Read, state, range}));
  return *this;
}

RenderGraphPassBuilder &
RenderGraphPassBuilder::writeTexture(const RgTextureHandle texture,
                                     const RhiResourceState state,
                                     const RgTextureSubresourceRange range) {
  static_cast<void>(m_graph->addTextureAccess(
      m_handle, {texture, RgAccessType::Write, state, range}));
  return *this;
}

RenderGraphPassBuilder &RenderGraphPassBuilder::readWriteTexture(
    const RgTextureHandle texture, const RhiResourceState state,
    const RgTextureSubresourceRange range) {
  static_cast<void>(m_graph->addTextureAccess(
      m_handle, {texture, RgAccessType::ReadWrite, state, range}));
  return *this;
}

RenderGraphPassBuilder &
RenderGraphPassBuilder::readBuffer(const RgBufferHandle buffer,
                                   const RhiResourceState state,
                                   const RgBufferRange range) {
  static_cast<void>(m_graph->addBufferAccess(
      m_handle, {buffer, RgAccessType::Read, state, range}));
  return *this;
}

RenderGraphPassBuilder &
RenderGraphPassBuilder::writeBuffer(const RgBufferHandle buffer,
                                    const RhiResourceState state,
                                    const RgBufferRange range) {
  static_cast<void>(m_graph->addBufferAccess(
      m_handle, {buffer, RgAccessType::Write, state, range}));
  return *this;
}

RenderGraphPassBuilder &
RenderGraphPassBuilder::readWriteBuffer(const RgBufferHandle buffer,
                                        const RhiResourceState state,
                                        const RgBufferRange range) {
  static_cast<void>(m_graph->addBufferAccess(
      m_handle, {buffer, RgAccessType::ReadWrite, state, range}));
  return *this;
}

RenderGraphPassBuilder &
RenderGraphPassBuilder::dependsOn(const RgPassHandle dependency) {
  static_cast<void>(m_graph->addDependency(m_handle, dependency));
  return *this;
}

RenderGraphPassBuilder &
RenderGraphPassBuilder::setExecute(RgExecuteCallback callback) {
  static_cast<void>(m_graph->setExecute(m_handle, std::move(callback)));
  return *this;
}

RenderGraph::RenderGraph() = default;

RenderGraph::~RenderGraph() = default;

RgTextureHandle RenderGraph::importTexture(const RgImportedTextureDesc &desc) {
  if (desc.name == nullptr || desc.name[0] == '\0') {
    setBuilderError(RgCompileError::EmptyName,
                    "imported render graph texture requires a name");
    return {};
  }
  if (!desc.texture.isValid() || desc.desc.width == 0u ||
      desc.desc.height == 0u || desc.desc.depthOrLayers == 0u ||
      desc.desc.mipLevels == 0u) {
    setBuilderError(RgCompileError::InvalidTextureHandle,
                    "invalid imported texture description");
    return {};
  }
  m_textures.push_back({desc.name, desc.texture, desc.desc, desc.initialState,
                        desc.finalState, true, m_generation});
  return {static_cast<uint32_t>(m_textures.size()), m_generation};
}

RgTextureHandle RenderGraph::createTexture(const RgTransientTextureDesc &desc) {
  if (desc.name == nullptr || desc.name[0] == '\0') {
    setBuilderError(RgCompileError::EmptyName,
                    "transient render graph texture requires a name");
    return {};
  }
  if (desc.desc.width == 0u || desc.desc.height == 0u ||
      desc.desc.depthOrLayers == 0u || desc.desc.mipLevels == 0u) {
    setBuilderError(RgCompileError::InvalidTextureAccess,
                    "invalid transient texture description");
    return {};
  }
  m_textures.push_back({desc.name,
                        {},
                        desc.desc,
                        RhiResourceState::Undefined,
                        desc.finalState,
                        false,
                        m_generation});
  return {static_cast<uint32_t>(m_textures.size()), m_generation};
}

RgBufferHandle RenderGraph::importBuffer(const RgImportedBufferDesc &desc) {
  if (desc.name == nullptr || desc.name[0] == '\0') {
    setBuilderError(RgCompileError::EmptyName,
                    "imported render graph buffer requires a name");
    return {};
  }
  if (!desc.buffer.isValid() || desc.desc.size == 0u) {
    setBuilderError(RgCompileError::InvalidBufferHandle,
                    "invalid imported buffer description");
    return {};
  }
  m_buffers.push_back({desc.name, desc.buffer, desc.desc, desc.initialState,
                       desc.finalState, true, m_generation});
  return {static_cast<uint32_t>(m_buffers.size()), m_generation};
}

RgBufferHandle RenderGraph::createBuffer(const RgTransientBufferDesc &desc) {
  if (desc.name == nullptr || desc.name[0] == '\0') {
    setBuilderError(RgCompileError::EmptyName,
                    "transient render graph buffer requires a name");
    return {};
  }
  if (desc.desc.size == 0u) {
    setBuilderError(RgCompileError::InvalidBufferAccess,
                    "invalid transient buffer description");
    return {};
  }
  m_buffers.push_back({desc.name,
                       {},
                       desc.desc,
                       RhiResourceState::Undefined,
                       desc.finalState,
                       false,
                       m_generation});
  return {static_cast<uint32_t>(m_buffers.size()), m_generation};
}

RenderGraphPassBuilder RenderGraph::addPass(const RgPassDesc &desc) {
  if (desc.name == nullptr || desc.name[0] == '\0') {
    setBuilderError(RgCompileError::EmptyName,
                    "render graph pass requires a name");
    return {*this, {}};
  }
  if (!queueCompatibleWithPass(desc.type, desc.queue)) {
    setBuilderError(RgCompileError::InvalidPassHandle,
                    "render graph pass type is incompatible with its queue");
    return {*this, {}};
  }
  m_passes.push_back(
      {desc.name, desc.type, desc.queue, {}, {}, {}, {}, m_generation});
  return {*this, {static_cast<uint32_t>(m_passes.size()), m_generation}};
}

RenderGraphPassBuilder RenderGraph::editPass(const RgPassHandle pass) {
  if (passRecord(pass) == nullptr) {
    setBuilderError(RgCompileError::InvalidPassHandle,
                    "cannot edit an invalid render graph pass");
    return {*this, {}};
  }
  return {*this, pass};
}

bool RenderGraph::addTextureAccess(const RgPassHandle pass,
                                   const RgTextureAccess &access) {
  PassRecord *const record = passRecord(pass);
  const TextureRecord *const texture = textureRecord(access.texture);
  if (record == nullptr || texture == nullptr) {
    setBuilderError(RgCompileError::InvalidTextureHandle,
                    "render graph texture access contains an invalid handle");
    return false;
  }
  ResolvedTextureRange resolved;
  if (!textureStateAcceptsAccess(access.state, access.access) ||
      !resolveTextureRange(texture->desc, access.range, resolved)) {
    setBuilderError(
        RgCompileError::InvalidTextureAccess,
        "render graph texture access is incompatible with its state or range");
    return false;
  }
  for (const RgTextureAccess &existing : record->textureAccesses) {
    if (existing.texture.index != access.texture.index)
      continue;
    ResolvedTextureRange existingRange;
    static_cast<void>(
        resolveTextureRange(texture->desc, existing.range, existingRange));
    if (textureRangesOverlap(existingRange, resolved)) {
      setBuilderError(
          RgCompileError::DuplicateResourceAccess,
          "render graph pass declares overlapping texture accesses");
      return false;
    }
  }
  record->textureAccesses.push_back(access);
  m_compiled = false;
  return true;
}

bool RenderGraph::addBufferAccess(const RgPassHandle pass,
                                  const RgBufferAccess &access) {
  PassRecord *const record = passRecord(pass);
  const BufferRecord *const buffer = bufferRecord(access.buffer);
  if (record == nullptr || buffer == nullptr) {
    setBuilderError(RgCompileError::InvalidBufferHandle,
                    "render graph buffer access contains an invalid handle");
    return false;
  }
  uint64_t begin = 0u;
  uint64_t end = 0u;
  if (!bufferStateAcceptsAccess(access.state, access.access) ||
      !resolveBufferRange(buffer->desc, access.range, begin, end)) {
    setBuilderError(
        RgCompileError::InvalidBufferAccess,
        "render graph buffer access is incompatible with its state or range");
    return false;
  }
  for (const RgBufferAccess &existing : record->bufferAccesses) {
    if (existing.buffer.index != access.buffer.index)
      continue;
    uint64_t existingBegin = 0u;
    uint64_t existingEnd = 0u;
    static_cast<void>(resolveBufferRange(buffer->desc, existing.range,
                                         existingBegin, existingEnd));
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

bool RenderGraph::addDependency(const RgPassHandle pass,
                                const RgPassHandle dependency) {
  PassRecord *const record = passRecord(pass);
  if (record == nullptr || passRecord(dependency) == nullptr ||
      (pass.index == dependency.index &&
       pass.generation == dependency.generation)) {
    setBuilderError(RgCompileError::InvalidPassHandle,
                    "render graph dependency contains an invalid pass handle");
    return false;
  }
  if (std::find_if(record->explicitDependencies.begin(),
                   record->explicitDependencies.end(),
                   [dependency](const RgPassHandle candidate) {
                     return candidate.index == dependency.index &&
                            candidate.generation == dependency.generation;
                   }) == record->explicitDependencies.end()) {
    record->explicitDependencies.push_back(dependency);
  }
  m_compiled = false;
  return true;
}

bool RenderGraph::setExecute(const RgPassHandle pass,
                             RgExecuteCallback callback) {
  PassRecord *const record = passRecord(pass);
  if (record == nullptr || !callback) {
    setBuilderError(RgCompileError::MissingExecuteCallback,
                    "render graph pass execute callback is invalid");
    return false;
  }
  record->execute = std::move(callback);
  m_compiled = false;
  return true;
}

void RenderGraph::setBuilderError(const RgCompileError error,
                                  const char *const message) {
  if (m_builderError)
    return;
  m_builderError = true;
  m_builderErrorCode = error;
  m_builderErrorMessage = message;
}

RenderGraph::TextureRecord *
RenderGraph::textureRecord(const RgTextureHandle handle) {
  if (!handle.isValid() || handle.index > m_textures.size())
    return nullptr;
  TextureRecord &record = m_textures[handle.index - 1u];
  return record.generation == handle.generation ? &record : nullptr;
}

const RenderGraph::TextureRecord *
RenderGraph::textureRecord(const RgTextureHandle handle) const {
  if (!handle.isValid() || handle.index > m_textures.size())
    return nullptr;
  const TextureRecord &record = m_textures[handle.index - 1u];
  return record.generation == handle.generation ? &record : nullptr;
}

RenderGraph::BufferRecord *
RenderGraph::bufferRecord(const RgBufferHandle handle) {
  if (!handle.isValid() || handle.index > m_buffers.size())
    return nullptr;
  BufferRecord &record = m_buffers[handle.index - 1u];
  return record.generation == handle.generation ? &record : nullptr;
}

const RenderGraph::BufferRecord *
RenderGraph::bufferRecord(const RgBufferHandle handle) const {
  if (!handle.isValid() || handle.index > m_buffers.size())
    return nullptr;
  const BufferRecord &record = m_buffers[handle.index - 1u];
  return record.generation == handle.generation ? &record : nullptr;
}

RenderGraph::PassRecord *RenderGraph::passRecord(const RgPassHandle handle) {
  if (!handle.isValid() || handle.index > m_passes.size())
    return nullptr;
  PassRecord &record = m_passes[handle.index - 1u];
  return record.generation == handle.generation ? &record : nullptr;
}

const RenderGraph::PassRecord *
RenderGraph::passRecord(const RgPassHandle handle) const {
  if (!handle.isValid() || handle.index > m_passes.size())
    return nullptr;
  const PassRecord &record = m_passes[handle.index - 1u];
  return record.generation == handle.generation ? &record : nullptr;
}

RgCompileResult RenderGraph::compile() {
  m_compiledPasses.clear();
  m_textureLifetimes.assign(m_textures.size(), {});
  m_bufferLifetimes.assign(m_buffers.size(), {});
  m_epilogueTextureBarriers.clear();
  m_epilogueBufferBarriers.clear();
  m_compiled = false;

  if (m_builderError) {
    return {m_builderErrorCode, m_builderErrorMessage};
  }
  for (const PassRecord &pass : m_passes) {
    if (!pass.execute) {
      return {RgCompileError::MissingExecuteCallback,
              "render graph pass '" + pass.name + "' has no execute callback"};
    }
  }

  const uint32_t passCount = static_cast<uint32_t>(m_passes.size());
  std::vector<std::vector<uint32_t>> edges(passCount);
  std::vector<std::vector<uint32_t>> dependencies(passCount);
  for (uint32_t passIndex = 0u; passIndex < passCount; ++passIndex) {
    for (const RgPassHandle dependency :
         m_passes[passIndex].explicitDependencies) {
      addUniqueEdge(edges, dependencies, dependency.index - 1u, passIndex);
    }
  }

  for (uint32_t textureIndex = 0u; textureIndex < m_textures.size();
       ++textureIndex) {
    const TextureRecord &texture = m_textures[textureIndex];
    struct PriorAccess {
      uint32_t pass = 0u;
      RgTextureAccess access;
      ResolvedTextureRange range;
    };
    std::vector<PriorAccess> prior;
    for (uint32_t passIndex = 0u; passIndex < passCount; ++passIndex) {
      for (const RgTextureAccess &access :
           m_passes[passIndex].textureAccesses) {
        if (access.texture.index != textureIndex + 1u)
          continue;
        ResolvedTextureRange range;
        static_cast<void>(
            resolveTextureRange(texture.desc, access.range, range));
        if ((!texture.imported ||
             texture.initialState == RhiResourceState::Undefined) &&
            includesRead(access.access)) {
          for (uint32_t mip = range.baseMip;
               mip < range.baseMip + range.mipCount; ++mip) {
            for (uint32_t layer = range.baseLayer;
                 layer < range.baseLayer + range.layerCount; ++layer) {
              bool initialized = false;
              for (const PriorAccess &candidate : prior) {
                const bool includesSubresource =
                    mip >= candidate.range.baseMip &&
                    mip < candidate.range.baseMip + candidate.range.mipCount &&
                    layer >= candidate.range.baseLayer &&
                    layer < candidate.range.baseLayer +
                                candidate.range.layerCount &&
                    (candidate.range.aspect & range.aspect) != 0u;
                if (includesSubresource &&
                    includesWrite(candidate.access.access)) {
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
        for (const PriorAccess &candidate : prior) {
          if (textureRangesOverlap(candidate.range, range) &&
              (includesWrite(candidate.access.access) ||
               includesWrite(access.access))) {
            addUniqueEdge(edges, dependencies, candidate.pass, passIndex);
          }
        }
        prior.push_back({passIndex, access, range});
      }
    }
  }

  for (uint32_t bufferIndex = 0u; bufferIndex < m_buffers.size();
       ++bufferIndex) {
    const BufferRecord &buffer = m_buffers[bufferIndex];
    struct PriorAccess {
      uint32_t pass = 0u;
      RgBufferAccess access;
      uint64_t begin = 0u;
      uint64_t end = 0u;
    };
    std::vector<PriorAccess> prior;
    for (uint32_t passIndex = 0u; passIndex < passCount; ++passIndex) {
      for (const RgBufferAccess &access : m_passes[passIndex].bufferAccesses) {
        if (access.buffer.index != bufferIndex + 1u)
          continue;
        uint64_t begin = 0u;
        uint64_t end = 0u;
        static_cast<void>(
            resolveBufferRange(buffer.desc, access.range, begin, end));
        if ((!buffer.imported ||
             buffer.initialState == RhiResourceState::Undefined) &&
            includesRead(access.access)) {
          std::vector<std::pair<uint64_t, uint64_t>> written;
          for (const PriorAccess &candidate : prior) {
            if (!includesWrite(candidate.access.access))
              continue;
            const uint64_t overlapBegin = std::max(begin, candidate.begin);
            const uint64_t overlapEnd = std::min(end, candidate.end);
            if (overlapBegin < overlapEnd)
              written.emplace_back(overlapBegin, overlapEnd);
          }
          std::sort(written.begin(), written.end());
          uint64_t covered = begin;
          for (const auto &interval : written) {
            if (interval.first > covered)
              break;
            covered = std::max(covered, interval.second);
          }
          if (covered < end) {
            return {RgCompileError::ReadBeforeWrite,
                    "transient buffer '" + buffer.name +
                        "' is read before the requested range is written"};
          }
        }
        for (const PriorAccess &candidate : prior) {
          if (bufferRangesOverlap(candidate.begin, candidate.end, begin, end) &&
              (includesWrite(candidate.access.access) ||
               includesWrite(access.access))) {
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
      if (indegree[index] == 0u &&
          std::find(order.begin(), order.end(), index) == order.end()) {
        selected = index;
        break;
      }
    }
    if (selected == passCount) {
      return {RgCompileError::CyclicDependency,
              "render graph contains a cyclic pass dependency"};
    }
    order.push_back(selected);
    indegree[selected] = std::numeric_limits<uint32_t>::max();
    for (const uint32_t destination : edges[selected]) {
      --indegree[destination];
    }
  }

  std::vector<uint32_t> originalToCompiled(passCount, 0u);
  for (uint32_t compiledIndex = 0u; compiledIndex < order.size();
       ++compiledIndex) {
    originalToCompiled[order[compiledIndex]] = compiledIndex;
  }

  struct TextureState {
    RhiResourceState state = RhiResourceState::Undefined;
    RhiQueueType queue = RhiQueueType::Graphics;
    RgAccessType lastAccess = RgAccessType::Read;
    bool accessed = false;
  };
  std::vector<std::vector<TextureState>> textureStates;
  textureStates.reserve(m_textures.size());
  for (const TextureRecord &texture : m_textures) {
    const uint32_t subresourceCount =
        texture.desc.mipLevels * texture.desc.depthOrLayers;
    textureStates.emplace_back(subresourceCount,
                               TextureState{texture.initialState,
                                            RhiQueueType::Graphics,
                                            RgAccessType::Read, false});
  }
  struct BufferState {
    RhiResourceState state = RhiResourceState::Undefined;
    RhiQueueType queue = RhiQueueType::Graphics;
    RgAccessType lastAccess = RgAccessType::Read;
    bool accessed = false;
  };
  std::vector<BufferState> bufferStates;
  bufferStates.reserve(m_buffers.size());
  for (const BufferRecord &buffer : m_buffers) {
    bufferStates.push_back({buffer.initialState, RhiQueueType::Graphics,
                            RgAccessType::Read, false});
  }

  m_compiledPasses.reserve(passCount);
  for (uint32_t compiledIndex = 0u; compiledIndex < order.size();
       ++compiledIndex) {
    const uint32_t originalIndex = order[compiledIndex];
    const PassRecord &pass = m_passes[originalIndex];
    RgCompiledPass compiled;
    compiled.handle = {originalIndex + 1u, pass.generation};
    compiled.name = pass.name;
    compiled.type = pass.type;
    compiled.queue = pass.queue;
    for (const uint32_t dependency : dependencies[originalIndex]) {
      compiled.dependencies.push_back(originalToCompiled[dependency]);
    }
    std::sort(compiled.dependencies.begin(), compiled.dependencies.end());

    for (const RgTextureAccess &access : pass.textureAccesses) {
      const uint32_t resourceIndex = access.texture.index - 1u;
      const TextureRecord &texture = m_textures[resourceIndex];
      ResolvedTextureRange range;
      static_cast<void>(resolveTextureRange(texture.desc, access.range, range));
      RgResourceLifetime &lifetime = m_textureLifetimes[resourceIndex];
      if (!lifetime.used) {
        lifetime = {compiledIndex, compiledIndex, true};
      } else {
        lifetime.lastPass = compiledIndex;
      }
      for (uint32_t mip = range.baseMip; mip < range.baseMip + range.mipCount;
           ++mip) {
        for (uint32_t layer = range.baseLayer;
             layer < range.baseLayer + range.layerCount; ++layer) {
          TextureState &current =
              textureStates[resourceIndex]
                           [mip * texture.desc.depthOrLayers + layer];
          const bool hazard =
              current.accessed && (includesWrite(current.lastAccess) ||
                                   includesWrite(access.access));
          if (current.state != access.state || current.queue != pass.queue ||
              hazard) {
            compiled.textureBarriers.push_back(
                {access.texture, current.state, access.state,
                 singleSubresourceRange(mip, layer, range.aspect),
                 current.queue, pass.queue});
          }
          current = {access.state, pass.queue, access.access, true};
        }
      }
    }

    for (const RgBufferAccess &access : pass.bufferAccesses) {
      const uint32_t resourceIndex = access.buffer.index - 1u;
      RgResourceLifetime &lifetime = m_bufferLifetimes[resourceIndex];
      if (!lifetime.used) {
        lifetime = {compiledIndex, compiledIndex, true};
      } else {
        lifetime.lastPass = compiledIndex;
      }
      BufferState &current = bufferStates[resourceIndex];
      const bool hazard =
          current.accessed &&
          (includesWrite(current.lastAccess) || includesWrite(access.access));
      if (current.state != access.state || current.queue != pass.queue ||
          hazard) {
        compiled.bufferBarriers.push_back({access.buffer, current.state,
                                           access.state, access.range,
                                           current.queue, pass.queue});
      }
      current = {access.state, pass.queue, access.access, true};
    }
    m_compiledPasses.push_back(std::move(compiled));
  }

  for (uint32_t index = 0u; index < m_textures.size(); ++index) {
    const TextureRecord &texture = m_textures[index];
    if (texture.finalState == RhiResourceState::Undefined ||
        !m_textureLifetimes[index].used)
      continue;
    const RgTextureHandle handle{index + 1u, texture.generation};
    for (uint32_t mip = 0u; mip < texture.desc.mipLevels; ++mip) {
      for (uint32_t layer = 0u; layer < texture.desc.depthOrLayers; ++layer) {
        const TextureState &current =
            textureStates[index][mip * texture.desc.depthOrLayers + layer];
        if (current.state == texture.finalState)
          continue;
        m_epilogueTextureBarriers.push_back(
            {handle, current.state, texture.finalState,
             singleSubresourceRange(mip, layer,
                                    formatAspect(texture.desc.format)),
             current.queue, current.queue});
      }
    }
  }
  for (uint32_t index = 0u; index < m_buffers.size(); ++index) {
    const BufferRecord &buffer = m_buffers[index];
    if (buffer.finalState == RhiResourceState::Undefined ||
        !m_bufferLifetimes[index].used ||
        bufferStates[index].state == buffer.finalState)
      continue;
    m_epilogueBufferBarriers.push_back({{index + 1u, buffer.generation},
                                        bufferStates[index].state,
                                        buffer.finalState,
                                        {},
                                        bufferStates[index].queue,
                                        bufferStates[index].queue});
  }

  m_compiled = true;
  return {};
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
  m_compiled = false;
  m_builderError = false;
  m_builderErrorCode = RgCompileError::None;
  m_builderErrorMessage.clear();
  ++m_generation;
  if (m_generation == 0u)
    m_generation = 1u;
}
