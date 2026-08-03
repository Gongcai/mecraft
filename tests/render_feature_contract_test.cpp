#include "renderer/contracts/RenderFeatureContract.h"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace {
bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

RhiCapabilities completeCapabilities() {
    RhiCapabilities capabilities;
    capabilities.maxColorAttachments = 8u;
    capabilities.textureView = true;
    capabilities.storageImage = true;
    capabilities.storageImageExtendedFormats = true;
    capabilities.samplerAnisotropy = true;
    capabilities.descriptorIndexing = true;
    capabilities.descriptorBindingPartiallyBound = true;
    capabilities.descriptorBindingVariableDescriptorCount = true;
    capabilities.descriptorBindingUpdateUnusedWhilePending = true;
    capabilities.descriptorBindingSampledImageUpdateAfterBind = true;
    capabilities.descriptorBindingStorageBufferUpdateAfterBind = true;
    capabilities.descriptorBindingAccelerationStructureUpdateAfterBind = true;
    capabilities.runtimeDescriptorArray = true;
    capabilities.shaderSampledImageArrayNonUniformIndexing = true;
    capabilities.shaderStorageBufferArrayNonUniformIndexing = true;
    capabilities.accelerationStructure = true;
    capabilities.rayQuery = true;
    capabilities.bufferDeviceAddress = true;
    capabilities.timestampQuery = true;
    capabilities.hdr10Swapchain = true;
    return capabilities;
}

constexpr uint64_t completeFeatureMask() {
    return (uint64_t{1u} << static_cast<uint8_t>(renderer::contracts::RenderFeature::Count)) - 1u;
}
} // namespace

