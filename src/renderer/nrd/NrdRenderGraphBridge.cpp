#include "renderer/nrd/NrdRenderGraphBridge.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace renderer::nrd {
namespace {
constexpr ::nrd::Identifier kDiffuseDenoiserIdentifier = 1u;
constexpr uint8_t kNrdVersionMajor = 4u;
constexpr uint8_t kNrdVersionMinor = 17u;
constexpr uint8_t kNrdVersionBuild = 3u;

[[nodiscard]] bool sameHandle(const RgTextureHandle lhs, const RgTextureHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] bool sameHandle(const RhiTextureViewHandle lhs, const RhiTextureViewHandle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] uint16_t divideUp(const uint16_t value, const uint16_t divisor) {
    return static_cast<uint16_t>((static_cast<uint32_t>(value) + divisor - 1u) / divisor);
}

[[nodiscard]] bool externalResourceType(const ::nrd::ResourceType type) {
    return type != ::nrd::ResourceType::PERMANENT_POOL && type != ::nrd::ResourceType::TRANSIENT_POOL &&
           static_cast<size_t>(type) < kNrdResourceTypeCount;
}

[[nodiscard]] bool commonSettingsMatchExtent(const ::nrd::CommonSettings& settings, const uint16_t resourceWidth,
                                             const uint16_t resourceHeight) {
    const bool fixedResourceSize =
        settings.resourceSize[0] == resourceWidth && settings.resourceSize[1] == resourceHeight &&
        settings.resourceSizePrev[0] == resourceWidth && settings.resourceSizePrev[1] == resourceHeight;
    if (!fixedResourceSize || settings.rectSize[0] == 0u || settings.rectSize[1] == 0u ||
        settings.rectSizePrev[0] == 0u || settings.rectSizePrev[1] == 0u || settings.rectSize[0] > resourceWidth ||
        settings.rectSize[1] > resourceHeight || settings.rectSizePrev[0] > resourceWidth ||
        settings.rectSizePrev[1] > resourceHeight) {
        return false;
    }
    const uint64_t rectEndX = static_cast<uint64_t>(settings.rectOrigin[0]) + settings.rectSize[0];
    const uint64_t rectEndY = static_cast<uint64_t>(settings.rectOrigin[1]) + settings.rectSize[1];
    return rectEndX <= resourceWidth && rectEndY <= resourceHeight;
}
} // namespace

bool NrdExternalResources::bind(const ::nrd::ResourceType type, const RgTextureHandle textureHandle) {
    if (!externalResourceType(type) || !nrdDiffuseExternalTextureFormat(type).has_value() || !textureHandle.isValid()) {
        return false;
    }
    textures[static_cast<size_t>(type)] = textureHandle;
    return true;
}

RgTextureHandle NrdExternalResources::texture(const ::nrd::ResourceType type) const {
    if (!externalResourceType(type) || !nrdDiffuseExternalTextureFormat(type).has_value()) {
        return {};
    }
    return textures[static_cast<size_t>(type)];
}

std::optional<RhiTextureFormat> nrdTextureFormatToRhi(const ::nrd::Format format) {
    switch (format) {
    case ::nrd::Format::R8_UNORM: return RhiTextureFormat::R8Unorm;
    case ::nrd::Format::R8_UINT: return RhiTextureFormat::R8Uint;
    case ::nrd::Format::RGBA8_UNORM: return RhiTextureFormat::Rgba8Unorm;
    case ::nrd::Format::R16_UINT: return RhiTextureFormat::R16Uint;
    case ::nrd::Format::R16_SFLOAT: return RhiTextureFormat::R16Float;
    case ::nrd::Format::RGBA16_SFLOAT: return RhiTextureFormat::Rgba16Float;
    case ::nrd::Format::R32_SFLOAT: return RhiTextureFormat::R32Float;
    case ::nrd::Format::R10_G10_B10_A2_UNORM: return RhiTextureFormat::Rgb10A2Unorm;
    default: return std::nullopt;
    }
}

std::optional<RhiTextureFormat> nrdDiffuseExternalTextureFormat(const ::nrd::ResourceType type) {
    switch (type) {
    case ::nrd::ResourceType::IN_MV: return RhiTextureFormat::Rg16Float;
    case ::nrd::ResourceType::IN_NORMAL_ROUGHNESS: return RhiTextureFormat::Rgb10A2Unorm;
    case ::nrd::ResourceType::IN_VIEWZ: return RhiTextureFormat::R32Float;
    case ::nrd::ResourceType::IN_DIFF_CONFIDENCE:
    case ::nrd::ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX: return RhiTextureFormat::R8Unorm;
    case ::nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
    case ::nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST: return RhiTextureFormat::Rgba16Float;
    case ::nrd::ResourceType::OUT_VALIDATION: return RhiTextureFormat::Rgba8Unorm;
    default: return std::nullopt;
    }
}

std::optional<::nrd::Denoiser> nrdDiffuseDenoiser(const NrdDiffuseMethod method) {
    switch (method) {
    case NrdDiffuseMethod::Relax: return ::nrd::Denoiser::RELAX_DIFFUSE;
    case NrdDiffuseMethod::Reblur: return ::nrd::Denoiser::REBLUR_DIFFUSE;
    }
    return std::nullopt;
}

