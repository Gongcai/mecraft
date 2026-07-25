#include "renderer/upscaling/StreamlineRuntime.h"

#include "renderer/rhi/vulkan/VulkanRequirementCollector.h"

#include <sl.h>
#include <sl_dlss.h>
#include <sl_helpers_vk.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_security.h>

#include <Windows.h>

#include <array>
#include <cstddef>
#include <iostream>
#include <limits>
#include <system_error>

namespace {

StreamlineRuntime* g_latencyRuntime = nullptr;
HWND g_latencyWindow = nullptr;
WNDPROC g_previousLatencyWindowProc = nullptr;

LRESULT CALLBACK streamlineLatencyWindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM wordParameter,
    const LPARAM longParameter) {
    if (g_latencyRuntime != nullptr) {
        g_latencyRuntime->processLatencyWindowMessage(message);
    }
    return CallWindowProcW(
        g_previousLatencyWindowProc, window, message, wordParameter, longParameter);
}

std::filesystem::path executableDirectory() {
    std::array<wchar_t, 32768u> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (length == 0u || length >= path.size()) {
        return {};
    }
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}

std::string streamlineResultMessage(const char* operation, const sl::Result result) {
    return std::string(operation) + " failed with Streamline result " +
           std::to_string(static_cast<int32_t>(result));
}

void appendStrings(std::vector<std::string>& destination,
                   const uint32_t count,
                   const char* const* values) {
    if (values == nullptr) {
        return;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        if (values[i] != nullptr && values[i][0] != '\0') {
            destination.emplace_back(values[i]);
        }
    }
}

sl::DLSSMode toDlssMode(const StreamlineDlssMode mode) {
    switch (mode) {
        case StreamlineDlssMode::Quality:
            return sl::DLSSMode::eMaxQuality;
        case StreamlineDlssMode::Balanced:
            return sl::DLSSMode::eBalanced;
        case StreamlineDlssMode::Performance:
            return sl::DLSSMode::eMaxPerformance;
        case StreamlineDlssMode::UltraPerformance:
            return sl::DLSSMode::eUltraPerformance;
    }
    return sl::DLSSMode::eOff;
}

sl::ReflexMode toReflexMode(const StreamlineReflexMode mode) {
    switch (mode) {
        case StreamlineReflexMode::Off:
            return sl::ReflexMode::eOff;
        case StreamlineReflexMode::LowLatency:
            return sl::ReflexMode::eLowLatency;
        case StreamlineReflexMode::LowLatencyWithBoost:
            return sl::ReflexMode::eLowLatencyWithBoost;
    }
    return sl::ReflexMode::eOff;
}

sl::PCLMarker toPclMarker(const StreamlinePclMarker marker) {
    switch (marker) {
        case StreamlinePclMarker::SimulationStart:
            return sl::PCLMarker::eSimulationStart;
        case StreamlinePclMarker::SimulationEnd:
            return sl::PCLMarker::eSimulationEnd;
        case StreamlinePclMarker::RenderSubmitStart:
            return sl::PCLMarker::eRenderSubmitStart;
        case StreamlinePclMarker::RenderSubmitEnd:
            return sl::PCLMarker::eRenderSubmitEnd;
        case StreamlinePclMarker::PresentStart:
            return sl::PCLMarker::ePresentStart;
        case StreamlinePclMarker::PresentEnd:
            return sl::PCLMarker::ePresentEnd;
        case StreamlinePclMarker::TriggerFlash:
            return sl::PCLMarker::eTriggerFlash;
        case StreamlinePclMarker::LatencyPing:
            return sl::PCLMarker::ePCLatencyPing;
    }
    return sl::PCLMarker::eMaximum;
}

uint32_t pclMarkerBit(const StreamlinePclMarker marker) {
    return 1u << static_cast<uint32_t>(marker);
}

sl::DLSSOptions makeDlssOptions(const StreamlineDlssOptions& options) {
    sl::DLSSOptions result{};
    result.mode = toDlssMode(options.mode);
    result.outputWidth = options.outputWidth;
    result.outputHeight = options.outputHeight;
    result.preExposure = options.preExposure;
    result.exposureScale = options.exposureScale;
    result.colorBuffersHDR = sl::Boolean::eTrue;
    result.useAutoExposure = sl::Boolean::eFalse;
    result.alphaUpscalingEnabled = sl::Boolean::eFalse;
    result.qualityPreset = sl::DLSSPreset::ePresetK;
    result.balancedPreset = sl::DLSSPreset::ePresetK;
    result.performancePreset = sl::DLSSPreset::ePresetM;
    result.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
    return result;
}

void copyMatrix(const std::array<float, 16u>& source, sl::float4x4& destination) {
    for (uint32_t row = 0u; row < 4u; ++row) {
        destination[row] = {
            source[row * 4u],
            source[row * 4u + 1u],
            source[row * 4u + 2u],
            source[row * 4u + 3u]
        };
    }
}

