#include "renderer/rhi/RhiRenderGraph.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
bool requireTrue(const bool condition, const char *const message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}

RhiTextureDesc colorTextureDesc(const uint32_t mipLevels = 1u,
                                const uint32_t layers = 1u) {
  RhiTextureDesc desc;
  desc.debugName = "RenderGraphTest.Color";
  desc.format = RhiTextureFormat::Rgba16Float;
  desc.width = 64u;
  desc.height = 64u;
  desc.depthOrLayers = layers;
  desc.mipLevels = mipLevels;
  desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
               rhiFlag(RhiTextureUsage::Storage) |
               rhiFlag(RhiTextureUsage::ColorAttachment) |
               rhiFlag(RhiTextureUsage::TransferSrc) |
               rhiFlag(RhiTextureUsage::TransferDst);
  return desc;
}

RhiTextureDesc depthTextureDesc(const uint32_t layers) {
  RhiTextureDesc desc;
  desc.debugName = "RenderGraphTest.Depth";
  desc.dimension = RhiTextureDimension::Texture2DArray;
  desc.format = RhiTextureFormat::Depth32Float;
  desc.width = 64u;
  desc.height = 64u;
  desc.depthOrLayers = layers;
  desc.mipLevels = 1u;
  desc.usage = rhiFlag(RhiTextureUsage::Sampled) |
               rhiFlag(RhiTextureUsage::DepthStencilAttachment) |
               rhiFlag(RhiTextureUsage::TransferSrc) |
               rhiFlag(RhiTextureUsage::TransferDst);
  return desc;
}

bool executeNoop(RgPassContext &) { return true; }

bool testTextureDependencyAndBarrierPlanning() {
  RenderGraph graph;
  const RgTextureHandle texture = graph.createTexture(
      {"Lighting", colorTextureDesc(), RhiResourceState::ShaderRead});
  graph.addPass({"WriteLighting", RgPassType::Graphics, RhiQueueType::Graphics})
      .writeTexture(texture, RhiResourceState::RenderTarget)
      .setExecute(executeNoop);
  graph.addPass({"ReadLighting", RgPassType::Compute, RhiQueueType::Compute})
      .readTexture(texture)
      .setExecute(executeNoop);

  const RgCompileResult result = graph.compile();
  if (!requireTrue(result.succeeded(), result.message.c_str()) ||
      !requireTrue(graph.compiledPasses().size() == 2u,
                   "two texture passes must compile")) {
    return false;
  }
  const auto &writePass = graph.compiledPasses()[0];
  const auto &readPass = graph.compiledPasses()[1];
  return requireTrue(
             writePass.textureBarriers.size() == 1u,
             "the first texture write must transition from Undefined") &&
         requireTrue(readPass.dependencies.size() == 1u &&
                         readPass.dependencies[0] == 0u,
                     "the texture reader must depend on its writer") &&
         requireTrue(
             readPass.textureBarriers.size() == 1u &&
                 readPass.textureBarriers[0].oldState ==
                     RhiResourceState::RenderTarget &&
                 readPass.textureBarriers[0].newState ==
                     RhiResourceState::ShaderRead &&
                 readPass.textureBarriers[0].sourceQueue ==
                     RhiQueueType::Graphics &&
                 readPass.textureBarriers[0].destinationQueue ==
                     RhiQueueType::Compute,
             "the reader must receive a cross-queue state transition") &&
         requireTrue(
             graph.epilogueTextureBarriers().size() == 1u &&
                 graph.epilogueTextureBarriers()[0].oldState ==
                     RhiResourceState::ShaderRead &&
                 graph.epilogueTextureBarriers()[0].newState ==
                     RhiResourceState::ShaderRead &&
                 graph.epilogueTextureBarriers()[0].sourceQueue ==
                     RhiQueueType::Compute &&
                 graph.epilogueTextureBarriers()[0].destinationQueue ==
                     RhiQueueType::Graphics,
             "the final queue contract must return the texture to graphics") &&
         requireTrue(graph.textureLifetimes()[0].used &&
                         graph.textureLifetimes()[0].firstPass == 0u &&
                         graph.textureLifetimes()[0].lastPass == 1u,
                     "texture lifetime must span its writer and reader");
}

