#ifndef MECRAFT_REFLECTION_PROBE_CAPTURE_PASS_H
#define MECRAFT_REFLECTION_PROBE_CAPTURE_PASS_H

#include "RenderPass.h"
#include "renderer/contracts/ReflectionProbeContract.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiRenderGraph.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

class FrameContext;
class RhiCommandList;
class RhiDevice;

/// Describes one world-space probe whose captured image is versioned by the
/// producer. Spatial fields are converted to camera-relative GPU records when
/// the frame is prepared.
struct ReflectionProbeCaptureSource final {
    renderer::contracts::StableReflectionProbeId probeId;
    glm::vec3 positionWorldMeters{0.0f};
    float exposureScale = 1.0f;
    glm::vec3 influenceMinWorldMeters{0.0f};
    glm::vec3 influenceMaxWorldMeters{0.0f};
    float blendDistanceMeters = 0.0f;
    glm::vec3 boxProjectionMinWorldMeters{0.0f};
    glm::vec3 boxProjectionMaxWorldMeters{0.0f};
    uint32_t requestedRevision = 0u;
};

/// Identifies one deterministic capture work item in a probe generation.
struct ReflectionProbeCaptureWork final {
    uint32_t probeIndex = 0u;
    uint32_t cubemapIndex = 0u;
    uint32_t workItem = 0u;
    uint32_t face = 0u;
    uint32_t mip = 0u;
    glm::vec3 positionWorldMeters{0.0f};
    glm::mat4 viewProjection{1.0f};
    RhiTextureViewHandle targetView;
};

/// Records one six-face HDR scene capture using the same material and direct
/// lighting resources as the main view. The callback owns the render scope.
class IReflectionProbeCaptureRenderer {
public:
    virtual ~IReflectionProbeCaptureRenderer() = default;

    /// Records one radiance face into the supplied target view.
    /// @param commandList Graphics command list owned by the Render Graph.
    /// @param context Main-frame state shared with the capture renderer.
    /// @param work Face index, camera transform, and target view for the work item.
    /// @return True when the complete face was recorded.
    [[nodiscard]] virtual bool recordReflectionProbeRadianceFace(
        RhiCommandList& commandList,
        const FrameContext& context,
        const ReflectionProbeCaptureWork& work) = 0;
};

/// Owns double-buffered radiance and prefiltered probe capture products.
class ReflectionProbeCapturePass final : public RenderPass {
public:
    struct GraphResources final {
        RgTextureHandle radiance;
        RgTextureHandle prefiltered;
    };

    struct ConsumerResources final {
        RhiTextureHandle prefilteredTexture;
        RhiTextureViewHandle prefilteredView;
        uint32_t slotCapacity = 0u;
    };

    void shutdown() override;
    [[nodiscard]] const char* name() const override {
        return "ReflectionProbeCapture";
    }

    /// Replaces the complete world-space source snapshot.
    /// @param sources Probe placement and requested capture revisions.
    void setSources(std::vector<ReflectionProbeCaptureSource> sources);

    /// Installs the scene renderer used for six-face radiance capture.
    /// @param renderer Explicit capture renderer; pending face work requires it.
    void setCaptureRenderer(IReflectionProbeCaptureRenderer* renderer) {
        m_captureRenderer = renderer;
    }

    /// Validates sources, allocates capture arrays, and selects the current
    /// active probe snapshot for this camera.
    /// @param rhiDevice Device that owns the capture resources.
    /// @param cameraPositionWorld Main-view world-space camera position.
    /// @return True when the source snapshot and resources are ready.
    [[nodiscard]] bool prepareFrame(RhiDevice& rhiDevice,
                                    const glm::vec3& cameraPositionWorld);

    /// Imports persistent capture arrays with their exact cross-frame state.
    [[nodiscard]] bool importGraphResources(RenderGraph& graph,
                                            GraphResources& resources) const;

    /// Adds at most one deterministic face or prefilter work item.
    /// @param graph Render Graph receiving the capture work.
    /// @param resources Imported radiance and prefilter arrays.
    /// @param context Main-frame state shared with the capture renderer.
    /// @param dependency Previous graph tail.
    /// @return New graph tail or an invalid handle on contract failure.
    [[nodiscard]] RgPassHandle addGraphPasses(
        RenderGraph& graph,
        const GraphResources& resources,
        const FrameContext& context,
        RgPassHandle dependency);

