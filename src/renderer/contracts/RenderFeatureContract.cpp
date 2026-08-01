#include "renderer/contracts/RenderFeatureContract.h"

#include <array>
#include <cstdlib>

namespace renderer::contracts {
namespace {
struct FeatureDefinition {
    RenderFeature feature;
    const char* stableId;
    const char* displayName;
    bool openGlBase;
    bool vulkanModern;
};

constexpr std::array<FeatureDefinition, renderFeatureCount()> kFeatureDefinitions{{
    {RenderFeature::DeferredPbr, "DeferredPbr", "Deferred PBR", true, true},
    {RenderFeature::CascadedSunShadows, "CascadedSunShadows", "Cascaded Sun Shadows", true, true},
    {RenderFeature::Ssao, "Ssao", "Screen-Space Ambient Occlusion", true, true},
    {RenderFeature::Ssgi, "Ssgi", "Screen-Space Global Illumination Debug Mode", true, true},
    {RenderFeature::Ssr, "Ssr", "Screen-Space Reflection Debug Mode", true, true},
    {RenderFeature::GltfMaterials, "GltfMaterials", "glTF Materials", true, true},
    {RenderFeature::ClusteredLighting, "ClusteredLighting", "Clustered Lighting", false, true},
    {RenderFeature::PbrImageBasedLighting, "PbrImageBasedLighting", "GGX Image-Based Lighting", false, true},
    {RenderFeature::ReflectionProbeGrid, "ReflectionProbeGrid", "Reflection Probe Grid", false, true},
    {RenderFeature::RayTracedGlobalIllumination, "RayTracedGlobalIllumination", "Ray-Traced Global Illumination", false,
     true},
    {RenderFeature::NrdDenoiser, "NrdDenoiser", "NRD Spatiotemporal Denoising", false, true},
    {RenderFeature::MultiLayerTransparency, "MultiLayerTransparency", "Multi-Layer Transparency", false, true},
    {RenderFeature::BindlessGpuScene, "BindlessGpuScene", "Bindless GPU Scene", false, true},
    {RenderFeature::GpuDynamicResolution, "GpuDynamicResolution", "GPU Dynamic Resolution", false, true},
    {RenderFeature::HdrSwapchain, "HdrSwapchain", "HDR Swapchain", false, true},
}};

constexpr uint64_t featureBit(const RenderFeature feature) {
    return uint64_t{1u} << static_cast<uint8_t>(feature);
}

const FeatureDefinition& definition(const RenderFeature feature) {
    const size_t index = static_cast<size_t>(feature);
    if (index >= kFeatureDefinitions.size() || kFeatureDefinitions[index].feature != feature) {
        std::abort();
    }
    return kFeatureDefinitions[index];
}

bool profileMatchesBackend(const RenderProfile profile, const RhiBackend backend) {
    switch (profile) {
    case RenderProfile::OpenGlBase: return backend == RhiBackend::OpenGL;
    case RenderProfile::VulkanModern: return backend == RhiBackend::Vulkan;
    }
    std::abort();
}

const char* missingDeviceReason(const RenderFeature feature) {
    switch (feature) {
    case RenderFeature::ClusteredLighting:
    case RenderFeature::MultiLayerTransparency: return "The device or driver does not provide storage images.";
    case RenderFeature::PbrImageBasedLighting:
    case RenderFeature::ReflectionProbeGrid:
        return "The device or driver does not provide texture views and anisotropic sampling.";
    case RenderFeature::RayTracedGlobalIllumination:
        return "The device or driver does not provide acceleration structures, ray queries, and buffer device "
               "addresses.";
    case RenderFeature::NrdDenoiser:
        return "The device or driver does not provide the compute storage images required by NRD.";
    case RenderFeature::BindlessGpuScene:
        return "The device or driver does not provide the required bindless descriptor features.";
    case RenderFeature::GpuDynamicResolution: return "The device or driver does not provide GPU timestamp queries.";
    case RenderFeature::HdrSwapchain: return "The display surface does not provide an HDR10 or scRGB swapchain.";
    case RenderFeature::DeferredPbr:
    case RenderFeature::CascadedSunShadows:
    case RenderFeature::Ssao:
    case RenderFeature::Ssgi:
    case RenderFeature::Ssr:
    case RenderFeature::GltfMaterials:
        return "The device or driver does not provide the required graphics capabilities.";
    case RenderFeature::Count: std::abort();
    }
    std::abort();
}

RenderFeatureStatusCode missingDeviceStatusCode(const RenderFeature feature) {
    switch (feature) {
    case RenderFeature::RayTracedGlobalIllumination: return RenderFeatureStatusCode::RayTracingCapabilityMissing;
    case RenderFeature::BindlessGpuScene: return RenderFeatureStatusCode::BindlessDescriptorCapabilityMissing;
    case RenderFeature::GpuDynamicResolution: return RenderFeatureStatusCode::GpuTimingUnavailable;
    case RenderFeature::HdrSwapchain: return RenderFeatureStatusCode::HdrDisplayModeUnavailable;
    case RenderFeature::DeferredPbr:
    case RenderFeature::CascadedSunShadows:
    case RenderFeature::Ssao:
    case RenderFeature::Ssgi:
    case RenderFeature::Ssr:
    case RenderFeature::GltfMaterials:
    case RenderFeature::ClusteredLighting:
    case RenderFeature::PbrImageBasedLighting:
    case RenderFeature::ReflectionProbeGrid:
    case RenderFeature::NrdDenoiser:
    case RenderFeature::MultiLayerTransparency: return RenderFeatureStatusCode::DeviceCapabilityMissing;
    case RenderFeature::Count: std::abort();
    }
    std::abort();
}

bool deviceSupportsFeature(const RhiCapabilities& capabilities, const RenderFeature feature) {
    switch (feature) {
    case RenderFeature::DeferredPbr: return capabilities.maxColorAttachments >= 8u;
    case RenderFeature::CascadedSunShadows: return capabilities.textureView;
    case RenderFeature::Ssao:
    case RenderFeature::Ssgi:
    case RenderFeature::Ssr: return capabilities.storageImage && capabilities.textureView;
    case RenderFeature::GltfMaterials: return capabilities.samplerAnisotropy;
    case RenderFeature::ClusteredLighting:
    case RenderFeature::MultiLayerTransparency: return capabilities.storageImage;
    case RenderFeature::PbrImageBasedLighting:
    case RenderFeature::ReflectionProbeGrid: return capabilities.textureView && capabilities.samplerAnisotropy;
    case RenderFeature::RayTracedGlobalIllumination:
        return capabilities.accelerationStructure && capabilities.rayQuery && capabilities.bufferDeviceAddress;
    case RenderFeature::NrdDenoiser: return capabilities.storageImage;
    case RenderFeature::BindlessGpuScene:
        return capabilities.accelerationStructure && capabilities.descriptorIndexing &&
               capabilities.descriptorBindingPartiallyBound && capabilities.descriptorBindingVariableDescriptorCount &&
               capabilities.runtimeDescriptorArray && capabilities.descriptorBindingUpdateUnusedWhilePending &&
               capabilities.descriptorBindingSampledImageUpdateAfterBind &&
               capabilities.descriptorBindingStorageBufferUpdateAfterBind &&
               capabilities.descriptorBindingAccelerationStructureUpdateAfterBind &&
               capabilities.shaderSampledImageArrayNonUniformIndexing &&
               capabilities.shaderStorageBufferArrayNonUniformIndexing;
    case RenderFeature::GpuDynamicResolution: return capabilities.timestampQuery;
    case RenderFeature::HdrSwapchain: return capabilities.hdr10Swapchain || capabilities.scRgbSwapchain;
    case RenderFeature::Count: std::abort();
    }
    std::abort();
}
} // namespace

RenderProfile activeRenderProfile(const RhiBackend backend) {
    switch (backend) {
    case RhiBackend::OpenGL: return RenderProfile::OpenGlBase;
    case RhiBackend::Vulkan: return RenderProfile::VulkanModern;
    }
    std::abort();
}

const char* renderProfileStableId(const RenderProfile profile) {
    switch (profile) {
    case RenderProfile::OpenGlBase: return "OpenGlBase";
    case RenderProfile::VulkanModern: return "VulkanModern";
    }
    std::abort();
}

const char* renderProfileDisplayName(const RenderProfile profile) {
    switch (profile) {
    case RenderProfile::OpenGlBase: return "OpenGL Base Pipeline";
    case RenderProfile::VulkanModern: return "Vulkan Modern Pipeline";
    }
    std::abort();
}

const char* renderFeatureStableId(const RenderFeature feature) {
    return definition(feature).stableId;
}

const char* renderFeatureDisplayName(const RenderFeature feature) {
    return definition(feature).displayName;
}

const char* renderFeatureStatusCodeStableId(const RenderFeatureStatusCode code) {
    switch (code) {
    case RenderFeatureStatusCode::Available: return "Available";
    case RenderFeatureStatusCode::BackendFeatureUnavailable: return "BackendFeatureUnavailable";
    case RenderFeatureStatusCode::BuildFeatureUnavailable: return "BuildFeatureUnavailable";
    case RenderFeatureStatusCode::DeviceCapabilityMissing: return "DeviceCapabilityMissing";
    case RenderFeatureStatusCode::RayTracingCapabilityMissing: return "RayTracingCapabilityMissing";
    case RenderFeatureStatusCode::BindlessDescriptorCapabilityMissing: return "BindlessDescriptorCapabilityMissing";
    case RenderFeatureStatusCode::GpuTimingUnavailable: return "GpuTimingUnavailable";
    case RenderFeatureStatusCode::HdrDisplayModeUnavailable: return "HdrDisplayModeUnavailable";
    case RenderFeatureStatusCode::ImplementationPending: return "ImplementationPending";
    }
    std::abort();
}

RenderFeatureRequirement renderFeatureRequirement(const RenderProfile profile, const RenderFeature feature) {
    const FeatureDefinition& featureDefinition = definition(feature);
    switch (profile) {
    case RenderProfile::OpenGlBase:
        return featureDefinition.openGlBase ? RenderFeatureRequirement::Required
                                            : RenderFeatureRequirement::Unsupported;
    case RenderProfile::VulkanModern:
        return featureDefinition.vulkanModern ? RenderFeatureRequirement::Required
                                              : RenderFeatureRequirement::Unsupported;
    }
    std::abort();
}

RenderBuildCapabilities currentRenderBuildCapabilities() {
    RenderBuildCapabilities capabilities;
#if defined(MECRAFT_ENABLE_NRD)
    capabilities.nrd = true;
#endif
    return capabilities;
}

uint64_t currentImplementedRenderFeatureMask() {
    return featureBit(RenderFeature::DeferredPbr) | featureBit(RenderFeature::CascadedSunShadows) |
           featureBit(RenderFeature::Ssao) | featureBit(RenderFeature::Ssgi) | featureBit(RenderFeature::Ssr) |
           featureBit(RenderFeature::GltfMaterials) | featureBit(RenderFeature::ClusteredLighting) |
           featureBit(RenderFeature::PbrImageBasedLighting);
}

RenderFeatureStatus evaluateRenderFeature(const RenderProfile profile, const RhiBackend backend,
                                          const RhiCapabilities& capabilities,
                                          const RenderBuildCapabilities& buildCapabilities,
                                          const uint64_t implementedFeatureMask, const RenderFeature feature) {
    RenderFeatureStatus status;
    status.feature = feature;
    if (!profileMatchesBackend(profile, backend) ||
        renderFeatureRequirement(profile, feature) == RenderFeatureRequirement::Unsupported) {
        status.code = RenderFeatureStatusCode::BackendFeatureUnavailable;
        status.reason = "The active rendering backend does not support this feature.";
        return status;
    }
    if (feature == RenderFeature::NrdDenoiser && !buildCapabilities.nrd) {
        status.code = RenderFeatureStatusCode::BuildFeatureUnavailable;
        status.reason = "This build does not include the NRD component.";
        return status;
    }
    if ((implementedFeatureMask & featureBit(feature)) == 0u) {
        status.code = RenderFeatureStatusCode::ImplementationPending;
        status.reason = "This feature has not completed product integration.";
        return status;
    }
    if (!deviceSupportsFeature(capabilities, feature)) {
        status.code = missingDeviceStatusCode(feature);
        status.reason = missingDeviceReason(feature);
        return status;
    }
    status.code = RenderFeatureStatusCode::Available;
    status.reason = "Available.";
    return status;
}

RenderFeatureStatus evaluateCurrentRenderFeature(const RenderProfile profile, const RhiBackend backend,
                                                 const RhiCapabilities& capabilities, const RenderFeature feature) {
    return evaluateRenderFeature(profile, backend, capabilities, currentRenderBuildCapabilities(),
                                 currentImplementedRenderFeatureMask(), feature);
}

RenderProfileStatus evaluateRenderProfile(const RenderProfile profile, const RhiBackend backend,
                                          const RhiCapabilities& capabilities,
                                          const RenderBuildCapabilities& buildCapabilities,
                                          const uint64_t implementedFeatureMask) {
    RenderProfileStatus profileStatus;
    profileStatus.profile = profile;
    if (!profileMatchesBackend(profile, backend)) {
        profileStatus.blockingFeature = RenderFeature::Count;
        profileStatus.code = RenderFeatureStatusCode::BackendFeatureUnavailable;
        profileStatus.reason = "The active rendering backend does not match the selected profile.";
        return profileStatus;
    }
    for (size_t index = 0u; index < renderFeatureCount(); ++index) {
        const RenderFeature feature = static_cast<RenderFeature>(index);
        if (renderFeatureRequirement(profile, feature) == RenderFeatureRequirement::Unsupported) {
            continue;
        }
        const RenderFeatureStatus featureStatus =
            evaluateRenderFeature(profile, backend, capabilities, buildCapabilities, implementedFeatureMask, feature);
        if (!featureStatus.available()) {
            profileStatus.blockingFeature = feature;
            profileStatus.code = featureStatus.code;
            profileStatus.reason = featureStatus.reason;
            return profileStatus;
        }
    }
    profileStatus.blockingFeature = RenderFeature::Count;
    profileStatus.code = RenderFeatureStatusCode::Available;
    profileStatus.reason = "Available.";
    return profileStatus;
}

RenderProfileStatus evaluateCurrentRenderProfile(const RenderProfile profile, const RhiBackend backend,
                                                 const RhiCapabilities& capabilities) {
    return evaluateRenderProfile(profile, backend, capabilities, currentRenderBuildCapabilities(),
                                 currentImplementedRenderFeatureMask());
}

} // namespace renderer::contracts