bool testSubresourceIndependence() {
  RenderGraph graph;
  const RgTextureHandle texture = graph.createTexture(
      {"MipChain", colorTextureDesc(2u, 2u), RhiResourceState::Undefined});
  const RgTextureSubresourceRange mip0Layer0{0u, 1u, 0u, 1u, 0u};
  const RgTextureSubresourceRange mip1Layer1{1u, 1u, 1u, 1u, 0u};
  graph.addPass({"WriteMip0", RgPassType::Compute, RhiQueueType::Graphics})
      .writeTexture(texture, RhiResourceState::ShaderWrite, mip0Layer0)
      .setExecute(executeNoop);
  graph.addPass({"WriteMip1", RgPassType::Compute, RhiQueueType::Graphics})
      .writeTexture(texture, RhiResourceState::ShaderWrite, mip1Layer1)
      .setExecute(executeNoop);

  const RgCompileResult result = graph.compile();
  return requireTrue(result.succeeded(), result.message.c_str()) &&
         requireTrue(
             graph.compiledPasses()[1].dependencies.empty(),
             "disjoint texture subresources must not create a dependency") &&
         requireTrue(graph.compiledPasses()[0].textureBarriers.size() == 1u &&
                         graph.compiledPasses()[1].textureBarriers.size() == 1u,
                     "only accessed subresources must receive barriers");
}

bool testThreeDimensionalTextureSubresources() {
  RenderGraph graph;
  RhiTextureDesc desc = colorTextureDesc(3u, 16u);
  desc.dimension = RhiTextureDimension::Texture3D;
  const RgTextureHandle texture = graph.createTexture(
      {"Volume", desc, RhiResourceState::ShaderRead});
  graph.addPass({"WriteVolume", RgPassType::Copy, RhiQueueType::Graphics})
      .writeTexture(texture, RhiResourceState::TransferDst)
      .setExecute(executeNoop);

  const RgCompileResult result = graph.compile();
  if (!requireTrue(result.succeeded(), result.message.c_str())) {
    return false;
  }
  const auto hasOnlyNativeSubresourceLayers = [](const auto &barriers) {
    return std::all_of(barriers.begin(), barriers.end(), [](const auto &barrier) {
      return barrier.range.baseLayer == 0u && barrier.range.layerCount == 1u;
    });
  };
  return requireTrue(graph.compiledPasses()[0].textureBarriers.size() == 3u,
                     "a 3D texture must plan one barrier per mip") &&
         requireTrue(graph.epilogueTextureBarriers().size() == 3u,
                     "a 3D texture final state must cover each mip once") &&
         requireTrue(
             hasOnlyNativeSubresourceLayers(
                 graph.compiledPasses()[0].textureBarriers) &&
                 hasOnlyNativeSubresourceLayers(graph.epilogueTextureBarriers()),
             "3D texture depth must not be treated as array layers");
}

