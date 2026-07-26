#ifndef MECRAFT_RHI_RENDER_GRAPH_H
#define MECRAFT_RHI_RENDER_GRAPH_H

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiResources.h"
#include "renderer/rhi/RhiTypes.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

class RhiCommandList;
class RhiCommandListPool;
class RhiDevice;

inline constexpr uint32_t kRgInvalidPassIndex =
    std::numeric_limits<uint32_t>::max();

/// Bounds the number of compiled passes recorded in one queue submission.
/// Keeping command replay bounded prevents a frame graph from producing an
/// unbounded backend command stream while preserving the graph's dependency
/// ordering between adjacent same-queue batches.
inline constexpr uint32_t kRgMaxPassesPerSubmissionBatch = 8u;

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

enum class RgExecuteError {
  None,
  NotCompiled,
  DeviceMismatch,
  ResourceCreationFailed,
  CommandListUnavailable,
  CommandRecordingFailed,
  PassExecutionFailed,
  SubmissionFailed,
  TimestampUnsupported,
  TimestampQueryFailed
};

enum class RgTimingPollResult { Unavailable, Available, Error };

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
  RhiTextureViewHandle defaultView;
  RhiQueueType initialQueue = RhiQueueType::Graphics;
  RhiQueueType finalQueue = RhiQueueType::Graphics;
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
  RhiQueueType initialQueue = RhiQueueType::Graphics;
  RhiQueueType finalQueue = RhiQueueType::Graphics;
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
  uint32_t sourcePass = kRgInvalidPassIndex;
};

struct RgBufferBarrierPlan {
  RgBufferHandle buffer;
  RhiResourceState oldState = RhiResourceState::Undefined;
  RhiResourceState newState = RhiResourceState::Undefined;
  RgBufferRange range;
  RhiQueueType sourceQueue = RhiQueueType::Graphics;
  RhiQueueType destinationQueue = RhiQueueType::Graphics;
  uint32_t sourcePass = kRgInvalidPassIndex;
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
  std::vector<RgTextureBarrierPlan> releaseTextureBarriers;
  std::vector<RgBufferBarrierPlan> releaseBufferBarriers;
};

struct RgCompileResult {
  RgCompileError error = RgCompileError::None;
  std::string message;

  [[nodiscard]] bool succeeded() const { return error == RgCompileError::None; }
};

struct RgSubmissionBatchPlan {
  /// Logical queue that records and executes all passes in this batch.
  RhiQueueType queue = RhiQueueType::Graphics;
  /// Compiled pass indices recorded into the batch command list in order.
  std::vector<uint32_t> passes;
  /// Earlier batch indices whose timeline tokens must complete before execution.
  std::vector<uint32_t> dependencies;
};

struct RgExecuteResult {
  RgExecuteError error = RgExecuteError::None;
  std::string message;
  std::vector<RhiSubmissionToken> submissions;
  /// CPU milliseconds spent resolving transient resources and recording
  /// every batch command list, measured on the successful path only.
  double recordMilliseconds = 0.0;
  /// CPU milliseconds spent submitting recorded batches to device queues.
  double submitMilliseconds = 0.0;

  /// Reports whether every command list was recorded and submitted successfully.
  /// @return True only when no execution error was reported.
  [[nodiscard]] bool succeeded() const { return error == RgExecuteError::None; }

  /// Returns the token for the final graph submission in execution order.
  /// @return Final submission token, or an invalid token when nothing was submitted.
  [[nodiscard]] RhiSubmissionToken completionToken() const {
    return submissions.empty() ? RhiSubmissionToken{} : submissions.back();
  }
};

struct RgPassTiming {
  /// Debug name supplied by the pass declaration.
  std::string name;
  /// Logical queue that executed the measured pass.
  RhiQueueType queue = RhiQueueType::Graphics;
  /// GPU timestamp difference converted to nanoseconds by the RHI backend.
  uint64_t durationNanoseconds = 0u;
  /// Absolute GPU clock nanoseconds sampled when the pass started and ended.
  /// Values within one snapshot share a clock, so consumers can measure the
  /// scheduling gap between consecutive passes and the whole graph span.
  uint64_t beginNanoseconds = 0u;
  uint64_t endNanoseconds = 0u;
};