sl::Resource makeDlssResource(
    const StreamlineDlssResource& resource,
    sl::SubresourceRange& subresource) {
    sl::Resource result{
        sl::ResourceType::eTex2d,
        reinterpret_cast<void*>(resource.image),
        nullptr,
        reinterpret_cast<void*>(resource.view),
        static_cast<uint32_t>(resource.layout)
    };
    result.width = resource.extent.width;
    result.height = resource.extent.height;
    result.nativeFormat = static_cast<uint32_t>(resource.format);
    result.mipLevels = resource.mipLevels;
    result.arrayLayers = resource.arrayLayers;
    result.usage = resource.usage;
    subresource.aspectMask = resource.aspectMask;
    subresource.baseMipLevel = resource.baseMip;
    subresource.levelCount = resource.mipCount;
    subresource.baseArrayLayer = resource.baseLayer;
    subresource.layerCount = resource.layerCount;
    result.next = &subresource;
    return result;
}

sl::Extent resourceExtent(const StreamlineDlssResource& resource) {
    return {0u, 0u, resource.extent.width, resource.extent.height};
}

} // namespace

struct StreamlineRuntime::Implementation {
    HMODULE module = nullptr;
    PFun_slInit* init = nullptr;
    PFun_slShutdown* shutdown = nullptr;
    PFun_slGetFeatureRequirements* getFeatureRequirements = nullptr;
    PFun_slIsFeatureSupported* isFeatureSupported = nullptr;
    PFun_slSetVulkanInfo* setVulkanInfo = nullptr;
    PFun_slGetFeatureFunction* getFeatureFunction = nullptr;
    PFun_slGetNewFrameToken* getNewFrameToken = nullptr;
    PFun_slSetConstants* setConstants = nullptr;
    PFun_slSetTagForFrame* setTagForFrame = nullptr;
    PFun_slEvaluateFeature* evaluateFeature = nullptr;
    PFun_slFreeResources* freeResources = nullptr;
    PFun_slDLSSGetOptimalSettings* dlssGetOptimalSettings = nullptr;
    PFun_slDLSSSetOptions* dlssSetOptions = nullptr;
    PFun_slReflexGetState* reflexGetState = nullptr;
    PFun_slReflexSleep* reflexSleep = nullptr;
    PFun_slReflexSetOptions* reflexSetOptions = nullptr;
    PFun_slPCLGetState* pclGetState = nullptr;
    PFun_slPCLSetMarker* pclSetMarker = nullptr;
    PFun_slPCLSetOptions* pclSetOptions = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    PFN_vkQueuePresentKHR queuePresent = nullptr;
    StreamlineDlssOptions cachedDlssOptions;
    StreamlineDlssOptimalSettings cachedDlssSettings;
    bool cachedDlssSettingsValid = false;
    StreamlineVulkanRequirements requirements;
    std::filesystem::path runtimeDirectory;
    std::string error;
    StreamlineReflexState reflexState;
    uint32_t currentFrameIndex = 0u;
    uint32_t emittedPclMarkers = 0u;
    bool initialized = false;
    bool vulkanDeviceSet = false;
    bool reflexFrameActive = false;
};

StreamlineRuntime& StreamlineRuntime::instance() {
    static StreamlineRuntime runtime;
    return runtime;
}

StreamlineRuntime::StreamlineRuntime()
    : m_impl(std::make_unique<Implementation>()) {
}

StreamlineRuntime::~StreamlineRuntime() = default;