int main() {
    using namespace renderer::contracts;

    if (!requireTrue(activeRenderProfile(RhiBackend::OpenGL) == RenderProfile::OpenGlBase,
                     "OpenGL must map to the base renderer profile") ||
        !requireTrue(activeRenderProfile(RhiBackend::Vulkan) == RenderProfile::VulkanModern,
                     "Vulkan must map to the modern renderer profile") ||
        !requireTrue(renderFeatureCount() == 15u,
                     "the fixed renderer feature table must contain every documented feature")) {
        return 1;
    }

    const RhiCapabilities capabilities = completeCapabilities();
    RenderBuildCapabilities completeBuild;
    completeBuild.nrd = true;

    RhiCapabilities sevenAttachmentCapabilities = capabilities;
    sevenAttachmentCapabilities.maxColorAttachments = 7u;
    const RenderFeatureStatus sevenAttachmentDeferredPbr =
        evaluateRenderFeature(RenderProfile::OpenGlBase, RhiBackend::OpenGL, sevenAttachmentCapabilities, completeBuild,
                              completeFeatureMask(), RenderFeature::DeferredPbr);
    const RenderFeatureStatus eightAttachmentDeferredPbr =
        evaluateRenderFeature(RenderProfile::OpenGlBase, RhiBackend::OpenGL, capabilities, completeBuild,
                              completeFeatureMask(), RenderFeature::DeferredPbr);
    if (!requireTrue(sevenAttachmentDeferredPbr.code == RenderFeatureStatusCode::DeviceCapabilityMissing,
                     "deferred PBR must reject devices limited to seven color attachments") ||
        !requireTrue(eightAttachmentDeferredPbr.available(),
                     "deferred PBR must accept exactly eight color attachments")) {
        return 1;
    }

    const RenderProfileStatus openGlBase =
        evaluateRenderProfile(RenderProfile::OpenGlBase, RhiBackend::OpenGL, capabilities, completeBuild,
                              currentImplementedRenderFeatureMask());
    if (!requireTrue(openGlBase.available(), "OpenGL base must be available when its required capabilities exist")) {
        return 1;
    }

    const RenderFeatureStatus openGlRtgi =
        evaluateRenderFeature(RenderProfile::OpenGlBase, RhiBackend::OpenGL, capabilities, completeBuild,
                              completeFeatureMask(), RenderFeature::RayTracedGlobalIllumination);
    if (!requireTrue(openGlRtgi.code == RenderFeatureStatusCode::BackendFeatureUnavailable,
                     "OpenGL must reject RTGI at the backend contract boundary") ||
        !requireTrue(std::string_view(renderFeatureStatusCodeStableId(openGlRtgi.code)) == "BackendFeatureUnavailable",
                     "backend rejection must expose the stable error code")) {
        return 1;
    }

    const RenderProfileStatus mismatchedProfile = evaluateRenderProfile(
        RenderProfile::VulkanModern, RhiBackend::OpenGL, capabilities, completeBuild, completeFeatureMask());
    if (!requireTrue(mismatchedProfile.code == RenderFeatureStatusCode::BackendFeatureUnavailable,
                     "a renderer profile must reject a mismatched active backend")) {
        return 1;
    }

    const RenderProfileStatus currentVulkan =
        evaluateRenderProfile(RenderProfile::VulkanModern, RhiBackend::Vulkan, capabilities, completeBuild,
                              currentImplementedRenderFeatureMask());
    if (!requireTrue(currentVulkan.code == RenderFeatureStatusCode::ImplementationPending,
                     "the current Vulkan modern profile must report unfinished integration") ||
        !requireTrue(currentVulkan.blockingFeature == RenderFeature::ReflectionProbeGrid,
                     "profile evaluation must return the first deterministic blocker")) {
        return 1;
    }

    RenderBuildCapabilities missingNrdBuild;
    const RenderFeatureStatus missingNrd =
        evaluateRenderFeature(RenderProfile::VulkanModern, RhiBackend::Vulkan, capabilities, missingNrdBuild,
                              completeFeatureMask(), RenderFeature::NrdDenoiser);
    if (!requireTrue(missingNrd.code == RenderFeatureStatusCode::BuildFeatureUnavailable,
                     "NRD must report a missing build component separately")) {
        return 1;
    }

    RhiCapabilities missingExtendedStorageFormats = capabilities;
    missingExtendedStorageFormats.storageImageExtendedFormats = false;
    const RenderFeatureStatus missingNrdStorageFormats =
        evaluateRenderFeature(RenderProfile::VulkanModern, RhiBackend::Vulkan, missingExtendedStorageFormats,
                              completeBuild, completeFeatureMask(), RenderFeature::NrdDenoiser);
    if (!requireTrue(missingNrdStorageFormats.code == RenderFeatureStatusCode::DeviceCapabilityMissing,
                     "NRD must require extended storage-image formats")) {
        return 1;
    }

    RhiCapabilities missingRayQuery = capabilities;
    missingRayQuery.rayQuery = false;
    const RenderFeatureStatus missingRtCapability =
        evaluateRenderFeature(RenderProfile::VulkanModern, RhiBackend::Vulkan, missingRayQuery, completeBuild,
                              completeFeatureMask(), RenderFeature::RayTracedGlobalIllumination);
    if (!requireTrue(missingRtCapability.code == RenderFeatureStatusCode::RayTracingCapabilityMissing,
                     "RTGI must report a missing device capability separately")) {
        return 1;
    }

    RhiCapabilities incompleteBindless = capabilities;
    incompleteBindless.runtimeDescriptorArray = false;
    const RenderFeatureStatus missingBindlessCapability =
        evaluateRenderFeature(RenderProfile::VulkanModern, RhiBackend::Vulkan, incompleteBindless, completeBuild,
                              completeFeatureMask(), RenderFeature::BindlessGpuScene);
    if (!requireTrue(missingBindlessCapability.code == RenderFeatureStatusCode::BindlessDescriptorCapabilityMissing,
                     "bindless GPU scene must validate descriptor indexing subfeatures")) {
        return 1;
    }

    RhiCapabilities missingAccelerationStructureDescriptorUpdate = capabilities;
    missingAccelerationStructureDescriptorUpdate.descriptorBindingAccelerationStructureUpdateAfterBind = false;
    const RenderFeatureStatus missingAccelerationStructureDescriptorCapability = evaluateRenderFeature(
        RenderProfile::VulkanModern, RhiBackend::Vulkan, missingAccelerationStructureDescriptorUpdate, completeBuild,
        completeFeatureMask(), RenderFeature::BindlessGpuScene);
    if (!requireTrue(missingAccelerationStructureDescriptorCapability.code ==
                         RenderFeatureStatusCode::BindlessDescriptorCapabilityMissing,
                     "bindless GPU scene must require update-after-bind acceleration-structure descriptors")) {
        return 1;
    }

    RhiCapabilities missingAccelerationStructure = capabilities;
    missingAccelerationStructure.accelerationStructure = false;
    const RenderFeatureStatus missingAccelerationStructureCapability =
        evaluateRenderFeature(RenderProfile::VulkanModern, RhiBackend::Vulkan, missingAccelerationStructure,
                              completeBuild, completeFeatureMask(), RenderFeature::BindlessGpuScene);
    if (!requireTrue(missingAccelerationStructureCapability.code ==
                         RenderFeatureStatusCode::BindlessDescriptorCapabilityMissing,
                     "bindless GPU scene must require the acceleration-structure resource capability")) {
        return 1;
    }

    RhiCapabilities missingTiming = capabilities;
    missingTiming.timestampQuery = false;
    const RenderFeatureStatus missingGpuTiming =
        evaluateRenderFeature(RenderProfile::VulkanModern, RhiBackend::Vulkan, missingTiming, completeBuild,
                              completeFeatureMask(), RenderFeature::GpuDynamicResolution);
    if (!requireTrue(missingGpuTiming.code == RenderFeatureStatusCode::GpuTimingUnavailable,
                     "dynamic resolution must expose the stable GPU timing error")) {
        return 1;
    }

    RhiCapabilities missingHdr = capabilities;
    missingHdr.hdr10Swapchain = false;
    const RenderFeatureStatus missingHdrDisplay =
        evaluateRenderFeature(RenderProfile::VulkanModern, RhiBackend::Vulkan, missingHdr, completeBuild,
                              completeFeatureMask(), RenderFeature::HdrSwapchain);
    if (!requireTrue(missingHdrDisplay.code == RenderFeatureStatusCode::HdrDisplayModeUnavailable,
                     "HDR must expose the stable display-mode error")) {
        return 1;
    }

    const RenderProfileStatus completeVulkan = evaluateRenderProfile(
        RenderProfile::VulkanModern, RhiBackend::Vulkan, capabilities, completeBuild, completeFeatureMask());
    if (!requireTrue(completeVulkan.available(), "Vulkan modern must be available when every contract is satisfied")) {
        return 1;
    }

    return 0;
}