std::optional<std::string_view> nrdBridgeErrorStableId(const NrdBridgeError error) {
    switch (error) {
    case NrdBridgeError::None: return "None";
    case NrdBridgeError::AlreadyInitialized: return "AlreadyInitialized";
    case NrdBridgeError::InvalidDevice: return "InvalidDevice";
    case NrdBridgeError::UnsupportedDeviceCapabilities: return "UnsupportedDeviceCapabilities";
    case NrdBridgeError::InvalidExtent: return "InvalidExtent";
    case NrdBridgeError::InvalidMethod: return "InvalidMethod";
    case NrdBridgeError::UnsupportedLibraryContract: return "UnsupportedLibraryContract";
    case NrdBridgeError::InstanceCreationFailed: return "InstanceCreationFailed";
    case NrdBridgeError::UnsupportedInstanceContract: return "UnsupportedInstanceContract";
    case NrdBridgeError::UnsupportedTextureFormat: return "UnsupportedTextureFormat";
    case NrdBridgeError::ResourceCreationFailed: return "ResourceCreationFailed";
    case NrdBridgeError::PipelineCreationFailed: return "PipelineCreationFailed";
    case NrdBridgeError::NotInitialized: return "NotInitialized";
    case NrdBridgeError::NoFramePending: return "NoFramePending";
    case NrdBridgeError::FrameAlreadyPending: return "FrameAlreadyPending";
    case NrdBridgeError::ExecutionStateInvalid: return "ExecutionStateInvalid";
    case NrdBridgeError::MethodSettingsMismatch: return "MethodSettingsMismatch";
    case NrdBridgeError::InvalidCommonSettings: return "InvalidCommonSettings";
    case NrdBridgeError::DenoiserSettingsRejected: return "DenoiserSettingsRejected";
    case NrdBridgeError::CommonSettingsRejected: return "CommonSettingsRejected";
    case NrdBridgeError::DispatchQueryFailed: return "DispatchQueryFailed";
    case NrdBridgeError::MissingExternalResource: return "MissingExternalResource";
    case NrdBridgeError::InvalidDispatchContract: return "InvalidDispatchContract";
    case NrdBridgeError::GraphResourceCreationFailed: return "GraphResourceCreationFailed";
    case NrdBridgeError::GraphPassCreationFailed: return "GraphPassCreationFailed";
    }
    return std::nullopt;
}

struct NrdRenderGraphBridge::Impl final {
    struct PersistentTexture final {
        std::string name;
        RhiTextureDesc desc;
        RhiTextureHandle texture;
        RhiTextureViewHandle view;
    };

    struct PipelineBinding final {
        uint32_t binding = 0u;
        ::nrd::DescriptorType descriptorType = ::nrd::DescriptorType::TEXTURE;
    };

    struct PipelineRuntime final {
        std::string name;
        std::vector<PipelineBinding> bindings;
        RhiShaderHandle shader;
        RhiBindGroupLayoutHandle resourceLayout;
        RhiPipelineLayoutHandle pipelineLayout;
        RhiPipelineHandle pipeline;
    };

    struct BoundResource final {
        RgTextureHandle texture;
        RhiTextureFormat format = RhiTextureFormat::Undefined;
        ::nrd::ResourceType resourceType = ::nrd::ResourceType::IN_MV;
        ::nrd::DescriptorType descriptorType = ::nrd::DescriptorType::TEXTURE;
        uint32_t binding = 0u;
        uint16_t width = 0u;
        uint16_t height = 0u;
    };

    struct DispatchRuntime final {
        std::string name;
        std::vector<BoundResource> resources;
        std::vector<uint8_t> constantData;
        uint32_t constantDataSize = 0u;
        uint16_t pipelineIndex = 0u;
        uint16_t gridWidth = 0u;
        uint16_t gridHeight = 0u;
        bool constantDataMatchesPreviousDispatch = false;
    };

    struct DispatchBindingCache final {
        RhiBindGroupHandle bindGroup;
        std::vector<RhiTextureViewHandle> views;
        uint16_t pipelineIndex = std::numeric_limits<uint16_t>::max();
    };