    /// Commits the scheduled work only after complete graph submission.
    void finishGraphExecution(bool succeeded);

    [[nodiscard]] const std::vector<renderer::contracts::GpuReflectionProbe>&
    activeProbes() const {
        return m_activeProbes;
    }
    [[nodiscard]] ConsumerResources consumerResources() const;
    [[nodiscard]] bool hasPendingWork() const;
    [[nodiscard]] bool hasSources() const {
        return !m_sources.empty();
    }
    [[nodiscard]] const std::string& lastError() const {
        return m_lastError;
    }

private:
    struct ProbeState final {
        ReflectionProbeCaptureSource source;
        ReflectionProbeCaptureSource activeSource;
        uint32_t slotBase = 0u;
        uint32_t activeSlot = renderer::contracts::kReflectionProbeInvalidCubemapIndex;
        uint32_t buildSlot = renderer::contracts::kReflectionProbeInvalidCubemapIndex;
        uint32_t activeRevision = 0u;
        uint32_t buildRevision = 0u;
        uint32_t nextWorkItem = 0u;
        bool active = false;
        bool building = false;
    };

    [[nodiscard]] bool rebuildSources(const glm::vec3& cameraPositionWorld);
    [[nodiscard]] bool ensureResources(RhiDevice& rhiDevice,
                                       uint32_t requiredSlotCapacity);
    [[nodiscard]] bool createPipelines(RhiDevice& rhiDevice);
    [[nodiscard]] bool createViews(RhiDevice& rhiDevice,
                                   uint32_t slotCapacity);
    [[nodiscard]] bool recordRadianceFace(RhiCommandList& commandList,
                                          const FrameContext& context) const;
    [[nodiscard]] bool recordPrefilter(RhiCommandList& commandList) const;
    [[nodiscard]] ReflectionProbeCaptureWork buildWork(
        const ProbeState& state,
        uint32_t probeIndex,
        uint32_t workItem) const;
    [[nodiscard]] bool buildActiveProbe(
        const ProbeState& state,
        const glm::vec3& cameraPositionWorld,
        renderer::contracts::GpuReflectionProbe& probe) const;
    void destroyResources();

    RhiDevice* m_rhiDevice = nullptr;
    IReflectionProbeCaptureRenderer* m_captureRenderer = nullptr;
    std::vector<ReflectionProbeCaptureSource> m_sources;
    std::vector<ProbeState> m_states;
    std::vector<renderer::contracts::GpuReflectionProbe> m_activeProbes;
    glm::vec3 m_cameraPositionWorld{0.0f};
    uint32_t m_slotCapacity = 0u;
    uint32_t m_queueCursor = 0u;
    uint32_t m_scheduledProbeIndex = 0u;
    uint32_t m_scheduledWorkItem = 0u;
    bool m_workScheduled = false;
    bool m_radianceInitialized = false;
    bool m_prefilteredInitialized = false;
    uint64_t m_sourceRevision = 1u;
    uint64_t m_preparedRevision = 0u;
    RhiTextureHandle m_radianceTexture;
    RhiTextureHandle m_prefilteredTexture;
    RhiTextureViewHandle m_radianceView;
    RhiTextureViewHandle m_prefilteredView;
    std::vector<std::vector<RhiTextureViewHandle>> m_radianceFaceViews;
    std::vector<RhiTextureViewHandle> m_radianceCubeViews;
    std::vector<std::vector<std::vector<RhiTextureViewHandle>>>
        m_prefilterFaceMipViews;
    std::vector<RhiBindGroupHandle> m_prefilterBindGroups;
    RhiSamplerHandle m_linearClampSampler;
    RhiShaderHandle m_fullscreenVertexShader;
    RhiShaderHandle m_prefilterFragmentShader;
    RhiBindGroupLayoutHandle m_prefilterBindGroupLayout;
    RhiPipelineLayoutHandle m_prefilterPipelineLayout;
    RhiPipelineHandle m_prefilterPipeline;
    std::string m_lastError;
};

#endif // MECRAFT_REFLECTION_PROBE_CAPTURE_PASS_H