bool StreamlineRuntime::initialize(const std::filesystem::path& runtimeDirectory) {
    if (m_impl->initialized) {
        m_impl->error = "StreamlineRuntime: initialize was called more than once";
        return false;
    }

    std::error_code pathError;
    std::filesystem::path directory = runtimeDirectory.empty()
        ? executableDirectory()
        : std::filesystem::absolute(runtimeDirectory, pathError);
    if (pathError || directory.empty()) {
        m_impl->error = "StreamlineRuntime: failed to resolve the runtime directory";
        return false;
    }
    directory = directory.lexically_normal();
    const std::filesystem::path interposerPath = directory / "sl.interposer.dll";
    if (!std::filesystem::is_regular_file(interposerPath, pathError) || pathError) {
        m_impl->error = "StreamlineRuntime: missing sl.interposer.dll at " +
                        interposerPath.string();
        return false;
    }
    if (!sl::security::verifyEmbeddedSignature(interposerPath.c_str())) {
        m_impl->error = "StreamlineRuntime: NVIDIA signature verification failed for " +
                        interposerPath.string();
        return false;
    }

    m_impl->module = LoadLibraryExW(interposerPath.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                    LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (m_impl->module == nullptr) {
        m_impl->error = "StreamlineRuntime: failed to load sl.interposer.dll, Win32 error " +
                        std::to_string(GetLastError());
        return false;
    }

    m_impl->init = reinterpret_cast<PFun_slInit*>(
        GetProcAddress(m_impl->module, "slInit"));
    m_impl->shutdown = reinterpret_cast<PFun_slShutdown*>(
        GetProcAddress(m_impl->module, "slShutdown"));
    m_impl->getFeatureRequirements = reinterpret_cast<PFun_slGetFeatureRequirements*>(
        GetProcAddress(m_impl->module, "slGetFeatureRequirements"));
    m_impl->isFeatureSupported = reinterpret_cast<PFun_slIsFeatureSupported*>(
        GetProcAddress(m_impl->module, "slIsFeatureSupported"));
    m_impl->setVulkanInfo = reinterpret_cast<PFun_slSetVulkanInfo*>(
        GetProcAddress(m_impl->module, "slSetVulkanInfo"));
    m_impl->getFeatureFunction = reinterpret_cast<PFun_slGetFeatureFunction*>(
        GetProcAddress(m_impl->module, "slGetFeatureFunction"));
    m_impl->getNewFrameToken = reinterpret_cast<PFun_slGetNewFrameToken*>(
        GetProcAddress(m_impl->module, "slGetNewFrameToken"));
    m_impl->setConstants = reinterpret_cast<PFun_slSetConstants*>(
        GetProcAddress(m_impl->module, "slSetConstants"));
    m_impl->setTagForFrame = reinterpret_cast<PFun_slSetTagForFrame*>(
        GetProcAddress(m_impl->module, "slSetTagForFrame"));
    m_impl->evaluateFeature = reinterpret_cast<PFun_slEvaluateFeature*>(
        GetProcAddress(m_impl->module, "slEvaluateFeature"));
    m_impl->freeResources = reinterpret_cast<PFun_slFreeResources*>(
        GetProcAddress(m_impl->module, "slFreeResources"));
    m_impl->getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        GetProcAddress(m_impl->module, "vkGetDeviceProcAddr"));
    if (m_impl->init == nullptr || m_impl->shutdown == nullptr ||
        m_impl->getFeatureRequirements == nullptr ||
        m_impl->isFeatureSupported == nullptr || m_impl->setVulkanInfo == nullptr ||
        m_impl->getFeatureFunction == nullptr || m_impl->getNewFrameToken == nullptr ||
        m_impl->setConstants == nullptr || m_impl->setTagForFrame == nullptr ||
        m_impl->evaluateFeature == nullptr || m_impl->freeResources == nullptr ||
        m_impl->getDeviceProcAddr == nullptr) {
        m_impl->error = "StreamlineRuntime: the interposer is missing required exports";
        FreeLibrary(m_impl->module);
        m_impl->module = nullptr;
        return false;
    }

    const std::wstring pluginDirectory = directory.wstring();
    const wchar_t* pluginPaths[] = {pluginDirectory.c_str()};
    constexpr std::array<sl::Feature, 4u> features{
        sl::kFeatureDLSS,
        sl::kFeatureDLSS_G,
        sl::kFeatureReflex,
        sl::kFeaturePCL
    };
    sl::Preferences preferences{};
    preferences.showConsole = false;
    preferences.logLevel = sl::LogLevel::eDefault;
    preferences.pathsToPlugins = pluginPaths;
    preferences.numPathsToPlugins = 1u;
    preferences.pathToLogsAndData = pluginDirectory.c_str();
    preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking |
                        sl::PreferenceFlags::eUseManualHooking |
                        sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.featuresToLoad = features.data();
    preferences.numFeaturesToLoad = static_cast<uint32_t>(features.size());
    preferences.applicationId = MECRAFT_STREAMLINE_APPLICATION_ID;
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "0.1.0";
    preferences.projectId = "e605d759-40fa-4e7a-ad1a-a63bdc8d37f2";
    preferences.renderAPI = sl::RenderAPI::eVulkan;

    const sl::Result initResult = m_impl->init(preferences, sl::kSDKVersion);
    if (initResult != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slInit", initResult);
        FreeLibrary(m_impl->module);
        m_impl->module = nullptr;
        return false;
    }
    m_impl->initialized = true;
    m_impl->runtimeDirectory = directory;
    m_impl->requirements = {};

    for (const sl::Feature feature : features) {
        sl::FeatureRequirements featureRequirements{};
        const sl::Result requirementsResult =
            m_impl->getFeatureRequirements(feature, featureRequirements);
        if (requirementsResult != sl::Result::eOk) {
            const std::string error = streamlineResultMessage(
                "slGetFeatureRequirements", requirementsResult);
            shutdown();
            m_impl->error = error;
            return false;
        }
        const uint32_t flags = static_cast<uint32_t>(featureRequirements.flags);
        if ((flags & static_cast<uint32_t>(
                         sl::FeatureRequirementFlags::eVulkanSupported)) == 0u) {
            const std::string error =
                "StreamlineRuntime: a requested feature does not support Vulkan";
            shutdown();
            m_impl->error = error;
            return false;
        }
        appendStrings(m_impl->requirements.instanceExtensions,
                      featureRequirements.vkNumInstanceExtensions,
                      featureRequirements.vkInstanceExtensions);
        appendStrings(m_impl->requirements.deviceExtensions,
                      featureRequirements.vkNumDeviceExtensions,
                      featureRequirements.vkDeviceExtensions);
        appendStrings(m_impl->requirements.features12,
                      featureRequirements.vkNumFeatures12,
                      featureRequirements.vkFeatures12);
        appendStrings(m_impl->requirements.features13,
                      featureRequirements.vkNumFeatures13,
                      featureRequirements.vkFeatures13);
        m_impl->requirements.additionalGraphicsQueues +=
            featureRequirements.vkNumGraphicsQueuesRequired;
        m_impl->requirements.additionalComputeQueues +=
            featureRequirements.vkNumComputeQueuesRequired;
        m_impl->requirements.opticalFlowQueues +=
            featureRequirements.vkNumOpticalFlowQueuesRequired;
        m_impl->requirements.hardwareSchedulingRequired |=
            (flags & static_cast<uint32_t>(
                         sl::FeatureRequirementFlags::eHardwareSchedulingRequired)) != 0u;
        m_impl->requirements.vsyncOffRequired |=
            (flags & static_cast<uint32_t>(
                         sl::FeatureRequirementFlags::eVSyncOffRequired)) != 0u;
    }

    m_impl->error.clear();
    return true;
}