bool testLayeredDepthCopyPlanning() {
  RenderGraph graph;
  const RhiTextureDesc depthDesc = depthTextureDesc(2u);
  const RhiTextureDesc colorDesc = colorTextureDesc(1u, 2u);
  const RgTextureHandle depthOpaque = graph.importTexture(
      {"ShadowDepthOpaque", {31u, 1u}, depthDesc,
       RhiResourceState::DepthRead, RhiResourceState::DepthRead, {},
       RhiQueueType::Graphics, RhiQueueType::Graphics});
  const RgTextureHandle depthAll = graph.importTexture(
      {"ShadowDepthAll", {32u, 1u}, depthDesc,
       RhiResourceState::DepthRead, RhiResourceState::DepthRead, {},
       RhiQueueType::Graphics, RhiQueueType::Graphics});
  const RgTextureHandle color0 = graph.importTexture(
      {"ShadowColor0", {33u, 1u}, colorDesc,
       RhiResourceState::ShaderRead, RhiResourceState::ShaderRead, {},
       RhiQueueType::Graphics, RhiQueueType::Graphics});
  const RgTextureHandle color1 = graph.importTexture(
      {"ShadowColor1", {34u, 1u}, colorDesc,
       RhiResourceState::ShaderRead, RhiResourceState::ShaderRead, {},
       RhiQueueType::Graphics, RhiQueueType::Graphics});

  const char *const opaqueNames[] = {"Cascade0.Opaque", "Cascade1.Opaque"};
  const char *const copyNames[] = {"Cascade0.Copy", "Cascade1.Copy"};
  const char *const transparentNames[] = {"Cascade0.Transparent",
                                           "Cascade1.Transparent"};
  RgPassHandle previous;
  for (uint32_t cascade = 0u; cascade < 2u; ++cascade) {
    const RgTextureSubresourceRange range{0u, 1u, cascade, 1u, 0u};
    RenderGraphPassBuilder opaque = graph.addPass(
        {opaqueNames[cascade], RgPassType::Graphics, RhiQueueType::Graphics});
    if (previous.isValid())
      opaque.dependsOn(previous);
    opaque.writeTexture(depthOpaque, RhiResourceState::DepthWrite, range)
        .setExecute(executeNoop);
    previous = opaque.handle();

    RenderGraphPassBuilder copy = graph.addPass(
        {copyNames[cascade], RgPassType::Copy, RhiQueueType::Graphics});
    copy.dependsOn(previous)
        .readTexture(depthOpaque, RhiResourceState::TransferSrc, range)
        .writeTexture(depthAll, RhiResourceState::TransferDst, range)
        .setExecute(executeNoop);
    previous = copy.handle();

    RenderGraphPassBuilder transparent = graph.addPass(
        {transparentNames[cascade], RgPassType::Graphics,
         RhiQueueType::Graphics});
    transparent.dependsOn(previous)
        .readWriteTexture(depthAll, RhiResourceState::DepthWrite, range)
        .writeTexture(color0, RhiResourceState::RenderTarget, range)
        .writeTexture(color1, RhiResourceState::RenderTarget, range)
        .setExecute(executeNoop);
    previous = transparent.handle();
  }

  const RgCompileResult result = graph.compile();
  if (!requireTrue(result.succeeded(), result.message.c_str()) ||
      !requireTrue(graph.compiledPasses().size() == 6u,
                   "two shadow cascades must compile into six passes")) {
    return false;
  }
  const RgCompiledPass &opaque = graph.compiledPasses()[0];
  const RgCompiledPass &copy = graph.compiledPasses()[1];
  const RgCompiledPass &transparent = graph.compiledPasses()[2];
  return requireTrue(
             opaque.textureBarriers.size() == 1u &&
                 opaque.textureBarriers[0].oldState ==
                     RhiResourceState::DepthRead &&
                 opaque.textureBarriers[0].newState ==
                     RhiResourceState::DepthWrite,
             "shadow opaque pass must transition its layer to depth write") &&
         requireTrue(
             copy.dependencies.size() == 1u &&
                 copy.textureBarriers.size() == 2u &&
                 copy.textureBarriers[0].oldState ==
                     RhiResourceState::DepthWrite &&
                 copy.textureBarriers[0].newState ==
                     RhiResourceState::TransferSrc &&
                 copy.textureBarriers[1].oldState ==
                     RhiResourceState::DepthRead &&
                 copy.textureBarriers[1].newState ==
                     RhiResourceState::TransferDst,
             "shadow depth copy must transition both source and destination") &&
         requireTrue(
             transparent.dependencies.size() == 1u &&
                 transparent.textureBarriers.size() == 3u &&
                 transparent.textureBarriers[0].oldState ==
                     RhiResourceState::TransferDst &&
                 transparent.textureBarriers[0].newState ==
                     RhiResourceState::DepthWrite,
             "transparent shadow pass must consume the copied depth layer") &&
         requireTrue(
             graph.compiledPasses()[3].textureBarriers[0].range.baseLayer ==
                 1u,
             "the second cascade must plan barriers only for its own layer") &&
         requireTrue(graph.epilogueTextureBarriers().size() == 8u,
                     "all shadow layers must return to stable frame states");
}

bool testTransientReadBeforeWriteValidation() {
  RenderGraph graph;
  const RgTextureHandle texture = graph.createTexture(
      {"Uninitialized", colorTextureDesc(), RhiResourceState::Undefined});
  graph.addPass({"InvalidRead", RgPassType::Graphics, RhiQueueType::Graphics})
      .readTexture(texture)
      .setExecute(executeNoop);
  const RgCompileResult result = graph.compile();
  return requireTrue(result.error == RgCompileError::ReadBeforeWrite,
                     "transient texture reads must require a prior writer");
}

