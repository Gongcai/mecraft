#include "renderer/rhi/RhiRenderGraph.h"

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
             graph.epilogueTextureBarriers().empty(),
             "the requested final texture state is already established") &&
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
                           RhiResourceState::ShaderRead});
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
             graph.epilogueBufferBarriers().empty(),
             "the final buffer state must be established by its consumer");
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
                           RhiResourceState::ShaderRead});
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
                           RhiResourceState::ShaderRead});
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
} // namespace

int main() {
  const bool passed =
      testTextureDependencyAndBarrierPlanning() &&
      testSubresourceIndependence() &&
      testTransientReadBeforeWriteValidation() && testImportedResourceRead() &&
      testBufferRangeCoverage() && testExplicitCycleValidation() &&
      testDuplicateAccessValidation() && testResetInvalidatesHandles() &&
      testUndefinedImportedReadValidation();
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