bool StreamlineRuntime::setVulkanDevice(const StreamlineVulkanDeviceInfo& info) {
    if (!m_impl->initialized || m_impl->setVulkanInfo == nullptr ||
        m_impl->vulkanDeviceSet) {
        m_impl->error = "StreamlineRuntime: Vulkan device binding is not valid in the current state";
        return false;
    }
    sl::VulkanInfo vulkanInfo{};
    vulkanInfo.instance = info.instance;
    vulkanInfo.physicalDevice = info.physicalDevice;
    vulkanInfo.device = info.device;
    vulkanInfo.graphicsQueueFamily = info.graphicsQueueFamily;
    vulkanInfo.graphicsQueueIndex = info.graphicsQueueIndex;
    vulkanInfo.computeQueueFamily = info.computeQueueFamily;
    vulkanInfo.computeQueueIndex = info.computeQueueIndex;
    vulkanInfo.opticalFlowQueueFamily = info.opticalFlowQueueFamily;
    vulkanInfo.opticalFlowQueueIndex = info.opticalFlowQueueIndex;
    vulkanInfo.useNativeOpticalFlowMode = info.useNativeOpticalFlow;
    const sl::Result result = m_impl->setVulkanInfo(vulkanInfo);
    if (result != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slSetVulkanInfo", result);
        return false;
    }
    constexpr std::array<sl::Feature, 4u> features{
        sl::kFeatureDLSS,
        sl::kFeatureDLSS_G,
        sl::kFeatureReflex,
        sl::kFeaturePCL
    };
    constexpr std::array<const char*, 4u> featureNames{
        "DLSS", "DLSS Frame Generation", "Reflex", "PCL"
    };
    sl::AdapterInfo adapterInfo{};
    adapterInfo.vkPhysicalDevice = info.physicalDevice;
    for (size_t i = 0u; i < features.size(); ++i) {
        const sl::Result supportResult =
            m_impl->isFeatureSupported(features[i], adapterInfo);
        if (supportResult != sl::Result::eOk) {
            m_impl->error = std::string("StreamlineRuntime: ") + featureNames[i] +
                            " is unavailable, result " +
                            std::to_string(static_cast<int32_t>(supportResult));
            return false;
        }
    }
    void* dlssGetOptimalSettings = nullptr;
    sl::Result functionResult = m_impl->getFeatureFunction(
        sl::kFeatureDLSS, "slDLSSGetOptimalSettings", dlssGetOptimalSettings);
    if (functionResult != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage(
            "slGetFeatureFunction(slDLSSGetOptimalSettings)", functionResult);
        return false;
    }
    if (dlssGetOptimalSettings == nullptr) {
        m_impl->error = "StreamlineRuntime: slDLSSGetOptimalSettings resolved to null";
        return false;
    }
    void* dlssSetOptions = nullptr;
    functionResult = m_impl->getFeatureFunction(
        sl::kFeatureDLSS, "slDLSSSetOptions", dlssSetOptions);
    if (functionResult != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage(
            "slGetFeatureFunction(slDLSSSetOptions)", functionResult);
        return false;
    }
    if (dlssSetOptions == nullptr) {
        m_impl->error = "StreamlineRuntime: slDLSSSetOptions resolved to null";
        return false;
    }
    m_impl->dlssGetOptimalSettings =
        reinterpret_cast<PFun_slDLSSGetOptimalSettings*>(dlssGetOptimalSettings);
    m_impl->dlssSetOptions =
        reinterpret_cast<PFun_slDLSSSetOptions*>(dlssSetOptions);

    const auto resolveFeatureFunction = [this](
        const sl::Feature feature,
        const char* name,
        void*& function) {
        const sl::Result result = m_impl->getFeatureFunction(
            feature, name, function);
        if (result != sl::Result::eOk) {
            m_impl->error = streamlineResultMessage(name, result);
            return false;
        }
        if (function == nullptr) {
            m_impl->error = std::string("StreamlineRuntime: ") + name +
                            " resolved to null";
            return false;
        }
        return true;
    };
    void* reflexGetState = nullptr;
    void* reflexSleep = nullptr;
    void* reflexSetOptions = nullptr;
    void* pclGetState = nullptr;
    void* pclSetMarker = nullptr;
    void* pclSetOptions = nullptr;
    if (!resolveFeatureFunction(
            sl::kFeatureReflex, "slReflexGetState", reflexGetState) ||
        !resolveFeatureFunction(
            sl::kFeatureReflex, "slReflexSleep", reflexSleep) ||
        !resolveFeatureFunction(
            sl::kFeatureReflex, "slReflexSetOptions", reflexSetOptions) ||
        !resolveFeatureFunction(
            sl::kFeaturePCL, "slPCLGetState", pclGetState) ||
        !resolveFeatureFunction(
            sl::kFeaturePCL, "slPCLSetMarker", pclSetMarker) ||
        !resolveFeatureFunction(
            sl::kFeaturePCL, "slPCLSetOptions", pclSetOptions)) {
        return false;
    }
    m_impl->reflexGetState =
        reinterpret_cast<PFun_slReflexGetState*>(reflexGetState);
    m_impl->reflexSleep =
        reinterpret_cast<PFun_slReflexSleep*>(reflexSleep);
    m_impl->reflexSetOptions =
        reinterpret_cast<PFun_slReflexSetOptions*>(reflexSetOptions);
    m_impl->pclGetState =
        reinterpret_cast<PFun_slPCLGetState*>(pclGetState);
    m_impl->pclSetMarker =
        reinterpret_cast<PFun_slPCLSetMarker*>(pclSetMarker);
    m_impl->pclSetOptions =
        reinterpret_cast<PFun_slPCLSetOptions*>(pclSetOptions);
    m_impl->queuePresent = reinterpret_cast<PFN_vkQueuePresentKHR>(
        m_impl->getDeviceProcAddr(info.device, "vkQueuePresentKHR"));
    if (m_impl->queuePresent == nullptr) {
        m_impl->error = "StreamlineRuntime: failed to resolve the Vulkan present proxy";
        return false;
    }
    m_impl->vulkanDeviceSet = true;

    sl::PCLOptions pclOptions{};
    const sl::Result pclOptionsResult = m_impl->pclSetOptions(pclOptions);
    if (pclOptionsResult != sl::Result::eOk) {
        m_impl->vulkanDeviceSet = false;
        m_impl->error = streamlineResultMessage(
            "slPCLSetOptions", pclOptionsResult);
        return false;
    }
    if (!configureReflex(StreamlineReflexMode::LowLatency) ||
        !queryReflexState(m_impl->reflexState)) {
        m_impl->vulkanDeviceSet = false;
        return false;
    }
    m_impl->error.clear();
    return true;
}