bool testImportedResourceRead() {
  RenderGraph graph;
  const RgTextureHandle texture =
      graph.importTexture({"History",
                           {7u, 3u},
                           colorTextureDesc(),
                           RhiResourceState::ShaderRead,
                           RhiResourceState::ShaderRead,
                           {},
                           RhiQueueType::Graphics,
                           RhiQueueType::Graphics});
  graph.addPass({"HistoryRead", RgPassType::Graphics, RhiQueueType::Graphics})
      .readTexture(texture)
      .setExecute(executeNoop);
  const RgCompileResult result = graph.compile();
  return requireTrue(result.succeeded(), result.message.c_str()) &&
         requireTrue(
             graph.compiledPasses()[0].textureBarriers.empty(),
             "an imported texture in the requested state needs no barrier");
}

bool testBufferRangeCoverage() {
  RenderGraph graph;
  RhiBufferDesc desc;
  desc.debugName = "RenderGraphTest.Buffer";
  desc.size = 1024u;
  desc.usage =
      rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferDst);
  const RgBufferHandle buffer = graph.createBuffer(
      {"TransientBuffer", desc, RhiResourceState::StorageBuffer});
  graph.addPass({"UploadA", RgPassType::Copy, RhiQueueType::Transfer})
      .writeBuffer(buffer, RhiResourceState::TransferDst, {0u, 512u})
      .setExecute(executeNoop);
  graph.addPass({"UploadB", RgPassType::Copy, RhiQueueType::Transfer})
      .writeBuffer(buffer, RhiResourceState::TransferDst, {512u, 512u})
      .setExecute(executeNoop);
  graph.addPass({"Consume", RgPassType::Compute, RhiQueueType::Compute})
      .readBuffer(buffer, RhiResourceState::StorageBuffer, {0u, 1024u})
      .setExecute(executeNoop);

  const RgCompileResult result = graph.compile();
  return requireTrue(result.succeeded(), result.message.c_str()) &&
         requireTrue(
             graph.compiledPasses()[2].dependencies.size() == 2u,
             "a buffer consumer must depend on every overlapping writer") &&
         requireTrue(
             graph.epilogueBufferBarriers().size() == 1u &&
                 graph.epilogueBufferBarriers()[0].oldState ==
                     RhiResourceState::StorageBuffer &&
                 graph.epilogueBufferBarriers()[0].newState ==
                     RhiResourceState::StorageBuffer &&
                 graph.epilogueBufferBarriers()[0].sourceQueue ==
                     RhiQueueType::Compute &&
                 graph.epilogueBufferBarriers()[0].destinationQueue ==
                     RhiQueueType::Graphics,
             "the final buffer queue contract must return it to graphics");
}

bool testExplicitCycleValidation() {
  RenderGraph graph;
  const RgPassHandle a =
      graph.addPass({"A", RgPassType::Graphics, RhiQueueType::Graphics})
          .setExecute(executeNoop)
          .handle();
  const RgPassHandle b =
      graph.addPass({"B", RgPassType::Graphics, RhiQueueType::Graphics})
          .dependsOn(a)
          .setExecute(executeNoop)
          .handle();
  graph.editPass(a).dependsOn(b);
  const RgCompileResult result = graph.compile();
  return requireTrue(
             a.isValid() && b.isValid(),
             "pass handles must remain stable while building the graph") &&
         requireTrue(result.error == RgCompileError::CyclicDependency,
                     "explicit pass dependency cycles must be rejected");
}

bool testDuplicateAccessValidation() {
  RenderGraph graph;
  const RgTextureHandle texture =
      graph.importTexture({"Duplicate",
                           {4u, 2u},
                           colorTextureDesc(),
                           RhiResourceState::ShaderRead,
                           RhiResourceState::ShaderRead,
                           {},
                           RhiQueueType::Graphics,
                           RhiQueueType::Graphics});
  graph
      .addPass(
          {"DuplicateAccess", RgPassType::Graphics, RhiQueueType::Graphics})
      .readTexture(texture)
      .writeTexture(texture, RhiResourceState::RenderTarget)
      .setExecute(executeNoop);
  const RgCompileResult result = graph.compile();
  return requireTrue(result.error == RgCompileError::DuplicateResourceAccess,
                     "overlapping accesses in one pass must be rejected");
}

bool testResetInvalidatesHandles() {
  RenderGraph graph;
  const RgTextureHandle stale = graph.createTexture(
      {"BeforeReset", colorTextureDesc(), RhiResourceState::Undefined});
  graph.reset();
  const RgTextureHandle current = graph.createTexture(
      {"AfterReset", colorTextureDesc(), RhiResourceState::Undefined});
  graph.addPass({"StaleHandle", RgPassType::Graphics, RhiQueueType::Graphics})
      .writeTexture(stale, RhiResourceState::RenderTarget)
      .setExecute(executeNoop);
  const RgCompileResult result = graph.compile();
  return requireTrue(stale.index == current.index &&
                         stale.generation != current.generation,
                     "graph reset must advance resource handle generations") &&
         requireTrue(result.error == RgCompileError::InvalidTextureHandle,
                     "a resource handle from an earlier graph generation must "
                     "be rejected");
}

