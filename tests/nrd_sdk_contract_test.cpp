#include "renderer/nrd/NrdRenderGraphBridge.h"
#include "renderer/rhi/RhiShaderCompiler.h"

#include <NRD.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {
constexpr ::nrd::Identifier kDenoiserIdentifier = 1u;
constexpr uint16_t kResourceWidth = 16u;
constexpr uint16_t kResourceHeight = 16u;

struct MethodContract final {
    renderer::nrd::NrdDiffuseMethod method;
    ::nrd::Denoiser denoiser;
    uint32_t pipelineCount;
    uint32_t permanentPoolSize;
    uint32_t transientPoolSize;
    uint32_t constantBufferMaxDataSize;
    uint32_t firstFrameDispatchCount;
};

class InstanceOwner final {
public:
    ~InstanceOwner() {
        if (m_instance != nullptr) {
            ::nrd::DestroyInstance(*m_instance);
        }
    }

    InstanceOwner(const InstanceOwner&) = delete;
    InstanceOwner& operator=(const InstanceOwner&) = delete;
    InstanceOwner() = default;

    [[nodiscard]] bool create(const ::nrd::Denoiser denoiser) {
        const ::nrd::DenoiserDesc denoiserDesc{kDenoiserIdentifier, denoiser};
        ::nrd::InstanceCreationDesc creationDesc{};
        creationDesc.denoisers = &denoiserDesc;
        creationDesc.denoisersNum = 1u;
        return ::nrd::CreateInstance(creationDesc, m_instance) == ::nrd::Result::SUCCESS && m_instance != nullptr;
    }

    [[nodiscard]] ::nrd::Instance& instance() const { return *m_instance; }

private:
    ::nrd::Instance* m_instance = nullptr;
};