bool StreamlineRuntime::queryDlssOptimalSettings(
    const StreamlineDlssOptions& options,
    StreamlineDlssOptimalSettings& settings) {
    if (!m_impl->vulkanDeviceSet || m_impl->dlssGetOptimalSettings == nullptr ||
        options.outputWidth == 0u || options.outputHeight == 0u) {
        m_impl->error = "StreamlineRuntime: DLSS optimal settings query is not valid in the current state";
        return false;
    }
    if (m_impl->cachedDlssSettingsValid &&
        m_impl->cachedDlssOptions.mode == options.mode &&
        m_impl->cachedDlssOptions.outputWidth == options.outputWidth &&
        m_impl->cachedDlssOptions.outputHeight == options.outputHeight) {
        settings = m_impl->cachedDlssSettings;
        m_impl->error.clear();
        return true;
    }
    const sl::DLSSOptions dlssOptions = makeDlssOptions(options);
    sl::DLSSOptimalSettings optimalSettings{};
    const sl::Result result = m_impl->dlssGetOptimalSettings(
        dlssOptions, optimalSettings);
    if (result != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage(
            "slDLSSGetOptimalSettings", result);
        return false;
    }
    if (optimalSettings.optimalRenderWidth == 0u ||
        optimalSettings.optimalRenderHeight == 0u) {
        m_impl->error = "StreamlineRuntime: DLSS returned an invalid optimal render extent";
        return false;
    }
    settings.renderWidth = optimalSettings.optimalRenderWidth;
    settings.renderHeight = optimalSettings.optimalRenderHeight;
    settings.renderWidthMin = optimalSettings.renderWidthMin;
    settings.renderHeightMin = optimalSettings.renderHeightMin;
    settings.renderWidthMax = optimalSettings.renderWidthMax;
    settings.renderHeightMax = optimalSettings.renderHeightMax;
    m_impl->cachedDlssOptions = options;
    m_impl->cachedDlssSettings = settings;
    m_impl->cachedDlssSettingsValid = true;
    m_impl->error.clear();
    return true;
}

bool StreamlineRuntime::configureDlss(
    const uint32_t viewport,
    const StreamlineDlssOptions& options) {
    if (!m_impl->vulkanDeviceSet || m_impl->dlssSetOptions == nullptr ||
        options.outputWidth == 0u || options.outputHeight == 0u) {
        m_impl->error = "StreamlineRuntime: DLSS configuration is not valid in the current state";
        return false;
    }
    const sl::ViewportHandle viewportHandle{viewport};
    const sl::DLSSOptions dlssOptions = makeDlssOptions(options);
    const sl::Result result = m_impl->dlssSetOptions(
        viewportHandle, dlssOptions);
    if (result != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slDLSSSetOptions", result);
        return false;
    }
    m_impl->error.clear();
    return true;
}

