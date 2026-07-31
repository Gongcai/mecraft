#ifndef MECRAFT_RENDER_FEATURE_CONTRACT_H
#define MECRAFT_RENDER_FEATURE_CONTRACT_H

#include "renderer/rhi/RhiTypes.h"

#include <cstddef>
#include <cstdint>

namespace renderer::contracts {

/// Identifies a renderer profile with a fixed backend and feature boundary.
enum class RenderProfile : uint8_t { OpenGlBase, VulkanModern };

/// Identifies every user-visible feature governed by the renderer capability contract.
enum class RenderFeature : uint8_t {
    DeferredPbr,
    CascadedSunShadows,
    Ssao,
    Ssgi,
    Ssr,
    GltfMaterials,
    ClusteredLighting,
    PbrImageBasedLighting,
    ReflectionProbeGrid,
    RayTracedGlobalIllumination,
    NrdDenoiser,
    MultiLayerTransparency,
    BindlessGpuScene,
    GpuDynamicResolution,
    HdrSwapchain,
    Count
};

/// Describes whether a feature belongs to a renderer profile's supported scope.
enum class RenderFeatureRequirement : uint8_t { Required, Unsupported };

/// Provides stable machine-readable results for settings, logs, tests, and diagnostics.
enum class RenderFeatureStatusCode : uint8_t {
    Available,
    BackendFeatureUnavailable,
    BuildFeatureUnavailable,
    DeviceCapabilityMissing,
    RayTracingCapabilityMissing,
    BindlessDescriptorCapabilityMissing,
    GpuTimingUnavailable,
    HdrDisplayModeUnavailable,
    ImplementationPending
};

/// Records optional components compiled into the current executable.
struct RenderBuildCapabilities {
    bool nrd = false;
};

/// Reports one feature's availability and an exact user-facing reason.
struct RenderFeatureStatus {
    RenderFeature feature = RenderFeature::DeferredPbr;
    RenderFeatureStatusCode code = RenderFeatureStatusCode::ImplementationPending;
    const char* reason = nullptr;

    /// Reports whether the feature can be selected and executed.
    /// @return True only when the status code is Available.
    [[nodiscard]] constexpr bool available() const { return code == RenderFeatureStatusCode::Available; }
};

/// Reports whether every required feature in a renderer profile is available.
struct RenderProfileStatus {
    RenderProfile profile = RenderProfile::OpenGlBase;
    RenderFeature blockingFeature = RenderFeature::Count;
    RenderFeatureStatusCode code = RenderFeatureStatusCode::ImplementationPending;
    const char* reason = nullptr;