    [[nodiscard]] NrdBridgeError initialize(RhiDevice& targetDevice, const NrdDiffuseMethod targetMethod,
                                            const uint16_t targetWidth, const uint16_t targetHeight) {
        device = &targetDevice;
        method = targetMethod;
        width = targetWidth;
        height = targetHeight;

        if (!device->capabilities().storageImageExtendedFormats) {
            return NrdBridgeError::UnsupportedDeviceCapabilities;
        }

        const ::nrd::LibraryDesc* library = ::nrd::GetLibraryDesc();
        if (library == nullptr || library->versionMajor != kNrdVersionMajor ||
            library->versionMinor != kNrdVersionMinor || library->versionBuild != kNrdVersionBuild ||
            library->normalEncoding != ::nrd::NormalEncoding::R10_G10_B10_A2_UNORM ||
            library->roughnessEncoding != ::nrd::RoughnessEncoding::LINEAR ||
            library->spirvBindingOffsets.samplerOffset != 0u ||
            library->spirvBindingOffsets.constantBufferOffset != 2u ||
            library->spirvBindingOffsets.storageTextureAndBufferOffset != 3u ||
            library->spirvBindingOffsets.textureOffset != 20u) {
            return NrdBridgeError::UnsupportedLibraryContract;
        }
        libraryDesc = library;

        const std::optional<::nrd::Denoiser> denoiser = nrdDiffuseDenoiser(method);
        if (!denoiser.has_value()) {
            return NrdBridgeError::InvalidMethod;
        }
        const ::nrd::DenoiserDesc denoiserDesc{kDiffuseDenoiserIdentifier, *denoiser};
        ::nrd::InstanceCreationDesc creationDesc{};
        creationDesc.denoisers = &denoiserDesc;
        creationDesc.denoisersNum = 1u;
        if (::nrd::CreateInstance(creationDesc, instance) != ::nrd::Result::SUCCESS || instance == nullptr) {
            return NrdBridgeError::InstanceCreationFailed;
        }
        instanceDesc = ::nrd::GetInstanceDesc(*instance);
        if (instanceDesc == nullptr || instanceDesc->resourcesSpaceIndex != 0u ||
            instanceDesc->constantBufferAndSamplersSpaceIndex != 1u ||
            instanceDesc->constantBufferRegisterIndex != 0u || instanceDesc->samplersBaseRegisterIndex != 0u ||
            instanceDesc->resourcesBaseRegisterIndex != 0u || instanceDesc->samplersNum != 2u ||
            instanceDesc->samplers == nullptr || instanceDesc->samplers[0] != ::nrd::Sampler::NEAREST_CLAMP ||
            instanceDesc->samplers[1] != ::nrd::Sampler::LINEAR_CLAMP || instanceDesc->shaderEntryPoint == nullptr ||
            std::strcmp(instanceDesc->shaderEntryPoint, "main") != 0 || instanceDesc->pipelines == nullptr ||
            instanceDesc->pipelinesNum == 0u || instanceDesc->permanentPool == nullptr ||
            instanceDesc->permanentPoolSize == 0u || instanceDesc->transientPool == nullptr ||
            instanceDesc->transientPoolSize == 0u || instanceDesc->constantBufferMaxDataSize == 0u) {
            return NrdBridgeError::UnsupportedInstanceContract;
        }

        for (uint32_t i = 0u; i < instanceDesc->permanentPoolSize; ++i) {
            if (instanceDesc->permanentPool[i].downsampleFactor == 0u ||
                !nrdTextureFormatToRhi(instanceDesc->permanentPool[i].format).has_value()) {
                return NrdBridgeError::UnsupportedTextureFormat;
            }
        }
        for (uint32_t i = 0u; i < instanceDesc->transientPoolSize; ++i) {
            if (instanceDesc->transientPool[i].downsampleFactor == 0u ||
                !nrdTextureFormatToRhi(instanceDesc->transientPool[i].format).has_value()) {
                return NrdBridgeError::UnsupportedTextureFormat;
            }
        }

        permanentTextures.reserve(instanceDesc->permanentPoolSize);
        for (uint32_t i = 0u; i < instanceDesc->permanentPoolSize; ++i) {
            const ::nrd::TextureDesc& nrdTexture = instanceDesc->permanentPool[i];
            permanentTextures.emplace_back();
            PersistentTexture& persistent = permanentTextures.back();
            persistent.name = "NRD.Permanent." + std::to_string(i);
            persistent.desc.debugName = persistent.name.c_str();
            persistent.desc.dimension = RhiTextureDimension::Texture2D;
            persistent.desc.format = *nrdTextureFormatToRhi(nrdTexture.format);
            persistent.desc.width = divideUp(width, nrdTexture.downsampleFactor);
            persistent.desc.height = divideUp(height, nrdTexture.downsampleFactor);
            persistent.desc.depthOrLayers = 1u;
            persistent.desc.mipLevels = 1u;
            persistent.desc.sampleCount = 1u;
            persistent.desc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::Storage);
            persistent.desc.memoryCategory = RhiMemoryCategory::Nrd;
            persistent.texture = device->createTexture(persistent.desc, nullptr);
            if (!persistent.texture.isValid()) {
                return NrdBridgeError::ResourceCreationFailed;
            }
            RhiTextureViewDesc viewDesc;
            viewDesc.texture = persistent.texture;
            viewDesc.viewType = RhiTextureViewType::Texture2D;
            viewDesc.format = persistent.desc.format;
            persistent.view = device->createTextureView(viewDesc);
            if (!persistent.view.isValid()) {
                return NrdBridgeError::ResourceCreationFailed;
            }
        }

        RhiBufferDesc constantBufferDesc;
        constantBufferDesc.debugName = "NRD.Constants";
        constantBufferDesc.size = instanceDesc->constantBufferMaxDataSize;
        constantBufferDesc.usage = rhiFlag(RhiBufferUsage::Uniform) | rhiFlag(RhiBufferUsage::TransferDst);
        constantBufferDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        constantBufferDesc.initialState = RhiResourceState::UniformBuffer;
        constantBufferDesc.memoryCategory = RhiMemoryCategory::Nrd;
        constantBuffer = device->createBuffer(constantBufferDesc, nullptr, 0u);
        if (!constantBuffer.isValid()) {
            return NrdBridgeError::ResourceCreationFailed;
        }

        for (uint32_t i = 0u; i < samplers.size(); ++i) {
            RhiSamplerDesc samplerDesc;
            samplerDesc.minFilter = i == 0u ? RhiFilter::Nearest : RhiFilter::Linear;
            samplerDesc.magFilter = i == 0u ? RhiFilter::Nearest : RhiFilter::Linear;
            samplerDesc.mipmapMode = i == 0u ? RhiMipmapMode::Nearest : RhiMipmapMode::Linear;
            samplerDesc.addressU = RhiAddressMode::ClampToEdge;
            samplerDesc.addressV = RhiAddressMode::ClampToEdge;
            samplerDesc.addressW = RhiAddressMode::ClampToEdge;
            samplers[i] = device->createSampler(samplerDesc);
            if (!samplers[i].isValid()) {
                return NrdBridgeError::ResourceCreationFailed;
            }
        }

        RhiBindGroupLayoutDesc commonLayoutDesc;
        commonLayoutDesc.debugName = "NRD.CommonLayout";
        for (uint32_t i = 0u; i < samplers.size(); ++i) {
            commonLayoutDesc.entries.push_back(
                {libraryDesc->spirvBindingOffsets.samplerOffset + instanceDesc->samplersBaseRegisterIndex + i,
                 RhiBindingType::Sampler, rhiFlag(RhiShaderStage::Compute), 1u});
        }
        commonLayoutDesc.entries.push_back(
            {libraryDesc->spirvBindingOffsets.constantBufferOffset + instanceDesc->constantBufferRegisterIndex,
             RhiBindingType::UniformBuffer, rhiFlag(RhiShaderStage::Compute), 1u});
        commonLayout = device->createBindGroupLayout(commonLayoutDesc);
        if (!commonLayout.isValid()) {
            return NrdBridgeError::ResourceCreationFailed;
        }

