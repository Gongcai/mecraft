#ifndef MECRAFT_RHI_RENDER_GRAPH_H
#define MECRAFT_RHI_RENDER_GRAPH_H

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiResources.h"
#include "renderer/rhi/RhiTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class RhiCommandList;

struct RgTextureHandle {
  uint32_t index = 0u;
  uint32_t generation = 0u;

  [[nodiscard]] constexpr bool isValid() const {
    return index != 0u && generation != 0u;
  }
};

struct RgBufferHandle {
  uint32_t index = 0u;
  uint32_t generation = 0u;

  [[nodiscard]] constexpr bool isValid() const {
    return index != 0u && generation != 0u;
  }
};

struct RgPassHandle {
  uint32_t index = 0u;
  uint32_t generation = 0u;

  [[nodiscard]] constexpr bool isValid() const {
    return index != 0u && generation != 0u;
  }
};

enum class RgPassType { Graphics, Compute, Copy, External };

enum class RgAccessType { Read, Write, ReadWrite };

enum class RgCompileError {
  None,
  EmptyName,
  InvalidTextureHandle,
  InvalidBufferHandle,
  InvalidPassHandle,
  DuplicateResourceAccess,
  InvalidTextureAccess,
  InvalidBufferAccess,
  ReadBeforeWrite,
  MissingExecuteCallback,
  CyclicDependency
};

struct RgTextureSubresourceRange {
  uint32_t baseMip = 0u;
  uint32_t mipCount = kRhiRemainingMipLevels;
  uint32_t baseLayer = 0u;
  uint32_t layerCount = kRhiRemainingArrayLayers;
  RhiTextureAspectFlags aspect = 0u;
};

struct RgBufferRange {
  uint64_t offset = 0u;
  uint64_t size = kRhiWholeSize;
};

struct RgImportedTextureDesc {
  const char *name = nullptr;
  RhiTextureHandle texture;
  RhiTextureDesc desc;
  RhiResourceState initialState = RhiResourceState::Undefined;
  RhiResourceState finalState = RhiResourceState::Undefined;
};

struct RgTransientTextureDesc {
  const char *name = nullptr;
  RhiTextureDesc desc;
  RhiResourceState finalState = RhiResourceState::Undefined;
};

struct RgImportedBufferDesc {
  const char *name = nullptr;
  RhiBufferHandle buffer;
  RhiBufferDesc desc;
  RhiResourceState initialState = RhiResourceState::Undefined;
  RhiResourceState finalState = RhiResourceState::Undefined;
};

struct RgTransientBufferDesc {
  const char *name = nullptr;
  RhiBufferDesc desc;
  RhiResourceState finalState = RhiResourceState::Undefined;
};

struct RgPassDesc {
  const char *name = nullptr;
  RgPassType type = RgPassType::Graphics;
  RhiQueueType queue = RhiQueueType::Graphics;
};

struct RgTextureAccess {
  RgTextureHandle texture;
  RgAccessType access = RgAccessType::Read;
  RhiResourceState state = RhiResourceState::ShaderRead;
  RgTextureSubresourceRange range;
};

struct RgBufferAccess {
  RgBufferHandle buffer;
  RgAccessType access = RgAccessType::Read;
  RhiResourceState state = RhiResourceState::StorageBuffer;
  RgBufferRange range;
};

struct RgTextureBarrierPlan {
  RgTextureHandle texture;
  RhiResourceState oldState = RhiResourceState::Undefined;
  RhiResourceState newState = RhiResourceState::Undefined;
  RgTextureSubresourceRange range;
  RhiQueueType sourceQueue = RhiQueueType::Graphics;
  RhiQueueType destinationQueue = RhiQueueType::Graphics;
};

struct RgBufferBarrierPlan {
  RgBufferHandle buffer;
  RhiResourceState oldState = RhiResourceState::Undefined;
  RhiResourceState newState = RhiResourceState::Undefined;
  RgBufferRange range;
  RhiQueueType sourceQueue = RhiQueueType::Graphics;
  RhiQueueType destinationQueue = RhiQueueType::Graphics;
};

struct RgResourceLifetime {
  uint32_t firstPass = 0u;
  uint32_t lastPass = 0u;
  bool used = false;
};

struct RgCompiledPass {
  RgPassHandle handle;
  std::string name;
  RgPassType type = RgPassType::Graphics;
  RhiQueueType queue = RhiQueueType::Graphics;
  std::vector<uint32_t> dependencies;
  std::vector<RgTextureBarrierPlan> textureBarriers;
  std::vector<RgBufferBarrierPlan> bufferBarriers;
};

struct RgCompileResult {
  RgCompileError error = RgCompileError::None;
  std::string message;

  [[nodiscard]] bool succeeded() const { return error == RgCompileError::None; }
};

class RgPassContext {
public:
  [[nodiscard]] RhiCommandList &commandList() const;
  [[nodiscard]] RhiTextureHandle texture(RgTextureHandle handle) const;
  [[nodiscard]] RhiBufferHandle buffer(RgBufferHandle handle) const;

private:
  friend class RenderGraph;
  RgPassContext(RhiCommandList &commandList,
                const std::vector<RhiTextureHandle> &textures,
                const std::vector<RhiBufferHandle> &buffers);