    /// Reports whether the complete profile can be selected and executed.
    /// @return True only when all profile requirements are available.
    [[nodiscard]] constexpr bool available() const { return code == RenderFeatureStatusCode::Available; }
};

/// Returns the number of entries in the fixed renderer feature table.
/// @return Number of valid RenderFeature values excluding Count.
[[nodiscard]] constexpr size_t renderFeatureCount() {
    return static_cast<size_t>(RenderFeature::Count);
}

/// Maps an active RHI backend to the profile defined for that backend.
/// @param backend Active backend selected when the RHI device was created.
/// @return OpenGlBase for OpenGL and VulkanModern for Vulkan.
[[nodiscard]] RenderProfile activeRenderProfile(RhiBackend backend);

/// Returns the stable identifier used by logs and automated diagnostics.
/// @param profile Renderer profile to identify.
/// @return Process-lifetime string containing the stable profile identifier.
[[nodiscard]] const char* renderProfileStableId(RenderProfile profile);

/// Returns the English profile name displayed to users.
/// @param profile Renderer profile to name.
/// @return Process-lifetime string containing the display name.
[[nodiscard]] const char* renderProfileDisplayName(RenderProfile profile);

/// Returns the stable identifier used by logs and automated diagnostics.
/// @param feature Renderer feature to identify.
/// @return Process-lifetime string containing the stable feature identifier.
[[nodiscard]] const char* renderFeatureStableId(RenderFeature feature);

/// Returns the English feature name displayed to users.
/// @param feature Renderer feature to name.
/// @return Process-lifetime string containing the display name.
[[nodiscard]] const char* renderFeatureDisplayName(RenderFeature feature);

/// Returns the stable identifier for a structured availability result.
/// @param code Availability code to identify.
/// @return Process-lifetime string containing the stable error identifier.
[[nodiscard]] const char* renderFeatureStatusCodeStableId(RenderFeatureStatusCode code);

/// Looks up whether a feature belongs to a profile's supported feature scope.
/// @param profile Profile whose fixed table entry is queried.
/// @param feature Feature whose requirement is queried.
/// @return Required for profile features and Unsupported outside the profile boundary.
[[nodiscard]] RenderFeatureRequirement renderFeatureRequirement(RenderProfile profile, RenderFeature feature);

/// Returns optional components compiled into the current executable.
/// @return Build capability record derived only from compile-time integration switches.
[[nodiscard]] RenderBuildCapabilities currentRenderBuildCapabilities();

/// Returns the feature mask implemented by the current renderer source revision.
/// @return Bit mask whose set bits identify product-integrated RenderFeature values.
[[nodiscard]] uint64_t currentImplementedRenderFeatureMask();

/// Evaluates one feature without changing the requested algorithm or backend.
/// @param profile Profile selected by the caller.
/// @param backend Active RHI backend.
/// @param capabilities Capabilities exposed by the initialized RHI device.
/// @param buildCapabilities Optional components present in the executable.
/// @param implementedFeatureMask Product-integrated features represented as feature bits.
/// @param feature Feature to evaluate.
/// @return Structured availability code and an English user-facing reason.
[[nodiscard]] RenderFeatureStatus evaluateRenderFeature(RenderProfile profile, RhiBackend backend,
                                                        const RhiCapabilities& capabilities,
                                                        const RenderBuildCapabilities& buildCapabilities,
                                                        uint64_t implementedFeatureMask, RenderFeature feature);

/// Evaluates one feature against the current executable's build and implementation state.
/// @param profile Profile selected by the caller.
/// @param backend Active RHI backend.
/// @param capabilities Capabilities exposed by the initialized RHI device.
/// @param feature Feature to evaluate.
/// @return Structured availability code and an English user-facing reason.
[[nodiscard]] RenderFeatureStatus evaluateCurrentRenderFeature(RenderProfile profile, RhiBackend backend,
                                                               const RhiCapabilities& capabilities,
                                                               RenderFeature feature);

/// Evaluates all required features and returns the first deterministic blocker.
/// @param profile Profile selected by the caller.
/// @param backend Active RHI backend.
/// @param capabilities Capabilities exposed by the initialized RHI device.
/// @param buildCapabilities Optional components present in the executable.
/// @param implementedFeatureMask Product-integrated features represented as feature bits.
/// @return Available when the complete profile is ready, otherwise its first blocking status.
[[nodiscard]] RenderProfileStatus evaluateRenderProfile(RenderProfile profile, RhiBackend backend,
                                                        const RhiCapabilities& capabilities,
                                                        const RenderBuildCapabilities& buildCapabilities,
                                                        uint64_t implementedFeatureMask);

/// Evaluates a profile against the current executable's build and implementation state.
/// @param profile Profile selected by the caller.
/// @param backend Active RHI backend.
/// @param capabilities Capabilities exposed by the initialized RHI device.
/// @return Available when the complete current profile can be selected and executed.
[[nodiscard]] RenderProfileStatus evaluateCurrentRenderProfile(RenderProfile profile, RhiBackend backend,
                                                               const RhiCapabilities& capabilities);

} // namespace renderer::contracts

#endif // MECRAFT_RENDER_FEATURE_CONTRACT_H