bool StreamlineRuntime::evaluateDlss(const StreamlineDlssDispatchInfo& info) {
    if (!m_impl->vulkanDeviceSet || info.commandBuffer == VK_NULL_HANDLE) {
        m_impl->error = "StreamlineRuntime: DLSS evaluation is not valid in the current state";
        return false;
    }
    const uint32_t frameIndex = static_cast<uint32_t>(info.frameIndex);
    sl::FrameToken* frameToken = nullptr;
    sl::Result result = m_impl->getNewFrameToken(frameToken, &frameIndex);
    if (result != sl::Result::eOk || frameToken == nullptr) {
        m_impl->error = streamlineResultMessage("slGetNewFrameToken", result);
        return false;
    }

    sl::Constants constants{};
    copyMatrix(info.constants.cameraViewToClip, constants.cameraViewToClip);
    copyMatrix(info.constants.clipToCameraView, constants.clipToCameraView);
    copyMatrix(info.constants.clipToPrevClip, constants.clipToPrevClip);
    copyMatrix(info.constants.prevClipToClip, constants.prevClipToClip);
    constants.jitterOffset = {
        info.constants.jitterOffset[0], info.constants.jitterOffset[1]};
    constants.mvecScale = {
        info.constants.motionVectorScale[0],
        info.constants.motionVectorScale[1]};
    constants.cameraPinholeOffset = {0.0f, 0.0f};
    constants.cameraPos = {
        info.constants.cameraPosition[0],
        info.constants.cameraPosition[1],
        info.constants.cameraPosition[2]};
    constants.cameraUp = {
        info.constants.cameraUp[0],
        info.constants.cameraUp[1],
        info.constants.cameraUp[2]};
    constants.cameraRight = {
        info.constants.cameraRight[0],
        info.constants.cameraRight[1],
        info.constants.cameraRight[2]};
    constants.cameraFwd = {
        info.constants.cameraForward[0],
        info.constants.cameraForward[1],
        info.constants.cameraForward[2]};
    constants.cameraNear = info.constants.cameraNear;
    constants.cameraFar = info.constants.cameraFar;
    constants.cameraFOV = info.constants.verticalFovRadians;
    constants.cameraAspectRatio = info.constants.cameraAspectRatio;
    constants.depthInverted = info.constants.depthInverted
        ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.reset = info.constants.reset
        ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.orthographicProjection = sl::Boolean::eFalse;
    constants.motionVectorsDilated = sl::Boolean::eTrue;
    constants.motionVectorsJittered = sl::Boolean::eFalse;
    const sl::ViewportHandle viewportHandle{info.viewport};
    result = m_impl->setConstants(constants, *frameToken, viewportHandle);
    if (result != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slSetConstants", result);
        return false;
    }

    std::array<sl::SubresourceRange, 5u> subresources{};
    std::array<sl::Resource, 5u> resources{
        makeDlssResource(info.inputColor, subresources[0]),
        makeDlssResource(info.outputColor, subresources[1]),
        makeDlssResource(info.depth, subresources[2]),
        makeDlssResource(info.motionVectors, subresources[3]),
        makeDlssResource(info.exposure, subresources[4])
    };
    std::array<sl::Extent, 5u> extents{
        resourceExtent(info.inputColor),
        resourceExtent(info.outputColor),
        resourceExtent(info.depth),
        resourceExtent(info.motionVectors),
        resourceExtent(info.exposure)
    };
    std::array<sl::ResourceTag, 5u> tags{
        sl::ResourceTag{&resources[0], sl::kBufferTypeScalingInputColor,
                        sl::ResourceLifecycle::eValidUntilEvaluate, &extents[0]},
        sl::ResourceTag{&resources[1], sl::kBufferTypeScalingOutputColor,
                        sl::ResourceLifecycle::eValidUntilEvaluate, &extents[1]},
        sl::ResourceTag{&resources[2], sl::kBufferTypeDepth,
                        sl::ResourceLifecycle::eValidUntilEvaluate, &extents[2]},
        sl::ResourceTag{&resources[3], sl::kBufferTypeMotionVectors,
                        sl::ResourceLifecycle::eValidUntilEvaluate, &extents[3]},
        sl::ResourceTag{&resources[4], sl::kBufferTypeExposure,
                        sl::ResourceLifecycle::eValidUntilEvaluate, &extents[4]}
    };
    auto* commandBuffer = reinterpret_cast<sl::CommandBuffer*>(info.commandBuffer);
    result = m_impl->setTagForFrame(
        *frameToken, viewportHandle, tags.data(),
        static_cast<uint32_t>(tags.size()), commandBuffer);
    if (result != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slSetTagForFrame", result);
        return false;
    }
    const sl::BaseStructure* inputs[] = {&viewportHandle};
    result = m_impl->evaluateFeature(
        sl::kFeatureDLSS, *frameToken, inputs, 1u, commandBuffer);
    if (result != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slEvaluateFeature(DLSS)", result);
        return false;
    }
    m_impl->error.clear();
    return true;
}

bool StreamlineRuntime::releaseDlssResources(const uint32_t viewport) {
    if (!m_impl->vulkanDeviceSet || m_impl->freeResources == nullptr) {
        m_impl->error = "StreamlineRuntime: DLSS resource release is not valid in the current state";
        return false;
    }
    const sl::ViewportHandle viewportHandle{viewport};
    const sl::Result result = m_impl->freeResources(
        sl::kFeatureDLSS, viewportHandle);
    if (result != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slFreeResources(DLSS)", result);
        return false;
    }
    m_impl->error.clear();
    return true;
}