[[nodiscard]] bool requireTrue(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

void setIdentity(float (&matrix)[16]) {
    std::fill(std::begin(matrix), std::end(matrix), 0.0f);
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
}

[[nodiscard]] ::nrd::CommonSettings firstFrameCommonSettings() {
    ::nrd::CommonSettings settings{};
    setIdentity(settings.viewToClipMatrix);
    setIdentity(settings.viewToClipMatrixPrev);
    setIdentity(settings.worldToViewMatrix);
    setIdentity(settings.worldToViewMatrixPrev);
    settings.resourceSize[0] = kResourceWidth;
    settings.resourceSize[1] = kResourceHeight;
    settings.resourceSizePrev[0] = kResourceWidth;
    settings.resourceSizePrev[1] = kResourceHeight;
    settings.rectSize[0] = kResourceWidth;
    settings.rectSize[1] = kResourceHeight;
    settings.rectSizePrev[0] = kResourceWidth;
    settings.rectSizePrev[1] = kResourceHeight;
    settings.motionVectorScale[0] = 1.0f;
    settings.motionVectorScale[1] = 1.0f;
    settings.motionVectorScale[2] = 0.0f;
    settings.timeDeltaBetweenFrames = 1000.0f / 60.0f;
    settings.denoisingRange = 1000.0f;
    settings.accumulationMode = ::nrd::AccumulationMode::CLEAR_AND_RESTART;
    return settings;
}

[[nodiscard]] bool bindingMatches(const renderer::rhi::RhiShaderBindingInfo& binding, const uint32_t set,
                                  const uint32_t bindingIndex, const RhiBindingType type) {
    return binding.set == set && binding.binding == bindingIndex && binding.type == type && binding.arrayCount == 1u &&
           !binding.runtimeArray && binding.stages == rhiFlag(RhiShaderStage::Compute);
}

[[nodiscard]] const renderer::rhi::RhiShaderBindingInfo*
findBinding(const renderer::rhi::RhiShaderReflection& reflection, const uint32_t set, const uint32_t bindingIndex) {
    const auto found = std::find_if(reflection.bindings.begin(), reflection.bindings.end(),
                                    [set, bindingIndex](const renderer::rhi::RhiShaderBindingInfo& binding) {
                                        return binding.set == set && binding.binding == bindingIndex;
                                    });
    return found == reflection.bindings.end() ? nullptr : &*found;
}

[[nodiscard]] bool validatePipelineReflection(const ::nrd::LibraryDesc& libraryDesc,
                                              const ::nrd::InstanceDesc& instanceDesc,
                                              const ::nrd::PipelineDesc& pipeline, const uint32_t pipelineIndex) {
    RhiShaderDesc shaderDesc;
    const std::string debugName = "NRD.Contract.Pipeline." + std::to_string(pipelineIndex);
    shaderDesc.debugName = debugName.c_str();
    shaderDesc.stage = RhiShaderStage::Compute;
    shaderDesc.bytecode = pipeline.computeShaderSPIRV.bytecode;
    shaderDesc.bytecodeSize = pipeline.computeShaderSPIRV.size;
    shaderDesc.entryPoint = instanceDesc.shaderEntryPoint;
    std::string errorMessage;
    const std::optional<renderer::rhi::RhiCompiledShader> compiled =
        renderer::rhi::compileShaderToSpirv(shaderDesc, renderer::rhi::RhiShaderBackend::Vulkan, errorMessage);
    if (!compiled.has_value()) {
        std::cerr << "NRD pipeline " << pipelineIndex << " SPIR-V reflection failed: " << errorMessage << '\n';
        return false;
    }

    uint32_t expectedResourceBindingCount = 0u;
    std::array<bool, 2u> descriptorTypesSeen{};
    for (uint32_t rangeIndex = 0u; rangeIndex < pipeline.resourceRangesNum; ++rangeIndex) {
        const ::nrd::ResourceRangeDesc& range = pipeline.resourceRanges[rangeIndex];
        const uint32_t descriptorTypeIndex = static_cast<uint32_t>(range.descriptorType);
        if (descriptorTypeIndex >= descriptorTypesSeen.size() || descriptorTypesSeen[descriptorTypeIndex] ||
            range.descriptorsNum == 0u) {
            std::cerr << "NRD pipeline " << pipelineIndex << " has an invalid descriptor range\n";
            return false;
        }
        descriptorTypesSeen[descriptorTypeIndex] = true;
        const bool sampled = range.descriptorType == ::nrd::DescriptorType::TEXTURE;
        const uint32_t baseBinding = (sampled ? libraryDesc.spirvBindingOffsets.textureOffset
                                              : libraryDesc.spirvBindingOffsets.storageTextureAndBufferOffset) +
                                     instanceDesc.resourcesBaseRegisterIndex;
        const RhiBindingType expectedType = sampled ? RhiBindingType::SampledTexture : RhiBindingType::StorageTexture;
        for (uint32_t descriptorIndex = 0u; descriptorIndex < range.descriptorsNum; ++descriptorIndex) {
            const uint32_t bindingIndex = baseBinding + descriptorIndex;
            const renderer::rhi::RhiShaderBindingInfo* binding =
                findBinding(compiled->reflection, instanceDesc.resourcesSpaceIndex, bindingIndex);
            if (binding == nullptr ||
                !bindingMatches(*binding, instanceDesc.resourcesSpaceIndex, bindingIndex, expectedType)) {
                std::cerr << "NRD pipeline " << pipelineIndex << " resource binding " << bindingIndex
                          << " does not match its SDK descriptor range\n";
                return false;
            }
            ++expectedResourceBindingCount;
        }
    }

    uint32_t reflectedResourceBindingCount = 0u;
    for (const renderer::rhi::RhiShaderBindingInfo& binding : compiled->reflection.bindings) {
        if (binding.set == instanceDesc.resourcesSpaceIndex) {
            ++reflectedResourceBindingCount;
            continue;
        }
        if (binding.set != instanceDesc.constantBufferAndSamplersSpaceIndex) {
            std::cerr << "NRD pipeline " << pipelineIndex << " declares an unexpected descriptor set\n";
            return false;
        }
        const uint32_t constantBinding =
            libraryDesc.spirvBindingOffsets.constantBufferOffset + instanceDesc.constantBufferRegisterIndex;
        const uint32_t samplerBinding0 =
            libraryDesc.spirvBindingOffsets.samplerOffset + instanceDesc.samplersBaseRegisterIndex;
        const uint32_t samplerBinding1 = samplerBinding0 + 1u;
        const bool validCommonBinding =
            (binding.binding == constantBinding && binding.type == RhiBindingType::UniformBuffer) ||
            ((binding.binding == samplerBinding0 || binding.binding == samplerBinding1) &&
             binding.type == RhiBindingType::Sampler);
        if (!validCommonBinding || binding.arrayCount != 1u || binding.runtimeArray ||
            binding.stages != rhiFlag(RhiShaderStage::Compute)) {
            std::cerr << "NRD pipeline " << pipelineIndex << " declares an invalid common binding\n";
            return false;
        }
    }
    const uint32_t constantBinding =
        libraryDesc.spirvBindingOffsets.constantBufferOffset + instanceDesc.constantBufferRegisterIndex;
    const bool constantBindingPresent =
        findBinding(compiled->reflection, instanceDesc.constantBufferAndSamplersSpaceIndex, constantBinding) != nullptr;
    return requireTrue(reflectedResourceBindingCount == expectedResourceBindingCount,
                       "NRD SPIR-V resource binding count does not match the SDK range count") &&
           requireTrue(constantBindingPresent == pipeline.hasConstantData,
                       "NRD SPIR-V constant-buffer reflection does not match PipelineDesc::hasConstantData");
}

[[nodiscard]] bool validatePoolFormats(const ::nrd::TextureDesc* const textures, const uint32_t textureCount,
                                       const char* const poolName) {
    for (uint32_t textureIndex = 0u; textureIndex < textureCount; ++textureIndex) {
        if (textures[textureIndex].downsampleFactor == 0u ||
            !renderer::nrd::nrdTextureFormatToRhi(textures[textureIndex].format).has_value()) {
            std::cerr << poolName << " texture " << textureIndex << " has an unsupported format contract\n";
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validateFirstFrameDispatches(::nrd::Instance& instance, const ::nrd::InstanceDesc& instanceDesc,
                                                const MethodContract& contract) {
    if (contract.denoiser == ::nrd::Denoiser::RELAX_DIFFUSE) {
        const ::nrd::RelaxSettings settings{};
        if (::nrd::SetDenoiserSettings(instance, kDenoiserIdentifier, &settings) != ::nrd::Result::SUCCESS) {
            return requireTrue(false, "RELAX settings were rejected");
        }
    } else {
        const ::nrd::ReblurSettings settings{};
        if (::nrd::SetDenoiserSettings(instance, kDenoiserIdentifier, &settings) != ::nrd::Result::SUCCESS) {
            return requireTrue(false, "REBLUR settings were rejected");
        }
    }
    const ::nrd::CommonSettings commonSettings = firstFrameCommonSettings();
    if (::nrd::SetCommonSettings(instance, commonSettings) != ::nrd::Result::SUCCESS) {
        return requireTrue(false, "NRD first-frame common settings were rejected");
    }

    const ::nrd::DispatchDesc* dispatches = nullptr;
    uint32_t dispatchCount = 0u;
    if (::nrd::GetComputeDispatches(instance, &kDenoiserIdentifier, 1u, dispatches, dispatchCount) !=
            ::nrd::Result::SUCCESS ||
        dispatches == nullptr || dispatchCount != contract.firstFrameDispatchCount) {
        std::cerr << "NRD first-frame dispatch count mismatch for denoiser " << static_cast<uint32_t>(contract.denoiser)
                  << ": " << dispatchCount << '\n';
        return false;
    }

    uint32_t previousConstantDataSize = 0u;
    for (uint32_t dispatchIndex = 0u; dispatchIndex < dispatchCount; ++dispatchIndex) {
        const ::nrd::DispatchDesc& dispatch = dispatches[dispatchIndex];
        if (dispatch.name == nullptr || dispatch.name[0] == '\0' || dispatch.identifier != kDenoiserIdentifier ||
            dispatch.pipelineIndex >= instanceDesc.pipelinesNum || dispatch.resources == nullptr ||
            dispatch.resourcesNum == 0u || dispatch.gridWidth == 0u || dispatch.gridHeight == 0u ||
            dispatch.constantBufferDataSize > instanceDesc.constantBufferMaxDataSize ||
            (dispatch.constantBufferDataSize != 0u && dispatch.constantBufferData == nullptr) ||
            (dispatch.constantBufferDataMatchesPreviousDispatch &&
             (dispatchIndex == 0u || dispatch.constantBufferDataSize != previousConstantDataSize))) {
            std::cerr << "NRD dispatch " << dispatchIndex << " violates the fixed dispatch contract\n";
            return false;
        }
        const ::nrd::PipelineDesc& pipeline = instanceDesc.pipelines[dispatch.pipelineIndex];
        uint32_t expectedResourceCount = 0u;
        uint32_t resourceIndex = 0u;
        for (uint32_t rangeIndex = 0u; rangeIndex < pipeline.resourceRangesNum; ++rangeIndex) {
            const ::nrd::ResourceRangeDesc& range = pipeline.resourceRanges[rangeIndex];
            expectedResourceCount += range.descriptorsNum;
            for (uint32_t descriptorIndex = 0u; descriptorIndex < range.descriptorsNum; ++descriptorIndex) {
                if (resourceIndex >= dispatch.resourcesNum ||
                    dispatch.resources[resourceIndex].descriptorType != range.descriptorType) {
                    std::cerr << "NRD dispatch " << dispatchIndex << " resource ordering mismatch\n";
                    return false;
                }
                ++resourceIndex;
            }
        }
        if (expectedResourceCount != dispatch.resourcesNum ||
            pipeline.hasConstantData != (dispatch.constantBufferDataSize != 0u)) {
            std::cerr << "NRD dispatch " << dispatchIndex << " pipeline metadata mismatch\n";
            return false;
        }
        for (uint32_t first = 0u; first < dispatch.resourcesNum; ++first) {
            const ::nrd::ResourceDesc& firstResource = dispatch.resources[first];
            if ((firstResource.type == ::nrd::ResourceType::PERMANENT_POOL &&
                 firstResource.indexInPool >= instanceDesc.permanentPoolSize) ||
                (firstResource.type == ::nrd::ResourceType::TRANSIENT_POOL &&
                 firstResource.indexInPool >= instanceDesc.transientPoolSize) ||
                (firstResource.type != ::nrd::ResourceType::PERMANENT_POOL &&
                 firstResource.type != ::nrd::ResourceType::TRANSIENT_POOL &&
                 !renderer::nrd::nrdDiffuseExternalTextureFormat(firstResource.type).has_value())) {
                std::cerr << "NRD dispatch " << dispatchIndex << " references an invalid texture slot\n";
                return false;
            }
            for (uint32_t second = first + 1u; second < dispatch.resourcesNum; ++second) {
                const ::nrd::ResourceDesc& secondResource = dispatch.resources[second];
                const bool samePoolTexture = firstResource.type == secondResource.type &&
                                             (firstResource.type == ::nrd::ResourceType::PERMANENT_POOL ||
                                              firstResource.type == ::nrd::ResourceType::TRANSIENT_POOL) &&
                                             firstResource.indexInPool == secondResource.indexInPool;
                if (samePoolTexture && firstResource.descriptorType != secondResource.descriptorType) {
                    std::cerr << "NRD dispatch " << dispatchIndex << " aliases one pool texture as SRV and UAV\n";
                    return false;
                }
            }
        }
        previousConstantDataSize = dispatch.constantBufferDataSize;
    }
    return true;
}

[[nodiscard]] bool validateMethodContract(const ::nrd::LibraryDesc& libraryDesc, const MethodContract& contract) {
    InstanceOwner owner;
    if (!owner.create(contract.denoiser)) {
        return requireTrue(false, "NRD instance creation failed");
    }
    const ::nrd::InstanceDesc* instanceDesc = ::nrd::GetInstanceDesc(owner.instance());
    if (instanceDesc == nullptr || instanceDesc->pipelines == nullptr || instanceDesc->permanentPool == nullptr ||
        instanceDesc->transientPool == nullptr || instanceDesc->shaderEntryPoint == nullptr) {
        return requireTrue(false, "NRD instance descriptor is incomplete");
    }
    if (instanceDesc->constantBufferAndSamplersSpaceIndex != 1u || instanceDesc->resourcesSpaceIndex != 0u ||
        instanceDesc->constantBufferRegisterIndex != 0u || instanceDesc->samplersBaseRegisterIndex != 0u ||
        instanceDesc->resourcesBaseRegisterIndex != 0u || instanceDesc->samplersNum != 2u ||
        instanceDesc->samplers == nullptr || instanceDesc->samplers[0] != ::nrd::Sampler::NEAREST_CLAMP ||
        instanceDesc->samplers[1] != ::nrd::Sampler::LINEAR_CLAMP ||
        std::string_view(instanceDesc->shaderEntryPoint) != "main" ||
        instanceDesc->pipelinesNum != contract.pipelineCount ||
        instanceDesc->permanentPoolSize != contract.permanentPoolSize ||
        instanceDesc->transientPoolSize != contract.transientPoolSize ||
        instanceDesc->constantBufferMaxDataSize != contract.constantBufferMaxDataSize) {
        std::cerr << "NRD instance descriptor mismatch for denoiser " << static_cast<uint32_t>(contract.denoiser)
                  << ": spaces=" << instanceDesc->constantBufferAndSamplersSpaceIndex << ','
                  << instanceDesc->resourcesSpaceIndex << " registers=" << instanceDesc->constantBufferRegisterIndex
                  << ',' << instanceDesc->samplersBaseRegisterIndex << ',' << instanceDesc->resourcesBaseRegisterIndex
                  << " samplers=" << instanceDesc->samplersNum << " entry=" << instanceDesc->shaderEntryPoint
                  << " pipelines=" << instanceDesc->pipelinesNum << " permanent=" << instanceDesc->permanentPoolSize
                  << " transient=" << instanceDesc->transientPoolSize
                  << " constantBytes=" << instanceDesc->constantBufferMaxDataSize << '\n';
        return false;
    }
    if (!validatePoolFormats(instanceDesc->permanentPool, instanceDesc->permanentPoolSize, "Permanent pool") ||
        !validatePoolFormats(instanceDesc->transientPool, instanceDesc->transientPoolSize, "Transient pool")) {
        return false;
    }
    for (uint32_t pipelineIndex = 0u; pipelineIndex < instanceDesc->pipelinesNum; ++pipelineIndex) {
        const ::nrd::PipelineDesc& pipeline = instanceDesc->pipelines[pipelineIndex];
        if (pipeline.computeShaderSPIRV.bytecode == nullptr || pipeline.computeShaderSPIRV.size == 0u ||
            pipeline.resourceRanges == nullptr || pipeline.resourceRangesNum == 0u || pipeline.resourceRangesNum > 2u ||
            pipeline.shaderIdentifier[0] == '\0' ||
            !validatePipelineReflection(libraryDesc, *instanceDesc, pipeline, pipelineIndex)) {
            return false;
        }
    }
    return validateFirstFrameDispatches(owner.instance(), *instanceDesc, contract);
}

[[nodiscard]] bool validateLibraryContract() {
    const ::nrd::LibraryDesc* libraryDesc = ::nrd::GetLibraryDesc();
    if (libraryDesc == nullptr || libraryDesc->versionMajor != 4u || libraryDesc->versionMinor != 17u ||
        libraryDesc->versionBuild != 3u || libraryDesc->normalEncoding != ::nrd::NormalEncoding::R10_G10_B10_A2_UNORM ||
        libraryDesc->roughnessEncoding != ::nrd::RoughnessEncoding::LINEAR ||
        libraryDesc->spirvBindingOffsets.samplerOffset != 0u ||
        libraryDesc->spirvBindingOffsets.constantBufferOffset != 2u ||
        libraryDesc->spirvBindingOffsets.storageTextureAndBufferOffset != 3u ||
        libraryDesc->spirvBindingOffsets.textureOffset != 20u) {
        return requireTrue(false, "NRD library descriptor does not match the fixed 4.17.3 contract");
    }

    const std::optional<::nrd::Denoiser> relax =
        renderer::nrd::nrdDiffuseDenoiser(renderer::nrd::NrdDiffuseMethod::Relax);
    const std::optional<::nrd::Denoiser> reblur =
        renderer::nrd::nrdDiffuseDenoiser(renderer::nrd::NrdDiffuseMethod::Reblur);
    if (!relax.has_value() || *relax != ::nrd::Denoiser::RELAX_DIFFUSE || !reblur.has_value() ||
        *reblur != ::nrd::Denoiser::REBLUR_DIFFUSE ||
        renderer::nrd::nrdDiffuseDenoiser(static_cast<renderer::nrd::NrdDiffuseMethod>(255u)).has_value()) {
        return requireTrue(false, "Project diffuse method mapping is invalid");
    }

    constexpr std::array<::nrd::Format, 8u> kPoolFormats{
        ::nrd::Format::R8_UNORM,    ::nrd::Format::R8_UINT,
        ::nrd::Format::RGBA8_UNORM, ::nrd::Format::R16_UINT,
        ::nrd::Format::R16_SFLOAT,  ::nrd::Format::RGBA16_SFLOAT,
        ::nrd::Format::R32_SFLOAT,  ::nrd::Format::R10_G10_B10_A2_UNORM,
    };
    for (const ::nrd::Format format : kPoolFormats) {
        if (!renderer::nrd::nrdTextureFormatToRhi(format).has_value()) {
            return requireTrue(false, "A fixed NRD pool format has no RHI mapping");
        }
    }
    if (renderer::nrd::nrdTextureFormatToRhi(::nrd::Format::RGBA32_SFLOAT).has_value()) {
        return requireTrue(false, "Unsupported NRD formats must not enter the diffuse bridge contract");
    }

    constexpr std::array<MethodContract, 2u> kMethodContracts{
        MethodContract{renderer::nrd::NrdDiffuseMethod::Relax, ::nrd::Denoiser::RELAX_DIFFUSE, 15u, 6u, 4u, 720u, 21u},
        MethodContract{renderer::nrd::NrdDiffuseMethod::Reblur, ::nrd::Denoiser::REBLUR_DIFFUSE, 14u, 7u, 5u, 864u,
                       21u},
    };
    for (const MethodContract& contract : kMethodContracts) {
        if (!validateMethodContract(*libraryDesc, contract)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validateBridgeApiContract() {
    using renderer::nrd::NrdBridgeError;
    using renderer::nrd::NrdRenderGraphBridge;

    NrdRenderGraphBridge bridge;
    if (bridge.initialized() || bridge.framePending() || bridge.method().has_value() || bridge.resourceWidth() != 0u ||
        bridge.resourceHeight() != 0u || bridge.pipelineCount() != 0u || bridge.permanentPoolSize() != 0u ||
        bridge.transientPoolSize() != 0u || bridge.lastError() != NrdBridgeError::None) {
        return requireTrue(false, "Uninitialized NRD bridge query contract is invalid");
    }

    struct ErrorContract final {
        NrdBridgeError error;
        std::string_view stableId;
    };
    constexpr std::array<ErrorContract, 25u> kErrorContracts{
        ErrorContract{NrdBridgeError::None, "None"},
        ErrorContract{NrdBridgeError::AlreadyInitialized, "AlreadyInitialized"},
        ErrorContract{NrdBridgeError::InvalidDevice, "InvalidDevice"},
        ErrorContract{NrdBridgeError::UnsupportedDeviceCapabilities, "UnsupportedDeviceCapabilities"},
        ErrorContract{NrdBridgeError::InvalidExtent, "InvalidExtent"},
        ErrorContract{NrdBridgeError::InvalidMethod, "InvalidMethod"},
        ErrorContract{NrdBridgeError::UnsupportedLibraryContract, "UnsupportedLibraryContract"},
        ErrorContract{NrdBridgeError::InstanceCreationFailed, "InstanceCreationFailed"},
        ErrorContract{NrdBridgeError::UnsupportedInstanceContract, "UnsupportedInstanceContract"},
        ErrorContract{NrdBridgeError::UnsupportedTextureFormat, "UnsupportedTextureFormat"},
        ErrorContract{NrdBridgeError::ResourceCreationFailed, "ResourceCreationFailed"},
        ErrorContract{NrdBridgeError::PipelineCreationFailed, "PipelineCreationFailed"},
        ErrorContract{NrdBridgeError::NotInitialized, "NotInitialized"},
        ErrorContract{NrdBridgeError::NoFramePending, "NoFramePending"},
        ErrorContract{NrdBridgeError::FrameAlreadyPending, "FrameAlreadyPending"},
        ErrorContract{NrdBridgeError::ExecutionStateInvalid, "ExecutionStateInvalid"},
        ErrorContract{NrdBridgeError::MethodSettingsMismatch, "MethodSettingsMismatch"},
        ErrorContract{NrdBridgeError::InvalidCommonSettings, "InvalidCommonSettings"},
        ErrorContract{NrdBridgeError::DenoiserSettingsRejected, "DenoiserSettingsRejected"},
        ErrorContract{NrdBridgeError::CommonSettingsRejected, "CommonSettingsRejected"},
        ErrorContract{NrdBridgeError::DispatchQueryFailed, "DispatchQueryFailed"},
        ErrorContract{NrdBridgeError::MissingExternalResource, "MissingExternalResource"},
        ErrorContract{NrdBridgeError::InvalidDispatchContract, "InvalidDispatchContract"},
        ErrorContract{NrdBridgeError::GraphResourceCreationFailed, "GraphResourceCreationFailed"},
        ErrorContract{NrdBridgeError::GraphPassCreationFailed, "GraphPassCreationFailed"},
    };
    for (const ErrorContract& contract : kErrorContracts) {
        const std::optional<std::string_view> stableId = renderer::nrd::nrdBridgeErrorStableId(contract.error);
        if (!stableId.has_value() || *stableId != contract.stableId) {
            return requireTrue(false, "NRD bridge error identifier contract is invalid");
        }
    }
    if (renderer::nrd::nrdBridgeErrorStableId(static_cast<NrdBridgeError>(255u)).has_value()) {
        return requireTrue(false, "Invalid NRD bridge error values must not have identifiers");
    }

    renderer::nrd::NrdGraphDispatchResult dispatchResult;
    return requireTrue(!dispatchResult.succeeded() && dispatchResult.error == NrdBridgeError::None &&
                           dispatchResult.dispatchCount == 0u,
                       "Default NRD graph dispatch result contract is invalid");
}

[[nodiscard]] bool validSha256(const std::string_view value) {
    return value.size() == 64u && std::all_of(value.begin(), value.end(), [](const unsigned char character) {
               return std::isdigit(character) != 0 || (character >= 'a' && character <= 'f');
           });
}

[[nodiscard]] bool validateDependencyFiles() {
    const std::filesystem::path root = std::filesystem::path(MECRAFT_TEST_SOURCE_DIR) / "third_party/nrd";
    constexpr std::array<const char*, 6u> kRequiredFiles{
        "LICENSE.txt",
        "SOURCE_NOTICE.txt",
        "README.mecraft.md",
        "Dependencies/ShaderMake/LICENSE.txt",
        "Dependencies/MathLib/LICENSE.txt",
        "Generated/SPIRV-SHA256.txt",
    };
    for (const char* const relativePath : kRequiredFiles) {
        const std::filesystem::path path = root / relativePath;
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) == 0u) {
            std::cerr << "Missing NRD dependency file: " << path << '\n';
            return false;
        }
    }

    std::ifstream manifest(root / "Generated/SPIRV-SHA256.txt");
    if (!manifest) {
        return requireTrue(false, "NRD SPIR-V hash manifest cannot be opened");
    }
    std::unordered_set<std::string> files;
    std::string line;
    while (std::getline(manifest, line)) {
        if (line.size() <= 66u || line[64] != ' ' || line[65] != ' ' ||
            !validSha256(std::string_view(line).substr(0u, 64u))) {
            return requireTrue(false, "NRD SPIR-V hash manifest contains an invalid line");
        }
        const std::string fileName = line.substr(66u);
        if (fileName.empty() || !files.insert(fileName).second ||
            !std::filesystem::is_regular_file(root / "Generated/SPIRV" / fileName)) {
            return requireTrue(false, "NRD SPIR-V hash manifest references an invalid file");
        }
    }
    return requireTrue(files.size() == 31u, "NRD SPIR-V hash manifest must contain exactly 31 generated blobs");
}

} // namespace

int main() {
    return validateLibraryContract() && validateBridgeApiContract() && validateDependencyFiles() ? 0 : 1;
}
