#ifndef MECRAFT_SKY_IBL_PASS_H
#define MECRAFT_SKY_IBL_PASS_H

#include "../contracts/SkyIblContract.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"
#include "RenderPass.h"

#include <array>
#include <cstdint>

class RhiCommandList;
class RhiDevice;

/// Produces the HDR sky cubemap, GGX prefilter chain, and split-sum DFG LUT.
class SkyIblPass : public RenderPass {
public:
  struct GraphGeneration {
    RgTextureHandle radiance;
    RgTextureHandle specularPrefilter;
  };

  struct GraphResources {
    std::array<GraphGeneration,
               renderer::contracts::kSkyIblGenerationCount>
        generations;
    RgTextureHandle consumerSpecularPrefilter;
    RgTextureHandle dfgLut;
  };

  void shutdown() override;
  [[nodiscard]] const char *name() const override { return "SkyIbl"; }

  /// Creates persistent IBL products and the pipelines that generate them.
  /// @param rhiDevice Device that owns the generated products.
  /// @param frameIndex Monotonic frame identifier used to request revisions.
  /// @return True when every texture, view, descriptor, and pipeline is valid.
  [[nodiscard]] bool prepareFrame(RhiDevice &rhiDevice,
                                  RhiTextureViewHandle skyCaptureView,
                                  uint64_t frameIndex);

  /// Imports persistent IBL products with their exact cross-frame state.
  /// @param graph Render graph receiving the imported products.
  /// @param resources Output graph handles consumed by generation and lighting.
  /// @return True when all products were imported successfully.
  [[nodiscard]] bool importGraphResources(RenderGraph &graph,
                                          GraphResources &resources) const;

  /// Adds sky conversion, GGX convolution, and required DFG generation passes.
  /// @param graph Render graph receiving the generation passes.
  /// @param skyCapture Equirectangular HDR sky atlas generated this frame.
  /// @param resources Imported persistent IBL products.
  /// @param dependency Pass that completed the equirectangular sky capture.
  /// @return Final generation pass, or an invalid handle for an invalid
  /// contract.
  [[nodiscard]] RgPassHandle addGraphPasses(RenderGraph &graph,
                                            RgTextureHandle skyCapture,
                                            const GraphResources &resources,
                                            RgPassHandle dependency);

  /// Commits resource initialization only after the complete graph submission.
  /// @param succeeded Whether every generation and consumer submission
  /// succeeded.
  void finishGraphExecution(bool succeeded);

  [[nodiscard]] RhiTextureViewHandle specularPrefilterView() const {
    return m_consumerGeneration < renderer::contracts::kSkyIblGenerationCount
               ? m_generations[m_consumerGeneration].specularPrefilterView
               : RhiTextureViewHandle{};
  }
  [[nodiscard]] RhiTextureViewHandle dfgLutView() const { return m_dfgLutView; }

  [[nodiscard]] uint64_t committedRevision() const {
    return m_committedRevision;
  }
  [[nodiscard]] uint64_t buildingRevision() const { return m_buildRevision; }
  [[nodiscard]] uint32_t completedBuildWorkItems() const {
    return m_nextPrefilterWorkItem;
  }

private:
  struct GenerationResources {
    RhiTextureHandle radianceTexture;
    RhiTextureHandle specularPrefilterTexture;
    RhiTextureViewHandle radianceView;
    RhiTextureViewHandle specularPrefilterView;
    std::array<RhiTextureViewHandle,
               renderer::contracts::kSkyIblCubeFaceCount>
        radianceFaceViews{};
    std::array<
        std::array<RhiTextureViewHandle,
                   renderer::contracts::kSkyIblCubeFaceCount>,
        renderer::contracts::kSkyIblCubeMipCount>
        specularFaceMipViews{};
    RhiBindGroupHandle prefilterBindGroup;
    bool radianceStateInitialized = false;
    bool prefilterStateInitialized = false;
    bool complete = false;
    uint64_t revision = 0u;
  };

  [[nodiscard]] bool createResources(RhiDevice &rhiDevice);
  [[nodiscard]] bool createPipelines(RhiDevice &rhiDevice);
  [[nodiscard]] bool createViews(RhiDevice &rhiDevice);
  [[nodiscard]] bool createBindGroups(RhiDevice &rhiDevice,
                                      RhiTextureViewHandle skyCaptureView);
  [[nodiscard]] bool recordSkyRadiance(RhiCommandList &commandList,
                                       uint32_t generation) const;
  [[nodiscard]] bool recordSpecularPrefilter(RhiCommandList &commandList,
                                             uint32_t generation,
                                             uint32_t firstWorkItem,
                                             uint32_t workItemCount) const;
  [[nodiscard]] bool recordDfgLut(RhiCommandList &commandList) const;
  void beginBuild(uint32_t generation, uint64_t revision, bool bootstrap);
  void destroyResources();

  RhiDevice *m_rhiDevice = nullptr;
  std::array<GenerationResources,
             renderer::contracts::kSkyIblGenerationCount>
      m_generations;
  RhiTextureHandle m_dfgLutTexture;
  RhiTextureViewHandle m_dfgLutView;
  RhiSamplerHandle m_linearClampSampler;
  RhiShaderHandle m_fullscreenVertexShader;
  RhiShaderHandle m_skyRadianceFragmentShader;
  RhiShaderHandle m_prefilterFragmentShader;
  RhiShaderHandle m_dfgFragmentShader;
  RhiBindGroupLayoutHandle m_skyRadianceBindGroupLayout;
  RhiBindGroupLayoutHandle m_prefilterBindGroupLayout;
  RhiPipelineLayoutHandle m_skyRadiancePipelineLayout;
  RhiPipelineLayoutHandle m_prefilterPipelineLayout;
  RhiPipelineLayoutHandle m_dfgPipelineLayout;
  RhiPipelineHandle m_skyRadiancePipeline;
  RhiPipelineHandle m_prefilterPipeline;
  RhiPipelineHandle m_dfgPipeline;
  RhiBindGroupHandle m_skyRadianceBindGroup;
  RhiTextureViewHandle m_boundSkyCaptureView;
  uint32_t m_activeGeneration = renderer::contracts::kSkyIblGenerationCount;
  uint32_t m_buildGeneration = renderer::contracts::kSkyIblGenerationCount;
  uint32_t m_consumerGeneration = renderer::contracts::kSkyIblGenerationCount;
  uint32_t m_nextPrefilterWorkItem = 0u;
  uint32_t m_scheduledWorkItemCount = 0u;
  uint64_t m_requestedRevision = 0u;
  uint64_t m_buildRevision = 0u;
  uint64_t m_committedRevision = 0u;
  bool m_buildNeedsRadiance = false;
  bool m_bootstrapBuild = false;
  bool m_radianceScheduled = false;
  bool m_prefilterScheduled = false;
  bool m_dfgReady = false;
  bool m_dfgScheduled = false;
};

#endif // MECRAFT_SKY_IBL_PASS_H