        RhiBindGroupDesc commonBindGroupDesc;
        commonBindGroupDesc.layout = commonLayout;
        for (uint32_t i = 0u; i < samplers.size(); ++i) {
            RhiBindGroupEntry entry;
            entry.binding =
                libraryDesc->spirvBindingOffsets.samplerOffset + instanceDesc->samplersBaseRegisterIndex + i;
            entry.resource.sampler = samplers[i];
            commonBindGroupDesc.entries.push_back(entry);
        }
        RhiBindGroupEntry constantEntry;
        constantEntry.binding =
            libraryDesc->spirvBindingOffsets.constantBufferOffset + instanceDesc->constantBufferRegisterIndex;
        constantEntry.resource.buffer = {constantBuffer, 0u, instanceDesc->constantBufferMaxDataSize};
        commonBindGroupDesc.entries.push_back(constantEntry);
        commonBindGroup = device->createBindGroup(commonBindGroupDesc);
        if (!commonBindGroup.isValid()) {
            return NrdBridgeError::ResourceCreationFailed;
        }

        pipelines.reserve(instanceDesc->pipelinesNum);
        for (uint32_t pipelineIndex = 0u; pipelineIndex < instanceDesc->pipelinesNum; ++pipelineIndex) {
            const ::nrd::PipelineDesc& nrdPipeline = instanceDesc->pipelines[pipelineIndex];
            if (nrdPipeline.computeShaderSPIRV.bytecode == nullptr || nrdPipeline.computeShaderSPIRV.size == 0u ||
                nrdPipeline.resourceRanges == nullptr || nrdPipeline.resourceRangesNum == 0u ||
                nrdPipeline.resourceRangesNum > 2u) {
                return NrdBridgeError::UnsupportedInstanceContract;
            }

            pipelines.emplace_back();
            PipelineRuntime& pipeline = pipelines.back();
            pipeline.name = "NRD.Pipeline." + std::to_string(pipelineIndex);
            RhiBindGroupLayoutDesc resourceLayoutDesc;
            resourceLayoutDesc.debugName = pipeline.name.c_str();
            for (uint32_t rangeIndex = 0u; rangeIndex < nrdPipeline.resourceRangesNum; ++rangeIndex) {
                const ::nrd::ResourceRangeDesc& range = nrdPipeline.resourceRanges[rangeIndex];
                if (range.descriptorsNum == 0u || (range.descriptorType != ::nrd::DescriptorType::TEXTURE &&
                                                   range.descriptorType != ::nrd::DescriptorType::STORAGE_TEXTURE)) {
                    return NrdBridgeError::UnsupportedInstanceContract;
                }
                const uint32_t baseBinding = (range.descriptorType == ::nrd::DescriptorType::TEXTURE
                                                  ? libraryDesc->spirvBindingOffsets.textureOffset
                                                  : libraryDesc->spirvBindingOffsets.storageTextureAndBufferOffset) +
                                             instanceDesc->resourcesBaseRegisterIndex;
                for (uint32_t descriptorIndex = 0u; descriptorIndex < range.descriptorsNum; ++descriptorIndex) {
                    const uint32_t binding = baseBinding + descriptorIndex;
                    const auto duplicate = std::find_if(
                        pipeline.bindings.begin(), pipeline.bindings.end(),
                        [binding](const PipelineBinding& existing) { return existing.binding == binding; });
                    if (duplicate != pipeline.bindings.end()) {
                        return NrdBridgeError::UnsupportedInstanceContract;
                    }
                    pipeline.bindings.push_back({binding, range.descriptorType});
                    resourceLayoutDesc.entries.push_back({binding,
                                                          range.descriptorType == ::nrd::DescriptorType::TEXTURE
                                                              ? RhiBindingType::SampledTexture
                                                              : RhiBindingType::StorageTexture,
                                                          rhiFlag(RhiShaderStage::Compute), 1u});
                }
            }
            pipeline.resourceLayout = device->createBindGroupLayout(resourceLayoutDesc);
            if (!pipeline.resourceLayout.isValid()) {
                return NrdBridgeError::PipelineCreationFailed;
            }

            RhiPipelineLayoutDesc pipelineLayoutDesc;
            pipelineLayoutDesc.debugName = pipeline.name.c_str();
            pipelineLayoutDesc.bindGroupLayouts = {pipeline.resourceLayout, commonLayout};
            pipeline.pipelineLayout = device->createPipelineLayout(pipelineLayoutDesc);
            if (!pipeline.pipelineLayout.isValid()) {
                return NrdBridgeError::PipelineCreationFailed;
            }

            RhiShaderDesc shaderDesc;
            shaderDesc.debugName = pipeline.name.c_str();
            shaderDesc.stage = RhiShaderStage::Compute;
            shaderDesc.bytecode = nrdPipeline.computeShaderSPIRV.bytecode;
            shaderDesc.bytecodeSize = nrdPipeline.computeShaderSPIRV.size;
            shaderDesc.entryPoint = instanceDesc->shaderEntryPoint;
            pipeline.shader = device->createShader(shaderDesc);
            if (!pipeline.shader.isValid()) {
                return NrdBridgeError::PipelineCreationFailed;
            }

            RhiComputePipelineDesc computePipelineDesc;
            computePipelineDesc.debugName = pipeline.name.c_str();
            computePipelineDesc.computeShader = pipeline.shader;
            computePipelineDesc.layout = pipeline.pipelineLayout;
            pipeline.pipeline = device->createComputePipeline(computePipelineDesc);
            if (!pipeline.pipeline.isValid()) {
                return NrdBridgeError::PipelineCreationFailed;
            }
        }
        return NrdBridgeError::None;
    }

    void shutdown() {
        if (device != nullptr) {
            for (DispatchBindingCache& cache : dispatchBindings) {
                if (cache.bindGroup.isValid()) {
                    device->destroyBindGroup(cache.bindGroup);
                }
            }
            if (commonBindGroup.isValid()) {
                device->destroyBindGroup(commonBindGroup);
            }
            for (PipelineRuntime& pipeline : pipelines) {
                if (pipeline.pipeline.isValid()) {
                    device->destroyPipeline(pipeline.pipeline);
                }
                if (pipeline.pipelineLayout.isValid()) {
                    device->destroyPipelineLayout(pipeline.pipelineLayout);
                }
                if (pipeline.resourceLayout.isValid()) {
                    device->destroyBindGroupLayout(pipeline.resourceLayout);
                }
                if (pipeline.shader.isValid()) {
                    device->destroyShader(pipeline.shader);
                }
            }
            if (commonLayout.isValid()) {
                device->destroyBindGroupLayout(commonLayout);
            }
            for (const RhiSamplerHandle sampler : samplers) {
                if (sampler.isValid()) {
                    device->destroySampler(sampler);
                }
            }
            if (constantBuffer.isValid()) {
                device->destroyBuffer(constantBuffer);
            }
            for (PersistentTexture& persistent : permanentTextures) {
                if (persistent.view.isValid()) {
                    device->destroyTextureView(persistent.view);
                }
                if (persistent.texture.isValid()) {
                    device->destroyTexture(persistent.texture);
                }
            }
        }
        if (instance != nullptr) {
            ::nrd::DestroyInstance(*instance);
        }
        device = nullptr;
        instance = nullptr;
        libraryDesc = nullptr;
        instanceDesc = nullptr;
        permanentTextures.clear();
        pipelines.clear();
        dispatchBindings.clear();
        constantBuffer = {};
        samplers = {};
        commonLayout = {};
        commonBindGroup = {};
        permanentPoolInitialized = false;
        framePending = false;
        executionStateValid = true;
    }

    [[nodiscard]] bool textureViewMatches(const RhiTextureViewHandle view, const BoundResource& resource) const {
        RhiTextureViewDesc viewDesc;
        RhiTextureDesc textureDesc;
        if (!view.isValid() || !device->getTextureViewDesc(view, viewDesc) ||
            !device->getTextureDesc(viewDesc.texture, textureDesc)) {
            return false;
        }
        const RhiTextureFormat viewFormat =
            viewDesc.format == RhiTextureFormat::Undefined ? textureDesc.format : viewDesc.format;
        const RhiTextureUsage requiredUsage = resource.descriptorType == ::nrd::DescriptorType::TEXTURE
                                                  ? RhiTextureUsage::Sampled
                                                  : RhiTextureUsage::Storage;
        return viewDesc.viewType == RhiTextureViewType::Texture2D && viewDesc.baseMip == 0u &&
               viewDesc.mipCount == 1u && viewDesc.baseLayer == 0u && viewDesc.layerCount == 1u &&
               textureDesc.dimension == RhiTextureDimension::Texture2D && viewFormat == resource.format &&
               textureDesc.format == resource.format && textureDesc.mipLevels == 1u &&
               textureDesc.depthOrLayers == 1u && textureDesc.sampleCount == 1u &&
               textureDesc.width == resource.width && textureDesc.height == resource.height &&
               (textureDesc.usage & rhiFlag(requiredUsage)) != 0u;
    }

    [[nodiscard]] bool recordDispatch(RgPassContext& pass, const uint32_t dispatchIndex,
                                      const DispatchRuntime& dispatch) {
        if (dispatch.pipelineIndex >= pipelines.size() || dispatchIndex >= dispatchBindings.size() ||
            dispatch.gridWidth == 0u || dispatch.gridHeight == 0u) {
            return false;
        }
        const PipelineRuntime& pipeline = pipelines[dispatch.pipelineIndex];
        if (pipeline.bindings.size() != dispatch.resources.size()) {
            return false;
        }

        std::vector<RhiTextureViewHandle> views;
        views.reserve(dispatch.resources.size());
        for (const BoundResource& resource : dispatch.resources) {
            const RhiTextureViewHandle view = pass.textureView(resource.texture);
            if (!textureViewMatches(view, resource)) {
                return false;
            }
            views.push_back(view);
        }

        DispatchBindingCache& cache = dispatchBindings[dispatchIndex];
        bool bindingMatches = cache.bindGroup.isValid() && cache.pipelineIndex == dispatch.pipelineIndex &&
                              cache.views.size() == views.size();
        for (size_t i = 0u; bindingMatches && i < views.size(); ++i) {
            bindingMatches = sameHandle(cache.views[i], views[i]);
        }
        if (!bindingMatches) {
            if (cache.bindGroup.isValid()) {
                device->destroyBindGroup(cache.bindGroup);
            }
            cache = {};
            RhiBindGroupDesc bindGroupDesc;
            bindGroupDesc.layout = pipeline.resourceLayout;
            for (size_t i = 0u; i < dispatch.resources.size(); ++i) {
                RhiBindGroupEntry entry;
                entry.binding = dispatch.resources[i].binding;
                entry.resource.textureView = views[i];
                bindGroupDesc.entries.push_back(entry);
            }
            cache.bindGroup = device->createBindGroup(bindGroupDesc);
            if (!cache.bindGroup.isValid()) {
                return false;
            }
            cache.pipelineIndex = dispatch.pipelineIndex;
            cache.views = views;
        }

        RhiCommandList& commandList = pass.commandList();
        if (dispatch.constantDataSize != 0u && !dispatch.constantDataMatchesPreviousDispatch) {
            if (dispatch.constantData.size() != dispatch.constantDataSize) {
                return false;
            }
            commandList.bufferBarrier({constantBuffer, RhiResourceState::UniformBuffer, RhiResourceState::TransferDst});
            commandList.updateBuffer(constantBuffer, 0u, dispatch.constantData.data(), dispatch.constantData.size());
            commandList.bufferBarrier({constantBuffer, RhiResourceState::TransferDst, RhiResourceState::UniformBuffer});
        }
        commandList.setComputePipeline(pipeline.pipeline);
        commandList.setBindGroup(instanceDesc->resourcesSpaceIndex, cache.bindGroup);
        commandList.setBindGroup(instanceDesc->constantBufferAndSamplersSpaceIndex, commonBindGroup);
        commandList.dispatch(dispatch.gridWidth, dispatch.gridHeight, 1u);
        return true;
    }

    void resizeDispatchBindings(const uint32_t dispatchCount) {
        for (size_t dispatchIndex = dispatchCount; dispatchIndex < dispatchBindings.size(); ++dispatchIndex) {
            if (dispatchBindings[dispatchIndex].bindGroup.isValid()) {
                device->destroyBindGroup(dispatchBindings[dispatchIndex].bindGroup);
            }
        }
        dispatchBindings.resize(dispatchCount);
    }

    RhiDevice* device = nullptr;
    ::nrd::Instance* instance = nullptr;
    const ::nrd::LibraryDesc* libraryDesc = nullptr;
    const ::nrd::InstanceDesc* instanceDesc = nullptr;
    NrdDiffuseMethod method = NrdDiffuseMethod::Relax;
    uint16_t width = 0u;
    uint16_t height = 0u;
    std::vector<PersistentTexture> permanentTextures;
    std::vector<PipelineRuntime> pipelines;
    std::vector<DispatchBindingCache> dispatchBindings;
    RhiBufferHandle constantBuffer;
    std::array<RhiSamplerHandle, 2u> samplers{};
    RhiBindGroupLayoutHandle commonLayout;
    RhiBindGroupHandle commonBindGroup;
    bool permanentPoolInitialized = false;
    bool framePending = false;
    bool executionStateValid = true;
};