bool testUndefinedImportedReadValidation() {
  RenderGraph graph;
  const RgTextureHandle texture =
      graph.importTexture({"UndefinedImport",
                           {11u, 2u},
                           colorTextureDesc(),
                           RhiResourceState::Undefined,
                           RhiResourceState::ShaderRead,
                           {},
                           RhiQueueType::Graphics,
                           RhiQueueType::Graphics});
  graph
      .addPass(
          {"ReadUndefinedImport", RgPassType::Graphics, RhiQueueType::Graphics})
      .readTexture(texture)
      .setExecute(executeNoop);
  const RgCompileResult result = graph.compile();
  return requireTrue(
      result.error == RgCompileError::ReadBeforeWrite,
      "an imported resource in Undefined state must be written before reading");
}

bool testCrossQueueReadOwnershipDependency() {
  RenderGraph graph;
  const RgTextureHandle texture =
      graph.importTexture({"SharedRead",
                           {17u, 4u},
                           colorTextureDesc(),
                           RhiResourceState::ShaderRead,
                           RhiResourceState::ShaderRead,
                           {},
                           RhiQueueType::Graphics,
                           RhiQueueType::Compute});
  graph.addPass({"GraphicsRead", RgPassType::Graphics,
                 RhiQueueType::Graphics})
      .readTexture(texture)
      .setExecute(executeNoop);
  graph.addPass({"ComputeRead", RgPassType::Compute, RhiQueueType::Compute})
      .readTexture(texture)
      .setExecute(executeNoop);

  const RgCompileResult result = graph.compile();
  if (!requireTrue(result.succeeded(), result.message.c_str()))
    return false;
  const RgCompiledPass &source = graph.compiledPasses()[0];
  const RgCompiledPass &destination = graph.compiledPasses()[1];
  return requireTrue(destination.dependencies.size() == 1u &&
                         destination.dependencies[0] == 0u,
                     "cross-queue reads must preserve exclusive ownership") &&
         requireTrue(source.releaseTextureBarriers.size() == 1u,
                     "cross-family ownership must have a source release plan") &&
         requireTrue(destination.textureBarriers.size() == 1u &&
                         destination.textureBarriers[0].sourcePass == 0u,
                     "cross-queue read must have a paired destination plan");
}

bool testQueueOnlyEpiloguePlanning() {
  RenderGraph graph;
  RhiBufferDesc desc;
  desc.debugName = "RenderGraphTest.QueueOnlyEpilogue";
  desc.size = 256u;
  desc.usage = rhiFlag(RhiBufferUsage::TransferDst);
  const RgBufferHandle buffer = graph.createBuffer(
      {"QueueOnlyEpilogue", desc, RhiResourceState::TransferDst});
  graph.addPass({"TransferWrite", RgPassType::Copy,
                 RhiQueueType::Transfer})
      .writeBuffer(buffer, RhiResourceState::TransferDst)
      .setExecute(executeNoop);

  const RgCompileResult result = graph.compile();
  if (!requireTrue(result.succeeded(), result.message.c_str()))
    return false;
  return requireTrue(graph.epilogueBufferBarriers().size() == 1u,
                     "final queue changes require an epilogue barrier") &&
         requireTrue(
             graph.epilogueBufferBarriers()[0].oldState ==
                     RhiResourceState::TransferDst &&
                 graph.epilogueBufferBarriers()[0].newState ==
                     RhiResourceState::TransferDst &&
                 graph.epilogueBufferBarriers()[0].sourceQueue ==
                     RhiQueueType::Transfer &&
                 graph.epilogueBufferBarriers()[0].destinationQueue ==
                     RhiQueueType::Graphics &&
                 graph.epilogueBufferBarriers()[0].sourcePass == 0u,
             "queue-only epilogue must retain state and source pass") &&
         requireTrue(
             graph.compiledPasses()[0].releaseBufferBarriers.size() == 1u,
             "queue-only epilogue must plan a matching source release");
}