struct RgTimingSnapshot {
  /// Monotonic Render Graph execution identity associated with this snapshot.
  uint64_t execution = 0u;
  /// Timings in compiled pass execution order.
  std::vector<RgPassTiming> passes;

  /// Reports whether this snapshot contains a completed execution.
  /// @return True when the execution identity is non-zero.
  [[nodiscard]] bool isValid() const { return execution != 0u; }
};

class RgPassContext {
public:
  [[nodiscard]] RhiCommandList &commandList() const;
  [[nodiscard]] RhiTextureHandle texture(RgTextureHandle handle) const;

  /// Resolves the default texture view for an imported or transient graph texture.
  /// @param handle Texture handle declared by the executing pass.
  /// @return RHI view owned by the imported resource or transient allocation.
  [[nodiscard]] RhiTextureViewHandle textureView(RgTextureHandle handle) const;
  [[nodiscard]] RhiBufferHandle buffer(RgBufferHandle handle) const;

private:
  friend class RenderGraph;
  RgPassContext(RhiCommandList &commandList,
                const std::vector<RhiTextureHandle> &textures,
                const std::vector<RhiTextureViewHandle> &textureViews,
                const std::vector<RhiBufferHandle> &buffers);

  RhiCommandList *m_commandList = nullptr;
  const std::vector<RhiTextureHandle> *m_textures = nullptr;
  const std::vector<RhiTextureViewHandle> *m_textureViews = nullptr;
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

  /// Records compiled passes, creates required transient resources, and submits
  /// dependency batches to their declared logical queues.
  /// @param device Device that owns imported and transient graph resources.
  /// @param commandListPool Pool used to allocate one command list per batch.
  /// @return Execution status and submission tokens in submission order.
  [[nodiscard]] RgExecuteResult execute(RhiDevice &device,
                                        RhiCommandListPool &commandListPool);

  /// Collects completed GPU pass timestamps without blocking for pending work.
  /// @param device Device used by the matching execute calls.
  /// @return Available after collecting at least one execution, Unavailable while
  /// work is pending, or Error when the device or query contract is violated.
  [[nodiscard]] RgTimingPollResult pollTimings(RhiDevice &device);

  /// Returns the most recently collected complete timing snapshot.
  /// @return Stable snapshot retained until a newer execution is collected or
  /// transient resources are released.
  [[nodiscard]] const RgTimingSnapshot &latestTimings() const {
    return m_latestTimings;
  }

  /// Destroys pooled transient resources and timestamp queries owned by a device.
  /// @param device Device passed to previous execute calls for this graph.
  void releaseTransientResources(RhiDevice &device);
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
  [[nodiscard]] const std::vector<RgSubmissionBatchPlan> &
  submissionBatches() const {
    return m_submissionBatches;
  }
  [[nodiscard]] bool isCompiled() const { return m_compiled; }

private:
  friend class RenderGraphPassBuilder;

  struct TextureRecord;
  struct BufferRecord;
  struct PassRecord;
  struct TransientTexture;
  struct TransientBuffer;
  struct TimingSlot;

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
  std::vector<RgSubmissionBatchPlan> m_submissionBatches;
  std::vector<uint32_t> m_passToBatch;
  std::vector<std::vector<uint32_t>> m_textureLastPasses;
  std::vector<uint32_t> m_bufferLastPasses;
  std::vector<TransientTexture> m_transientTextures;
  std::vector<TransientBuffer> m_transientBuffers;
  std::vector<TimingSlot> m_timingSlots;
  RgTimingSnapshot m_latestTimings;
  RhiDevice *m_runtimeDevice = nullptr;
  bool m_compiled = false;
  bool m_builderError = false;
  RgCompileError m_builderErrorCode = RgCompileError::None;
  std::string m_builderErrorMessage;
  uint32_t m_generation = 1u;
  uint64_t m_execution = 0u;
};

#endif // MECRAFT_RHI_RENDER_GRAPH_H