NrdRenderGraphBridge::NrdRenderGraphBridge() = default;

NrdRenderGraphBridge::~NrdRenderGraphBridge() {
    shutdown();
}

NrdBridgeError NrdRenderGraphBridge::initialize(RhiDevice& device, const NrdDiffuseMethod method,
                                                const uint16_t resourceWidth, const uint16_t resourceHeight) {
    if (m_impl != nullptr) {
        m_lastError = NrdBridgeError::AlreadyInitialized;
        return m_lastError;
    }
    if (device.backend() != RhiBackend::Vulkan) {
        m_lastError = NrdBridgeError::InvalidDevice;
        return m_lastError;
    }
    if (resourceWidth == 0u || resourceHeight == 0u) {
        m_lastError = NrdBridgeError::InvalidExtent;
        return m_lastError;
    }

    std::unique_ptr<Impl> implementation = std::make_unique<Impl>();
    const NrdBridgeError error = implementation->initialize(device, method, resourceWidth, resourceHeight);
    if (error != NrdBridgeError::None) {
        implementation->shutdown();
        m_lastError = error;
        return m_lastError;
    }
    m_impl = std::move(implementation);
    m_lastError = NrdBridgeError::None;
    return m_lastError;
}

void NrdRenderGraphBridge::shutdown() {
    if (m_impl != nullptr) {
        m_impl->shutdown();
        m_impl.reset();
    }
    m_lastError = NrdBridgeError::None;
}