  RhiCommandList *m_commandList = nullptr;
  const std::vector<RhiTextureHandle> *m_textures = nullptr;
  const std::vector<RhiBufferHandle> *m_buffers = nullptr;
};

using RgExecuteCallback = std::function<bool(RgPassContext &)>;

class RenderGraph;

class RenderGraphPassBuilder {
public:
  RenderGraphPassBuilder &
  readTexture(RgTextureHandle texture,
              RhiResourceState state = RhiResourceState::ShaderRead,
              RgTextureSubresourceRange range = {});
  RenderGraphPassBuilder &writeTexture(RgTextureHandle texture,
                                       RhiResourceState state,
                                       RgTextureSubresourceRange range = {});
  RenderGraphPassBuilder &
  readWriteTexture(RgTextureHandle texture, RhiResourceState state,
                   RgTextureSubresourceRange range = {});
  RenderGraphPassBuilder &readBuffer(RgBufferHandle buffer,
                                     RhiResourceState state,
                                     RgBufferRange range = {});
  RenderGraphPassBuilder &writeBuffer(RgBufferHandle buffer,
                                      RhiResourceState state,
                                      RgBufferRange range = {});
  RenderGraphPassBuilder &readWriteBuffer(RgBufferHandle buffer,
                                          RhiResourceState state,
                                          RgBufferRange range = {});
  RenderGraphPassBuilder &dependsOn(RgPassHandle dependency);
  RenderGraphPassBuilder &setExecute(RgExecuteCallback callback);

  [[nodiscard]] RgPassHandle handle() const { return m_handle; }

private:
  friend class RenderGraph;
  RenderGraphPassBuilder(RenderGraph &graph, RgPassHandle handle);

  RenderGraph *m_graph = nullptr;
  RgPassHandle m_handle;
};

class RenderGraph {
public:
  RenderGraph();
  ~RenderGraph();
  RenderGraph(const RenderGraph &) = delete;
  RenderGraph &operator=(const RenderGraph &) = delete;

  [[nodiscard]] RgTextureHandle
  importTexture(const RgImportedTextureDesc &desc);
  [[nodiscard]] RgTextureHandle
  createTexture(const RgTransientTextureDesc &desc);
  [[nodiscard]] RgBufferHandle importBuffer(const RgImportedBufferDesc &desc);
  [[nodiscard]] RgBufferHandle createBuffer(const RgTransientBufferDesc &desc);
  [[nodiscard]] RenderGraphPassBuilder addPass(const RgPassDesc &desc);
  [[nodiscard]] RenderGraphPassBuilder editPass(RgPassHandle pass);

  [[nodiscard]] RgCompileResult compile();
  void reset();

  [[nodiscard]] const std::vector<RgCompiledPass> &compiledPasses() const {
    return m_compiledPasses;
  }
  [[nodiscard]] const std::vector<RgResourceLifetime> &
  textureLifetimes() const {
    return m_textureLifetimes;
  }
  [[nodiscard]] const std::vector<RgResourceLifetime> &bufferLifetimes() const {
    return m_bufferLifetimes;
  }
  [[nodiscard]] const std::vector<RgTextureBarrierPlan> &
  epilogueTextureBarriers() const {
    return m_epilogueTextureBarriers;
  }
  [[nodiscard]] const std::vector<RgBufferBarrierPlan> &
  epilogueBufferBarriers() const {
    return m_epilogueBufferBarriers;
  }
  [[nodiscard]] bool isCompiled() const { return m_compiled; }

private:
  friend class RenderGraphPassBuilder;

  struct TextureRecord;
  struct BufferRecord;
  struct PassRecord;

  [[nodiscard]] bool addTextureAccess(RgPassHandle pass,
                                      const RgTextureAccess &access);
  [[nodiscard]] bool addBufferAccess(RgPassHandle pass,
                                     const RgBufferAccess &access);
  [[nodiscard]] bool addDependency(RgPassHandle pass, RgPassHandle dependency);
  [[nodiscard]] bool setExecute(RgPassHandle pass, RgExecuteCallback callback);
  void setBuilderError(RgCompileError error, const char *message);

  [[nodiscard]] TextureRecord *textureRecord(RgTextureHandle handle);
  [[nodiscard]] const TextureRecord *
  textureRecord(RgTextureHandle handle) const;
  [[nodiscard]] BufferRecord *bufferRecord(RgBufferHandle handle);
  [[nodiscard]] const BufferRecord *bufferRecord(RgBufferHandle handle) const;
  [[nodiscard]] PassRecord *passRecord(RgPassHandle handle);
  [[nodiscard]] const PassRecord *passRecord(RgPassHandle handle) const;

  std::vector<TextureRecord> m_textures;
  std::vector<BufferRecord> m_buffers;
  std::vector<PassRecord> m_passes;
  std::vector<RgCompiledPass> m_compiledPasses;
  std::vector<RgResourceLifetime> m_textureLifetimes;
  std::vector<RgResourceLifetime> m_bufferLifetimes;
  std::vector<RgTextureBarrierPlan> m_epilogueTextureBarriers;
  std::vector<RgBufferBarrierPlan> m_epilogueBufferBarriers;
  bool m_compiled = false;
  bool m_builderError = false;
  RgCompileError m_builderErrorCode = RgCompileError::None;
  std::string m_builderErrorMessage;
  uint32_t m_generation = 1u;
};

#endif // MECRAFT_RHI_RENDER_GRAPH_H