bool StreamlineRuntime::configureReflex(
    const StreamlineReflexMode mode,
    const uint32_t frameLimitMicroseconds) {
    if (!m_impl->vulkanDeviceSet || m_impl->reflexSetOptions == nullptr) {
        m_impl->error =
            "StreamlineRuntime: Reflex configuration is not valid in the current state";
        return false;
    }
    sl::ReflexOptions options{};
    options.mode = toReflexMode(mode);
    options.frameLimitUs = frameLimitMicroseconds;
    const sl::Result result = m_impl->reflexSetOptions(options);
    if (result != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slReflexSetOptions", result);
        return false;
    }
    m_impl->error.clear();
    return true;
}

bool StreamlineRuntime::queryReflexState(StreamlineReflexState& state) {
    if (!m_impl->vulkanDeviceSet || m_impl->reflexGetState == nullptr ||
        m_impl->pclGetState == nullptr) {
        m_impl->error =
            "StreamlineRuntime: Reflex state query is not valid in the current state";
        return false;
    }
    sl::ReflexState reflexState{};
    const sl::Result reflexResult = m_impl->reflexGetState(reflexState);
    if (reflexResult != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slReflexGetState", reflexResult);
        return false;
    }
    sl::PCLState pclState{};
    const sl::Result pclResult = m_impl->pclGetState(pclState);
    if (pclResult != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slPCLGetState", pclResult);
        return false;
    }
    state.lowLatencyAvailable = reflexState.lowLatencyAvailable;
    state.flashIndicatorDriverControlled =
        reflexState.flashIndicatorDriverControlled;
    state.statsWindowMessage = pclState.statsWindowMessage;
    m_impl->reflexState = state;
    m_impl->error.clear();
    return true;
}

bool StreamlineRuntime::beginReflexFrame(const uint32_t frameIndex) {
    if (!m_impl->vulkanDeviceSet || m_impl->getNewFrameToken == nullptr ||
        m_impl->reflexSleep == nullptr || frameIndex == 0u) {
        m_impl->error =
            "StreamlineRuntime: Reflex frame start is not valid in the current state";
        return false;
    }
    sl::FrameToken* frameToken = nullptr;
    const sl::Result tokenResult = m_impl->getNewFrameToken(
        frameToken, &frameIndex);
    if (tokenResult != sl::Result::eOk || frameToken == nullptr) {
        m_impl->error = streamlineResultMessage(
            "slGetNewFrameToken(Reflex)", tokenResult);
        return false;
    }
    const sl::Result sleepResult = m_impl->reflexSleep(*frameToken);
    if (sleepResult != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slReflexSleep", sleepResult);
        return false;
    }
    m_impl->currentFrameIndex = frameIndex;
    m_impl->emittedPclMarkers = 0u;
    m_impl->reflexFrameActive = true;
    m_impl->error.clear();
    return true;
}

bool StreamlineRuntime::setPclMarker(
    const uint32_t frameIndex,
    const StreamlinePclMarker marker) {
    if (!m_impl->vulkanDeviceSet || !m_impl->reflexFrameActive ||
        m_impl->pclSetMarker == nullptr ||
        frameIndex != m_impl->currentFrameIndex) {
        m_impl->error =
            "StreamlineRuntime: PCL marker does not match the active frame";
        return false;
    }
    const uint32_t markerBit = pclMarkerBit(marker);
    if ((m_impl->emittedPclMarkers & markerBit) != 0u) {
        m_impl->error.clear();
        return true;
    }
    const sl::PCLMarker pclMarker = toPclMarker(marker);
    if (pclMarker == sl::PCLMarker::eMaximum) {
        m_impl->error = "StreamlineRuntime: invalid PCL marker";
        return false;
    }
    sl::FrameToken* frameToken = nullptr;
    const sl::Result tokenResult = m_impl->getNewFrameToken(
        frameToken, &frameIndex);
    if (tokenResult != sl::Result::eOk || frameToken == nullptr) {
        m_impl->error = streamlineResultMessage(
            "slGetNewFrameToken(PCL)", tokenResult);
        return false;
    }
    const sl::Result markerResult = m_impl->pclSetMarker(
        pclMarker, *frameToken);
    if (markerResult != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slPCLSetMarker", markerResult);
        return false;
    }
    m_impl->emittedPclMarkers |= markerBit;
    m_impl->error.clear();
    return true;
}

bool StreamlineRuntime::attachLatencyWindow(void* nativeWindowHandle) {
    if (!m_impl->vulkanDeviceSet || nativeWindowHandle == nullptr ||
        g_latencyWindow != nullptr || g_latencyRuntime != nullptr) {
        m_impl->error =
            "StreamlineRuntime: latency window attachment is not valid in the current state";
        return false;
    }
    StreamlineReflexState state;
    if (!queryReflexState(state) || state.statsWindowMessage == 0u) {
        if (m_impl->error.empty()) {
            m_impl->error =
                "StreamlineRuntime: PCL returned an invalid latency message identifier";
        }
        return false;
    }
    const HWND window = static_cast<HWND>(nativeWindowHandle);
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(
        window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&streamlineLatencyWindowProcedure));
    if (previous == 0 && GetLastError() != ERROR_SUCCESS) {
        m_impl->error =
            "StreamlineRuntime: failed to install the latency window procedure, Win32 error " +
            std::to_string(GetLastError());
        return false;
    }
    g_latencyRuntime = this;
    g_latencyWindow = window;
    g_previousLatencyWindowProc = reinterpret_cast<WNDPROC>(previous);
    m_impl->error.clear();
    return true;
}