NrdGraphDispatchResult NrdRenderGraphBridge::addGraphDispatches(RenderGraph& graph,
                                                                const ::nrd::CommonSettings& commonSettings,
                                                                const NrdDiffuseSettings& methodSettings,
                                                                const NrdExternalResources& externalResources,
                                                                const RgPassHandle dependency) {
    NrdGraphDispatchResult result;
    bool dispatchStateAdvanced = false;
    const auto fail = [this, &result, &dispatchStateAdvanced](const NrdBridgeError error) {
        if (m_impl != nullptr && dispatchStateAdvanced) {
            m_impl->executionStateValid = false;
        }
        m_lastError = error;
        result.error = error;
        result.lastPass = {};
        result.dispatchCount = 0u;
        return result;
    };
    if (m_impl == nullptr) {
        return fail(NrdBridgeError::NotInitialized);
    }
    if (m_impl->framePending) {
        return fail(NrdBridgeError::FrameAlreadyPending);
    }
    if (!m_impl->executionStateValid) {
        return fail(NrdBridgeError::ExecutionStateInvalid);
    }
    if (!commonSettingsMatchExtent(commonSettings, m_impl->width, m_impl->height) ||
        (!m_impl->permanentPoolInitialized &&
         commonSettings.accumulationMode != ::nrd::AccumulationMode::CLEAR_AND_RESTART)) {
        return fail(NrdBridgeError::InvalidCommonSettings);
    }

    const void* selectedSettings = nullptr;
    if (m_impl->method == NrdDiffuseMethod::Relax) {
        selectedSettings = std::get_if<::nrd::RelaxSettings>(&methodSettings);
    } else {
        selectedSettings = std::get_if<::nrd::ReblurSettings>(&methodSettings);
    }
    if (selectedSettings == nullptr) {
        return fail(NrdBridgeError::MethodSettingsMismatch);
    }
    if (::nrd::SetDenoiserSettings(*m_impl->instance, kDiffuseDenoiserIdentifier, selectedSettings) !=
        ::nrd::Result::SUCCESS) {
        return fail(NrdBridgeError::DenoiserSettingsRejected);
    }
    if (::nrd::SetCommonSettings(*m_impl->instance, commonSettings) != ::nrd::Result::SUCCESS) {
        return fail(NrdBridgeError::CommonSettingsRejected);
    }

    const ::nrd::DispatchDesc* dispatchDescs = nullptr;
    uint32_t dispatchCount = 0u;
    const ::nrd::Result dispatchQuery =
        ::nrd::GetComputeDispatches(*m_impl->instance, &kDiffuseDenoiserIdentifier, 1u, dispatchDescs, dispatchCount);
    if (dispatchQuery != ::nrd::Result::SUCCESS) {
        return fail(NrdBridgeError::DispatchQueryFailed);
    }
    dispatchStateAdvanced = true;
    if (dispatchDescs == nullptr || dispatchCount == 0u) {
        return fail(NrdBridgeError::DispatchQueryFailed);
    }

    std::vector<RgTextureHandle> permanentHandles;
    permanentHandles.reserve(m_impl->permanentTextures.size());
    for (const Impl::PersistentTexture& persistent : m_impl->permanentTextures) {
        RgImportedTextureDesc imported;
        imported.name = persistent.name.c_str();
        imported.texture = persistent.texture;
        imported.desc = persistent.desc;
        imported.initialState =
            m_impl->permanentPoolInitialized ? RhiResourceState::ShaderRead : RhiResourceState::Undefined;
        imported.finalState = RhiResourceState::ShaderRead;
        imported.defaultView = persistent.view;
        imported.initialQueue = RhiQueueType::Graphics;
        imported.finalQueue = RhiQueueType::Graphics;
        const RgTextureHandle handle = graph.importTexture(imported);
        if (!handle.isValid()) {
            return fail(NrdBridgeError::GraphResourceCreationFailed);
        }
        permanentHandles.push_back(handle);
    }

    std::vector<RgTextureHandle> transientHandles;
    transientHandles.reserve(m_impl->instanceDesc->transientPoolSize);
    for (uint32_t i = 0u; i < m_impl->instanceDesc->transientPoolSize; ++i) {
        const ::nrd::TextureDesc& nrdTexture = m_impl->instanceDesc->transientPool[i];
        const std::string name = "NRD.Transient." + std::to_string(i);
        RgTransientTextureDesc transient;
        transient.name = name.c_str();
        transient.desc.debugName = name.c_str();
        transient.desc.dimension = RhiTextureDimension::Texture2D;
        transient.desc.format = *nrdTextureFormatToRhi(nrdTexture.format);
        transient.desc.width = divideUp(m_impl->width, nrdTexture.downsampleFactor);
        transient.desc.height = divideUp(m_impl->height, nrdTexture.downsampleFactor);
        transient.desc.depthOrLayers = 1u;
        transient.desc.mipLevels = 1u;
        transient.desc.sampleCount = 1u;
        transient.desc.usage = rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::Storage);
        const RgTextureHandle handle = graph.createTexture(transient);
        if (!handle.isValid()) {
            return fail(NrdBridgeError::GraphResourceCreationFailed);
        }
        transientHandles.push_back(handle);
    }

    std::vector<Impl::DispatchRuntime> dispatches;
    dispatches.reserve(dispatchCount);
    uint32_t previousConstantDataSize = 0u;
    for (uint32_t dispatchIndex = 0u; dispatchIndex < dispatchCount; ++dispatchIndex) {
        const ::nrd::DispatchDesc& nrdDispatch = dispatchDescs[dispatchIndex];
        if (nrdDispatch.name == nullptr || nrdDispatch.name[0] == '\0' ||
            nrdDispatch.identifier != kDiffuseDenoiserIdentifier ||
            nrdDispatch.pipelineIndex >= m_impl->pipelines.size() || nrdDispatch.resources == nullptr ||
            nrdDispatch.resourcesNum == 0u || nrdDispatch.gridWidth == 0u || nrdDispatch.gridHeight == 0u ||
            nrdDispatch.constantBufferDataSize > m_impl->instanceDesc->constantBufferMaxDataSize ||
            (nrdDispatch.constantBufferDataSize != 0u && nrdDispatch.constantBufferData == nullptr) ||
            (nrdDispatch.constantBufferDataMatchesPreviousDispatch &&
             (dispatchIndex == 0u || nrdDispatch.constantBufferDataSize != previousConstantDataSize))) {
            return fail(NrdBridgeError::InvalidDispatchContract);
        }
        const Impl::PipelineRuntime& pipeline = m_impl->pipelines[nrdDispatch.pipelineIndex];
        const ::nrd::PipelineDesc& pipelineDesc = m_impl->instanceDesc->pipelines[nrdDispatch.pipelineIndex];
        if (pipeline.bindings.size() != nrdDispatch.resourcesNum ||
            pipelineDesc.hasConstantData != (nrdDispatch.constantBufferDataSize != 0u)) {
            return fail(NrdBridgeError::InvalidDispatchContract);
        }

        dispatches.emplace_back();
        Impl::DispatchRuntime& dispatch = dispatches.back();
        dispatch.name = "NRD." + std::to_string(dispatchIndex) + "." + nrdDispatch.name;
        dispatch.pipelineIndex = nrdDispatch.pipelineIndex;
        dispatch.gridWidth = nrdDispatch.gridWidth;
        dispatch.gridHeight = nrdDispatch.gridHeight;
        dispatch.constantDataSize = nrdDispatch.constantBufferDataSize;
        dispatch.constantDataMatchesPreviousDispatch = nrdDispatch.constantBufferDataMatchesPreviousDispatch;
        if (dispatch.constantDataSize != 0u && !dispatch.constantDataMatchesPreviousDispatch) {
            dispatch.constantData.assign(nrdDispatch.constantBufferData,
                                         nrdDispatch.constantBufferData + nrdDispatch.constantBufferDataSize);
        }
        previousConstantDataSize = dispatch.constantDataSize;

        dispatch.resources.reserve(nrdDispatch.resourcesNum);
        for (uint32_t resourceIndex = 0u; resourceIndex < nrdDispatch.resourcesNum; ++resourceIndex) {
            const ::nrd::ResourceDesc& nrdResource = nrdDispatch.resources[resourceIndex];
            const Impl::PipelineBinding& pipelineBinding = pipeline.bindings[resourceIndex];
            if (nrdResource.descriptorType != pipelineBinding.descriptorType) {
                return fail(NrdBridgeError::InvalidDispatchContract);
            }

            Impl::BoundResource resource;
            resource.resourceType = nrdResource.type;
            resource.descriptorType = nrdResource.descriptorType;
            resource.binding = pipelineBinding.binding;
            if (nrdResource.type == ::nrd::ResourceType::PERMANENT_POOL) {
                if (nrdResource.indexInPool >= permanentHandles.size()) {
                    return fail(NrdBridgeError::InvalidDispatchContract);
                }
                resource.texture = permanentHandles[nrdResource.indexInPool];
                resource.format = m_impl->permanentTextures[nrdResource.indexInPool].desc.format;
                resource.width = static_cast<uint16_t>(m_impl->permanentTextures[nrdResource.indexInPool].desc.width);
                resource.height = static_cast<uint16_t>(m_impl->permanentTextures[nrdResource.indexInPool].desc.height);
            } else if (nrdResource.type == ::nrd::ResourceType::TRANSIENT_POOL) {
                if (nrdResource.indexInPool >= transientHandles.size()) {
                    return fail(NrdBridgeError::InvalidDispatchContract);
                }
                resource.texture = transientHandles[nrdResource.indexInPool];
                resource.format =
                    *nrdTextureFormatToRhi(m_impl->instanceDesc->transientPool[nrdResource.indexInPool].format);
                const ::nrd::TextureDesc& textureDesc = m_impl->instanceDesc->transientPool[nrdResource.indexInPool];
                resource.width = divideUp(m_impl->width, textureDesc.downsampleFactor);
                resource.height = divideUp(m_impl->height, textureDesc.downsampleFactor);
            } else {
                const std::optional<RhiTextureFormat> format = nrdDiffuseExternalTextureFormat(nrdResource.type);
                resource.texture = externalResources.texture(nrdResource.type);
                if (!format.has_value() || !resource.texture.isValid()) {
                    return fail(NrdBridgeError::MissingExternalResource);
                }
                resource.format = *format;
                resource.width = m_impl->width;
                resource.height = m_impl->height;
            }

            for (const Impl::BoundResource& existing : dispatch.resources) {
                if (sameHandle(existing.texture, resource.texture) &&
                    (existing.resourceType != resource.resourceType ||
                     existing.descriptorType != resource.descriptorType)) {
                    return fail(NrdBridgeError::InvalidDispatchContract);
                }
            }
            dispatch.resources.push_back(resource);
        }
    }

    m_impl->resizeDispatchBindings(dispatchCount);
    RgPassHandle previousPass = dependency;
    for (uint32_t dispatchIndex = 0u; dispatchIndex < dispatches.size(); ++dispatchIndex) {
        Impl::DispatchRuntime dispatch = std::move(dispatches[dispatchIndex]);
        const std::string passName = dispatch.name;
        RenderGraphPassBuilder pass =
            graph.addPass({passName.c_str(), RgPassType::Compute, RhiQueueType::Graphics, false});
        if (!pass.handle().isValid()) {
            return fail(NrdBridgeError::GraphPassCreationFailed);
        }
        if (previousPass.isValid()) {
            pass.dependsOn(previousPass);
        }

        struct UniqueAccess final {
            RgTextureHandle texture;
            ::nrd::DescriptorType descriptorType = ::nrd::DescriptorType::TEXTURE;
        };
        std::vector<UniqueAccess> uniqueAccesses;
        uniqueAccesses.reserve(dispatch.resources.size());
        for (const Impl::BoundResource& resource : dispatch.resources) {
            const auto existing =
                std::find_if(uniqueAccesses.begin(), uniqueAccesses.end(), [&resource](const UniqueAccess& access) {
                    return sameHandle(access.texture, resource.texture);
                });
            if (existing == uniqueAccesses.end()) {
                uniqueAccesses.push_back({resource.texture, resource.descriptorType});
            } else if (existing->descriptorType != resource.descriptorType) {
                return fail(NrdBridgeError::InvalidDispatchContract);
            }
        }
        for (const UniqueAccess& access : uniqueAccesses) {
            if (access.descriptorType == ::nrd::DescriptorType::TEXTURE) {
                pass.readTexture(access.texture, RhiResourceState::ShaderRead);
            } else {
                pass.writeTexture(access.texture, RhiResourceState::ShaderWrite);
            }
        }
        pass.setExecute(
            [implementation = m_impl.get(), dispatchIndex, dispatch = std::move(dispatch)](RgPassContext& context) {
                return implementation->recordDispatch(context, dispatchIndex, dispatch);
            });
        previousPass = pass.handle();
    }

    m_impl->framePending = true;
    m_lastError = NrdBridgeError::None;
    result.error = NrdBridgeError::None;
    result.lastPass = previousPass;
    result.dispatchCount = dispatchCount;
    return result;
}

