#ifndef MECRAFT_SKY_IBL_PASS_H
#define MECRAFT_SKY_IBL_PASS_H

#include "../contracts/SkyIblContract.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"
#include "RenderPass.h"

#include <array>

class RhiCommandList;
class RhiDevice;

/// Produces the HDR sky cubemap, GGX prefilter chain, and split-sum DFG LUT.
class SkyIblPass : public RenderPass {
public:
  struct GraphResources {
    RgTextureHandle skyRadiance;
    RgTextureHandle specularPrefilter;
    RgTextureHandle dfgLut;
  };

  void shutdown() override;
  [[nodiscard]] const char *name() const override { return "SkyIbl"; }

  /// Creates persistent IBL products and the pipelines that generate them.
  /// @param rhiDevice Device that owns the generated products.
  /// @return True when every texture, view, descriptor, and pipeline is valid.
  [[nodiscard]] bool prepareFrame(RhiDevice &rhiDevice,
                                  RhiTextureViewHandle skyCaptureView);

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
    return m_specularPrefilterView;
  }
  [[nodiscard]] RhiTextureViewHandle dfgLutView() const { return m_dfgLutView; }

private:
  [[nodiscard]] bool createResources(RhiDevice &rhiDevice);
  [[nodiscard]] bool createPipelines(RhiDevice &rhiDevice);
  [[nodiscard]] bool createViews(RhiDevice &rhiDevice);
  [[nodiscard]] bool createBindGroups(RhiDevice &rhiDevice,
                                      RhiTextureViewHandle skyCaptureView);
  [[nodiscard]] bool recordSkyRadiance(RhiCommandList &commandList) const;
  [[nodiscard]] bool recordSpecularPrefilter(RhiCommandList &commandList) const;
  [[nodiscard]] bool recordDfgLut(RhiCommandList &commandList) const;
  void destroyResources();

  RhiDevice *m_rhiDevice = nullptr;
  RhiTextureHandle m_skyRadianceTexture;
  RhiTextureHandle m_specularPrefilterTexture;
  RhiTextureHandle m_dfgLutTexture;
  RhiTextureViewHandle m_skyRadianceView;
  RhiTextureViewHandle m_specularPrefilterView;
  RhiTextureViewHandle m_dfgLutView;
  std::array<RhiTextureViewHandle, 6u> m_skyRadianceFaceViews = {};
  std::array<std::array<RhiTextureViewHandle, 6u>,
             renderer::contracts::kSkyIblCubeMipCount>
      m_specularFaceMipViews = {};
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
  RhiBindGroupHandle m_prefilterBindGroup;
  RhiTextureViewHandle m_boundSkyCaptureView;
  bool m_productsInitialized = false;
  bool m_dfgReady = false;
  bool m_dfgScheduled = false;
};

#endif // MECRAFT_SKY_IBL_PASS_H