void StreamlineRuntime::detachLatencyWindow() {
    if (g_latencyRuntime != this || g_latencyWindow == nullptr ||
        g_previousLatencyWindowProc == nullptr) {
        return;
    }
    SetWindowLongPtrW(
        g_latencyWindow, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(g_previousLatencyWindowProc));
    g_latencyRuntime = nullptr;
    g_latencyWindow = nullptr;
    g_previousLatencyWindowProc = nullptr;
}

void StreamlineRuntime::processLatencyWindowMessage(const uint32_t message) {
    if (!m_impl->reflexFrameActive) {
        return;
    }
    if (message == m_impl->reflexState.statsWindowMessage) {
        static_cast<void>(setPclMarker(
            m_impl->currentFrameIndex, StreamlinePclMarker::LatencyPing));
    } else if (message == WM_LBUTTONDOWN) {
        static_cast<void>(setPclMarker(
            m_impl->currentFrameIndex, StreamlinePclMarker::TriggerFlash));
    }
}

VkResult StreamlineRuntime::presentVulkanFrame(
    const VkQueue queue,
    const VkPresentInfoKHR& info) {
    if (!m_impl->vulkanDeviceSet || m_impl->queuePresent == nullptr ||
        queue == VK_NULL_HANDLE) {
        m_impl->error = "StreamlineRuntime: Vulkan presentation proxy is not available";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkResult result = m_impl->queuePresent(queue, &info);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR &&
        result != VK_ERROR_OUT_OF_DATE_KHR &&
        result != VK_ERROR_SURFACE_LOST_KHR &&
        result != VK_ERROR_DEVICE_LOST) {
        m_impl->error = "StreamlineRuntime: Vulkan presentation proxy returned " +
                        std::to_string(static_cast<int32_t>(result));
        return result;
    }
    m_impl->error.clear();
    return result;
}

bool StreamlineRuntime::appendVulkanRequirements(
    VulkanRequirementCollector& collector) const {
    if (!m_impl->initialized) {
        return false;
    }
    for (const std::string& extension : m_impl->requirements.instanceExtensions) {
        collector.requireInstanceExtension(extension.c_str());
    }
    for (const std::string& extension : m_impl->requirements.deviceExtensions) {
        if (extension == VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) {
            continue;
        }
        collector.requireDeviceExtension(extension.c_str());
    }
    for (const std::string& feature : m_impl->requirements.features12) {
        collector.requireVulkan12Feature(feature.c_str());
    }
    for (const std::string& feature : m_impl->requirements.features13) {
        collector.requireVulkan13Feature(feature.c_str());
    }
    collector.requireVulkan13Feature("privateData");
    collector.requireAdditionalQueues(
        RhiQueueType::Graphics, m_impl->requirements.additionalGraphicsQueues);
    collector.requireAdditionalQueues(
        RhiQueueType::Compute, m_impl->requirements.additionalComputeQueues);
    collector.requireOpticalFlowQueues(m_impl->requirements.opticalFlowQueues);
    return true;
}

bool StreamlineRuntime::shutdown() {
    if (!m_impl->initialized) {
        return true;
    }
    detachLatencyWindow();
    const sl::Result result = m_impl->shutdown();
    m_impl->initialized = false;
    m_impl->vulkanDeviceSet = false;
    m_impl->requirements = {};
    m_impl->dlssGetOptimalSettings = nullptr;
    m_impl->dlssSetOptions = nullptr;
    m_impl->reflexGetState = nullptr;
    m_impl->reflexSleep = nullptr;
    m_impl->reflexSetOptions = nullptr;
    m_impl->pclGetState = nullptr;
    m_impl->pclSetMarker = nullptr;
    m_impl->pclSetOptions = nullptr;
    m_impl->queuePresent = nullptr;
    m_impl->cachedDlssSettingsValid = false;
    m_impl->reflexState = {};
    m_impl->currentFrameIndex = 0u;
    m_impl->emittedPclMarkers = 0u;
    m_impl->reflexFrameActive = false;
    if (m_impl->module != nullptr) {
        FreeLibrary(m_impl->module);
        m_impl->module = nullptr;
    }
    if (result != sl::Result::eOk) {
        m_impl->error = streamlineResultMessage("slShutdown", result);
        return false;
    }
    m_impl->error.clear();
    return true;
}

bool StreamlineRuntime::initialized() const {
    return m_impl->initialized;
}

bool StreamlineRuntime::vulkanDeviceSet() const {
    return m_impl->vulkanDeviceSet;
}

bool StreamlineRuntime::reflexLowLatencyAvailable() const {
    return m_impl->vulkanDeviceSet && m_impl->reflexState.lowLatencyAvailable;
}

const StreamlineVulkanRequirements& StreamlineRuntime::vulkanRequirements() const {
    return m_impl->requirements;
}

const std::string& StreamlineRuntime::lastError() const {
    return m_impl->error;
}
