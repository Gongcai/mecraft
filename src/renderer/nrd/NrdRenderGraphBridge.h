#ifndef MECRAFT_NRD_RENDER_GRAPH_BRIDGE_H
#define MECRAFT_NRD_RENDER_GRAPH_BRIDGE_H

#include "renderer/rhi/RhiRenderGraph.h"

#include <NRD.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>

class RhiDevice;

namespace renderer::nrd {

enum class NrdDiffuseMethod : uint8_t { Relax, Reblur };

enum class NrdBridgeError : uint8_t {
    None,
    AlreadyInitialized,
    InvalidDevice,
    UnsupportedDeviceCapabilities,
    InvalidExtent,
    InvalidMethod,
    UnsupportedLibraryContract,
    InstanceCreationFailed,
    UnsupportedInstanceContract,
    UnsupportedTextureFormat,
    ResourceCreationFailed,
    PipelineCreationFailed,
    NotInitialized,
    NoFramePending,
    FrameAlreadyPending,
    ExecutionStateInvalid,
    MethodSettingsMismatch,
    InvalidCommonSettings,
    DenoiserSettingsRejected,
    CommonSettingsRejected,
    DispatchQueryFailed,
    MissingExternalResource,
    InvalidDispatchContract,
    GraphResourceCreationFailed,
    GraphPassCreationFailed,
};

inline constexpr size_t kNrdResourceTypeCount = static_cast<size_t>(::nrd::ResourceType::MAX_NUM);

struct NrdExternalResources final {
    std::array<RgTextureHandle, kNrdResourceTypeCount> textures{};

    /// Associates one application-owned NRD resource type with an imported graph texture.
    /// @param type Supported diffuse NRD input or output slot; unrelated and pool types are rejected.
    /// @param texture Imported render graph texture matching the fixed NRD format contract.
    /// @return True when the external slot was accepted.
    [[nodiscard]] bool bind(::nrd::ResourceType type, RgTextureHandle texture);

    /// Returns the graph texture associated with one external NRD resource type.
    /// @param type External NRD resource slot to query.
    /// @return Bound graph texture, or an invalid handle for an unbound or pool slot.
    [[nodiscard]] RgTextureHandle texture(::nrd::ResourceType type) const;
};

using NrdDiffuseSettings = std::variant<::nrd::RelaxSettings, ::nrd::ReblurSettings>;

struct NrdGraphDispatchResult final {
    NrdBridgeError error = NrdBridgeError::None;
    RgPassHandle lastPass;
    uint32_t dispatchCount = 0u;

    [[nodiscard]] bool succeeded() const { return error == NrdBridgeError::None && lastPass.isValid(); }
};

/// Maps one NRD SDK texture format into the explicit RHI format used by the diffuse bridge.
/// @param format NRD texture format declared by an instance pool.
/// @return Matching RHI format, or no value when the fixed diffuse integration does not support it.
[[nodiscard]] std::optional<RhiTextureFormat> nrdTextureFormatToRhi(::nrd::Format format);

/// Returns the fixed application texture format for a supported diffuse NRD input or output.
/// @param type External NRD resource type.
/// @return Required RHI format, or no value for unsupported or internal pool types.
[[nodiscard]] std::optional<RhiTextureFormat> nrdDiffuseExternalTextureFormat(::nrd::ResourceType type);

/// Maps the user-facing diffuse mode to the exact NRD denoiser identifier.
/// @param method Fixed diffuse method selected when the bridge is initialized.
/// @return RELAX_DIFFUSE or REBLUR_DIFFUSE, or no value for an invalid enum value.
[[nodiscard]] std::optional<::nrd::Denoiser> nrdDiffuseDenoiser(NrdDiffuseMethod method);

/// Returns a stable diagnostic identifier for bridge initialization and frame errors.
/// @param error Bridge error to identify.
/// @return Process-lifetime stable identifier, or no value for an invalid enum value.
[[nodiscard]] std::optional<std::string_view> nrdBridgeErrorStableId(NrdBridgeError error);

/// Owns one fixed-size NRD instance and translates its dispatch list into Render Graph passes.
class NrdRenderGraphBridge final {
public:
    NrdRenderGraphBridge();
    ~NrdRenderGraphBridge();
    NrdRenderGraphBridge(const NrdRenderGraphBridge&) = delete;
    NrdRenderGraphBridge& operator=(const NrdRenderGraphBridge&) = delete;

    /// Creates the fixed NRD instance, persistent pool, descriptors, and compute pipelines.
    /// @param device Initialized Vulkan RHI device that owns all persistent resources.
    /// @param method RELAX_DIFFUSE or REBLUR_DIFFUSE selection for this instance lifetime.
    /// @param resourceWidth Maximum NRD resource width.
    /// @param resourceHeight Maximum NRD resource height.
    /// @return Exact initialization result.
    [[nodiscard]] NrdBridgeError initialize(RhiDevice& device, NrdDiffuseMethod method, uint16_t resourceWidth,
                                            uint16_t resourceHeight);

    /// Releases the instance and every RHI resource owned by the bridge.
    void shutdown();

    /// Adds one compute pass for every dispatch returned by the fixed NRD instance.
    /// @param graph Render graph receiving permanent imports, transient textures, and dispatch passes.
    /// @param commonSettings Complete NRD common settings for the current frame.
    /// @param methodSettings Settings whose variant must match the initialized method.
    /// @param externalResources Imported application textures used by this frame's dispatch list.
    /// @param dependency Optional pass that completed all external NRD inputs.
    /// @return Last dispatch pass, count, and an exact error code.
    [[nodiscard]] NrdGraphDispatchResult addGraphDispatches(RenderGraph& graph,
                                                            const ::nrd::CommonSettings& commonSettings,
                                                            const NrdDiffuseSettings& methodSettings,
                                                            const NrdExternalResources& externalResources,
                                                            RgPassHandle dependency = {});

    /// Commits persistent-pool state after the matching graph execution attempt.
    /// @param result Render Graph execution result for the graph populated by addGraphDispatches.
    void completeGraphExecution(const RgExecuteResult& result);

    [[nodiscard]] bool initialized() const;
    [[nodiscard]] bool framePending() const;
    /// Returns the diffuse method fixed for this instance lifetime.
    /// @return Initialized method, or no value before initialize succeeds.
    [[nodiscard]] std::optional<NrdDiffuseMethod> method() const;
    [[nodiscard]] uint16_t resourceWidth() const;
    [[nodiscard]] uint16_t resourceHeight() const;
    [[nodiscard]] uint32_t pipelineCount() const;
    [[nodiscard]] uint32_t permanentPoolSize() const;
    [[nodiscard]] uint32_t transientPoolSize() const;
    [[nodiscard]] NrdBridgeError lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    NrdBridgeError m_lastError = NrdBridgeError::None;
};

} // namespace renderer::nrd

#endif // MECRAFT_NRD_RENDER_GRAPH_BRIDGE_H