bool testBatchSplitForLateQueueDependency() {
  RenderGraph graph;
  RhiBufferDesc desc;
  desc.debugName = "RenderGraphTest.BatchSplit";
  desc.size = 256u;
  desc.usage = rhiFlag(RhiBufferUsage::Storage) |
               rhiFlag(RhiBufferUsage::TransferDst);
  const RgBufferHandle computeOutput =
      graph.createBuffer({"ComputeOutput", desc, RhiResourceState::Undefined});
  const RgBufferHandle graphicsOutput = graph.createBuffer(
      {"GraphicsOutput", desc, RhiResourceState::Undefined});
  graph.addPass({"ComputeProducer", RgPassType::Compute,
                 RhiQueueType::Compute})
      .writeBuffer(computeOutput, RhiResourceState::StorageBuffer)
      .setExecute(executeNoop);
  graph.addPass({"IndependentGraphics", RgPassType::Copy,
                 RhiQueueType::Graphics})
      .writeBuffer(graphicsOutput, RhiResourceState::TransferDst)
      .setExecute(executeNoop);
  graph.addPass({"DependentGraphics", RgPassType::Compute,
                 RhiQueueType::Graphics})
      .readBuffer(computeOutput, RhiResourceState::StorageBuffer)
      .setExecute(executeNoop);

  const RgCompileResult result = graph.compile();
  if (!requireTrue(result.succeeded(), result.message.c_str()))
    return false;
  return requireTrue(graph.submissionBatches().size() == 3u,
                     "a late cross-queue wait must start a new batch") &&
         requireTrue(graph.submissionBatches()[1].dependencies.empty(),
                     "independent graphics work must not wait for compute") &&
         requireTrue(graph.submissionBatches()[2].dependencies.size() == 1u &&
                         graph.submissionBatches()[2].dependencies[0] == 0u,
                     "the split graphics batch must wait for its producer");
}

bool testSubmissionBatchPassLimit() {
  RenderGraph graph;
  RgPassHandle previous;
  const uint32_t passCount = kRgMaxPassesPerSubmissionBatch * 2u + 1u;
  for (uint32_t index = 0u; index < passCount; ++index) {
    RenderGraphPassBuilder pass = graph.addPass(
        {"BatchLimitPass", RgPassType::Copy, RhiQueueType::Graphics});
    if (previous.isValid()) {
      pass.dependsOn(previous);
    }
    pass.setExecute(executeNoop);
    previous = pass.handle();
  }

  const RgCompileResult result = graph.compile();
  if (!requireTrue(result.succeeded(), result.message.c_str())) {
    return false;
  }
  const auto &batches = graph.submissionBatches();
  const uint32_t expectedBatchCount =
      (passCount + kRgMaxPassesPerSubmissionBatch - 1u) /
      kRgMaxPassesPerSubmissionBatch;
  if (!requireTrue(batches.size() == expectedBatchCount,
                   "same-queue passes must be split at the declared batch limit")) {
    return false;
  }
  for (uint32_t batchIndex = 0u; batchIndex < batches.size(); ++batchIndex) {
    const uint32_t expectedPasses =
        std::min(kRgMaxPassesPerSubmissionBatch,
                 passCount - batchIndex * kRgMaxPassesPerSubmissionBatch);
    if (!requireTrue(batches[batchIndex].passes.size() == expectedPasses,
                     "submission batches must respect the pass limit") ||
        !requireTrue(
            batchIndex == 0u ||
                (batches[batchIndex].dependencies.size() == 1u &&
                 batches[batchIndex].dependencies[0] == batchIndex - 1u),
            "serial same-queue batches must retain their graph dependency")) {
      return false;
    }
  }
  return true;
}
} // namespace

int main() {
  const bool passed =
      testTextureDependencyAndBarrierPlanning() &&
      testSubresourceIndependence() &&
      testThreeDimensionalTextureSubresources() &&
      testLayeredDepthCopyPlanning() &&
      testTransientReadBeforeWriteValidation() && testImportedResourceRead() &&
      testBufferRangeCoverage() && testExplicitCycleValidation() &&
      testDuplicateAccessValidation() && testResetInvalidatesHandles() &&
      testUndefinedImportedReadValidation() &&
      testCrossQueueReadOwnershipDependency() &&
      testQueueOnlyEpiloguePlanning() &&
      testBatchSplitForLateQueueDependency() &&
      testSubmissionBatchPassLimit();
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
