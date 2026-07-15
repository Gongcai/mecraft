#include "renderer/upscaling/StreamlineRuntime.h"

#include "renderer/rhi/vulkan/VulkanRequirementCollector.h"

#include <sl.h>
#include <sl_helpers_vk.h>
#include <sl_security.h>

#include <Windows.h>

#include <array>
#include <cstddef>
#include <iostream>
#include <system_error>

namespace {

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

} // namespace

struct StreamlineRuntime::Implementation {
    HMODULE module = nullptr;
    PFun_slInit* init = nullptr;
    PFun_slShutdown* shutdown = nullptr;
    PFun_slGetFeatureRequirements* getFeatureRequirements = nullptr;
    PFun_slIsFeatureSupported* isFeatureSupported = nullptr;
    PFun_slSetVulkanInfo* setVulkanInfo = nullptr;
    StreamlineVulkanRequirements requirements;
    std::filesystem::path runtimeDirectory;
    std::string error;
    bool initialized = false;
    bool vulkanDeviceSet = false;
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
    if (m_impl->init == nullptr || m_impl->shutdown == nullptr ||
        m_impl->getFeatureRequirements == nullptr ||
        m_impl->isFeatureSupported == nullptr || m_impl->setVulkanInfo == nullptr) {
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
    m_impl->vulkanDeviceSet = true;
    m_impl->error.clear();
    return true;
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
    const sl::Result result = m_impl->shutdown();
    m_impl->initialized = false;
    m_impl->vulkanDeviceSet = false;
    m_impl->requirements = {};
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

const StreamlineVulkanRequirements& StreamlineRuntime::vulkanRequirements() const {
    return m_impl->requirements;
}

const std::string& StreamlineRuntime::lastError() const {
    return m_impl->error;
}