void NrdRenderGraphBridge::completeGraphExecution(const RgExecuteResult& result) {
    if (m_impl == nullptr) {
        m_lastError = NrdBridgeError::NotInitialized;
        return;
    }
    if (!m_impl->framePending) {
        m_lastError = NrdBridgeError::NoFramePending;
        return;
    }
    m_impl->framePending = false;
    if (result.succeeded()) {
        m_impl->permanentPoolInitialized = true;
        m_lastError = NrdBridgeError::None;
    } else {
        m_impl->executionStateValid = false;
        m_lastError = NrdBridgeError::ExecutionStateInvalid;
    }
}

bool NrdRenderGraphBridge::initialized() const {
    return m_impl != nullptr;
}

bool NrdRenderGraphBridge::framePending() const {
    return m_impl != nullptr && m_impl->framePending;
}

std::optional<NrdDiffuseMethod> NrdRenderGraphBridge::method() const {
    if (m_impl == nullptr) {
        return std::nullopt;
    }
    return m_impl->method;
}

uint16_t NrdRenderGraphBridge::resourceWidth() const {
    return m_impl != nullptr ? m_impl->width : 0u;
}

uint16_t NrdRenderGraphBridge::resourceHeight() const {
    return m_impl != nullptr ? m_impl->height : 0u;
}

uint32_t NrdRenderGraphBridge::pipelineCount() const {
    return m_impl != nullptr ? static_cast<uint32_t>(m_impl->pipelines.size()) : 0u;
}

uint32_t NrdRenderGraphBridge::permanentPoolSize() const {
    return m_impl != nullptr ? static_cast<uint32_t>(m_impl->permanentTextures.size()) : 0u;
}

uint32_t NrdRenderGraphBridge::transientPoolSize() const {
    return m_impl != nullptr ? m_impl->instanceDesc->transientPoolSize : 0u;
}

NrdBridgeError NrdRenderGraphBridge::lastError() const {
    return m_lastError;
}

} // namespace renderer::nrd
