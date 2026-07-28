#include "renderer/contracts/RenderFeatureContract.h"

#include <array>
#include <cstdlib>

namespace renderer::contracts {
namespace {
struct FeatureDefinition {
    RenderFeature feature;
    const char* stableId;
    const char* displayNameZh;
    bool openGlBase;
    bool vulkanModern;
};

constexpr std::array<FeatureDefinition, renderFeatureCount()> kFeatureDefinitions{{
    {RenderFeature::DeferredPbr, "DeferredPbr", "延迟渲染 PBR", true, true},
    {RenderFeature::CascadedSunShadows, "CascadedSunShadows", "级联太阳阴影", true, true},
    {RenderFeature::Ssao, "Ssao", "屏幕空间环境光遮蔽", true, true},
    {RenderFeature::Ssgi, "Ssgi", "屏幕空间全局光照调试模式", true, true},
    {RenderFeature::Ssr, "Ssr", "屏幕空间反射调试模式", true, true},
    {RenderFeature::GltfMaterials, "GltfMaterials", "glTF 材质", true, true},
    {RenderFeature::ClusteredLighting, "ClusteredLighting", "集群局部灯光", false, true},
    {RenderFeature::PbrImageBasedLighting, "PbrImageBasedLighting", "GGX 图像光照", false, true},
    {RenderFeature::ReflectionProbeGrid, "ReflectionProbeGrid", "反射探针网格", false, true},
    {RenderFeature::RayTracedGlobalIllumination, "RayTracedGlobalIllumination", "硬件光追全局光照", false, true},
    {RenderFeature::NrdDenoiser, "NrdDenoiser", "NRD 时空降噪", false, true},
    {RenderFeature::MultiLayerTransparency, "MultiLayerTransparency", "多层透明折射", false, true},
    {RenderFeature::BindlessGpuScene, "BindlessGpuScene", "Bindless GPU 场景", false, true},
    {RenderFeature::GpuDynamicResolution, "GpuDynamicResolution", "GPU 时间动态分辨率", false, true},
    {RenderFeature::HdrSwapchain, "HdrSwapchain", "HDR 交换链", false, true},
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

const char* missingDeviceReasonZh(const RenderFeature feature) {
    switch (feature) {
        case RenderFeature::ClusteredLighting:
        case RenderFeature::MultiLayerTransparency:
            return "当前设备或驱动缺少存储图像能力";
        case RenderFeature::PbrImageBasedLighting:
        case RenderFeature::ReflectionProbeGrid:
            return "当前设备或驱动缺少纹理视图或各向异性采样能力";
        case RenderFeature::RayTracedGlobalIllumination:
            return "当前设备或驱动缺少加速结构、Ray Query 或 Buffer Device Address";
        case RenderFeature::NrdDenoiser:
            return "当前设备或驱动缺少 NRD 所需的计算存储图像能力";
        case RenderFeature::BindlessGpuScene:
            return "当前设备或驱动缺少运行时描述符数组、部分绑定或非一致索引能力";
        case RenderFeature::GpuDynamicResolution:
            return "当前设备或驱动缺少 GPU 时间戳查询能力";
        case RenderFeature::HdrSwapchain:
            return "当前显示表面不支持 HDR10 或 scRGB 交换链";
        case RenderFeature::DeferredPbr:
        case RenderFeature::CascadedSunShadows:
        case RenderFeature::Ssao:
        case RenderFeature::Ssgi:
        case RenderFeature::Ssr:
        case RenderFeature::GltfMaterials:
            return "当前设备或驱动缺少该功能所需的图形能力";
        case RenderFeature::Count:
            std::abort();
    }
    std::abort();
}

RenderFeatureStatusCode missingDeviceStatusCode(const RenderFeature feature) {
    switch (feature) {
        case RenderFeature::RayTracedGlobalIllumination:
            return RenderFeatureStatusCode::RayTracingCapabilityMissing;
        case RenderFeature::BindlessGpuScene:
            return RenderFeatureStatusCode::BindlessDescriptorCapabilityMissing;
        case RenderFeature::GpuDynamicResolution:
            return RenderFeatureStatusCode::GpuTimingUnavailable;
        case RenderFeature::HdrSwapchain:
            return RenderFeatureStatusCode::HdrDisplayModeUnavailable;
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
        case RenderFeature::MultiLayerTransparency:
            return RenderFeatureStatusCode::DeviceCapabilityMissing;
        case RenderFeature::Count:
            std::abort();
    }
    std::abort();
}

bool deviceSupportsFeature(const RhiCapabilities& capabilities, const RenderFeature feature) {
    switch (feature) {
        case RenderFeature::DeferredPbr:
            return capabilities.maxColorAttachments >= 8u;
        case RenderFeature::CascadedSunShadows:
            return capabilities.textureView;
        case RenderFeature::Ssao:
        case RenderFeature::Ssgi:
        case RenderFeature::Ssr:
            return capabilities.storageImage && capabilities.textureView;
        case RenderFeature::GltfMaterials:
            return capabilities.samplerAnisotropy;
        case RenderFeature::ClusteredLighting:
        case RenderFeature::MultiLayerTransparency:
            return capabilities.storageImage;
        case RenderFeature::PbrImageBasedLighting:
        case RenderFeature::ReflectionProbeGrid:
            return capabilities.textureView && capabilities.samplerAnisotropy;
        case RenderFeature::RayTracedGlobalIllumination:
            return capabilities.accelerationStructure && capabilities.rayQuery &&
                   capabilities.bufferDeviceAddress;
        case RenderFeature::NrdDenoiser:
            return capabilities.storageImage;
        case RenderFeature::BindlessGpuScene:
            return capabilities.descriptorIndexing &&
                   capabilities.descriptorBindingPartiallyBound &&
                   capabilities.descriptorBindingVariableDescriptorCount &&
                   capabilities.runtimeDescriptorArray &&
                   capabilities.shaderSampledImageArrayNonUniformIndexing &&
                   capabilities.shaderStorageBufferArrayNonUniformIndexing;
        case RenderFeature::GpuDynamicResolution:
            return capabilities.timestampQuery;
        case RenderFeature::HdrSwapchain:
            return capabilities.hdr10Swapchain || capabilities.scRgbSwapchain;
        case RenderFeature::Count:
            std::abort();
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

const char* renderProfileDisplayNameZh(const RenderProfile profile) {
    switch (profile) {
        case RenderProfile::OpenGlBase: return "OpenGL 基础管线";
        case RenderProfile::VulkanModern: return "Vulkan 现代管线";
    }
    std::abort();
}

const char* renderFeatureStableId(const RenderFeature feature) {
    return definition(feature).stableId;
}

const char* renderFeatureDisplayNameZh(const RenderFeature feature) {
    return definition(feature).displayNameZh;
}

const char* renderFeatureStatusCodeStableId(const RenderFeatureStatusCode code) {
    switch (code) {
        case RenderFeatureStatusCode::Available: return "Available";
        case RenderFeatureStatusCode::BackendFeatureUnavailable:
            return "BackendFeatureUnavailable";
        case RenderFeatureStatusCode::BuildFeatureUnavailable:
            return "BuildFeatureUnavailable";
        case RenderFeatureStatusCode::DeviceCapabilityMissing:
            return "DeviceCapabilityMissing";
        case RenderFeatureStatusCode::RayTracingCapabilityMissing:
            return "RayTracingCapabilityMissing";
        case RenderFeatureStatusCode::BindlessDescriptorCapabilityMissing:
            return "BindlessDescriptorCapabilityMissing";
        case RenderFeatureStatusCode::GpuTimingUnavailable:
            return "GpuTimingUnavailable";
        case RenderFeatureStatusCode::HdrDisplayModeUnavailable:
            return "HdrDisplayModeUnavailable";
        case RenderFeatureStatusCode::ImplementationPending:
            return "ImplementationPending";
    }
    std::abort();
}

RenderFeatureRequirement renderFeatureRequirement(
    const RenderProfile profile,
    const RenderFeature feature) {
    const FeatureDefinition& featureDefinition = definition(feature);
    switch (profile) {
        case RenderProfile::OpenGlBase:
            return featureDefinition.openGlBase
                ? RenderFeatureRequirement::Required
                : RenderFeatureRequirement::Unsupported;
        case RenderProfile::VulkanModern:
            return featureDefinition.vulkanModern
                ? RenderFeatureRequirement::Required
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
    return featureBit(RenderFeature::DeferredPbr) |
           featureBit(RenderFeature::CascadedSunShadows) |
           featureBit(RenderFeature::Ssao) |
           featureBit(RenderFeature::Ssgi) |
           featureBit(RenderFeature::Ssr) |
           featureBit(RenderFeature::GltfMaterials) |
           featureBit(RenderFeature::ClusteredLighting);
}

RenderFeatureStatus evaluateRenderFeature(
    const RenderProfile profile,
    const RhiBackend backend,
    const RhiCapabilities& capabilities,
    const RenderBuildCapabilities& buildCapabilities,
    const uint64_t implementedFeatureMask,
    const RenderFeature feature) {
    RenderFeatureStatus status;
    status.feature = feature;
    if (!profileMatchesBackend(profile, backend) ||
        renderFeatureRequirement(profile, feature) == RenderFeatureRequirement::Unsupported) {
        status.code = RenderFeatureStatusCode::BackendFeatureUnavailable;
        status.reasonZh = "当前渲染后端不支持该功能";
        return status;
    }
    if (feature == RenderFeature::NrdDenoiser && !buildCapabilities.nrd) {
        status.code = RenderFeatureStatusCode::BuildFeatureUnavailable;
        status.reasonZh = "当前构建未启用 NRD 组件";
        return status;
    }
    if ((implementedFeatureMask & featureBit(feature)) == 0u) {
        status.code = RenderFeatureStatusCode::ImplementationPending;
        status.reasonZh = "该功能尚未完成产品集成";
        return status;
    }
    if (!deviceSupportsFeature(capabilities, feature)) {
        status.code = missingDeviceStatusCode(feature);
        status.reasonZh = missingDeviceReasonZh(feature);
        return status;
    }
    status.code = RenderFeatureStatusCode::Available;
    status.reasonZh = "可用";
    return status;
}

RenderFeatureStatus evaluateCurrentRenderFeature(
    const RenderProfile profile,
    const RhiBackend backend,
    const RhiCapabilities& capabilities,
    const RenderFeature feature) {
    return evaluateRenderFeature(
        profile,
        backend,
        capabilities,
        currentRenderBuildCapabilities(),
        currentImplementedRenderFeatureMask(),
        feature);
}

RenderProfileStatus evaluateRenderProfile(
    const RenderProfile profile,
    const RhiBackend backend,
    const RhiCapabilities& capabilities,
    const RenderBuildCapabilities& buildCapabilities,
    const uint64_t implementedFeatureMask) {
    RenderProfileStatus profileStatus;
    profileStatus.profile = profile;
    if (!profileMatchesBackend(profile, backend)) {
        profileStatus.blockingFeature = RenderFeature::Count;
        profileStatus.code = RenderFeatureStatusCode::BackendFeatureUnavailable;
        profileStatus.reasonZh = "当前渲染后端与所选管线不匹配";
        return profileStatus;
    }
    for (size_t index = 0u; index < renderFeatureCount(); ++index) {
        const RenderFeature feature = static_cast<RenderFeature>(index);
        if (renderFeatureRequirement(profile, feature) == RenderFeatureRequirement::Unsupported) {
            continue;
        }
        const RenderFeatureStatus featureStatus = evaluateRenderFeature(
            profile,
            backend,
            capabilities,
            buildCapabilities,
            implementedFeatureMask,
            feature);
        if (!featureStatus.available()) {
            profileStatus.blockingFeature = feature;
            profileStatus.code = featureStatus.code;
            profileStatus.reasonZh = featureStatus.reasonZh;
            return profileStatus;
        }
    }
    profileStatus.blockingFeature = RenderFeature::Count;
    profileStatus.code = RenderFeatureStatusCode::Available;
    profileStatus.reasonZh = "可用";
    return profileStatus;
}

RenderProfileStatus evaluateCurrentRenderProfile(
    const RenderProfile profile,
    const RhiBackend backend,
    const RhiCapabilities& capabilities) {
    return evaluateRenderProfile(
        profile,
        backend,
        capabilities,
        currentRenderBuildCapabilities(),
        currentImplementedRenderFeatureMask());
}

} // namespace renderer::contracts
