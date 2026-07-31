#include "renderer/rhi/vulkan/VulkanRequirementCollector.h"

#include <algorithm>
#include <cstring>

void VulkanRequirementCollector::requireInstanceExtension(const char* extension) {
    if (extension == nullptr || extension[0] == '\0') {
        return;
    }
    const std::string value(extension);
    if (std::find(m_instanceExtensions.begin(), m_instanceExtensions.end(), value) == m_instanceExtensions.end()) {
        m_instanceExtensions.push_back(value);
    }
}

void VulkanRequirementCollector::requireDeviceExtension(const char* extension) {
    if (extension == nullptr || extension[0] == '\0') {
        return;
    }
    const std::string value(extension);
    if (std::find(m_deviceExtensions.begin(), m_deviceExtensions.end(), value) == m_deviceExtensions.end()) {
        m_deviceExtensions.push_back(value);
    }
}

void VulkanRequirementCollector::requireVulkan12Feature(const char* feature) {
    if (feature == nullptr || feature[0] == '\0') {
        return;
    }
    const std::string value(feature);
    if (std::find(m_vulkan12Features.begin(), m_vulkan12Features.end(), value) == m_vulkan12Features.end()) {
        m_vulkan12Features.push_back(value);
    }
}

void VulkanRequirementCollector::requireVulkan13Feature(const char* feature) {
    if (feature == nullptr || feature[0] == '\0') {
        return;
    }
    const std::string value(feature);
    if (std::find(m_vulkan13Features.begin(), m_vulkan13Features.end(), value) == m_vulkan13Features.end()) {
        m_vulkan13Features.push_back(value);
    }
}

void VulkanRequirementCollector::requireQueue(const RhiQueueType queue, const uint32_t count) {
    uint32_t* target = nullptr;
    switch (queue) {
    case RhiQueueType::Graphics: target = &m_graphicsQueueCount; break;
    case RhiQueueType::Compute: target = &m_computeQueueCount; break;
    case RhiQueueType::Transfer: target = &m_transferQueueCount; break;
    case RhiQueueType::Present: target = &m_presentQueueCount; break;
    }
    if (target != nullptr && count > *target) {
        *target = count;
    }
}

void VulkanRequirementCollector::requireAdditionalQueues(const RhiQueueType queue, const uint32_t count) {
    uint32_t* target = nullptr;
    switch (queue) {
    case RhiQueueType::Graphics: target = &m_additionalGraphicsQueueCount; break;
    case RhiQueueType::Compute: target = &m_additionalComputeQueueCount; break;
    case RhiQueueType::Transfer: target = &m_additionalTransferQueueCount; break;
    case RhiQueueType::Present: target = &m_additionalPresentQueueCount; break;
    }
    if (target != nullptr) {
        *target += count;
    }
}

void VulkanRequirementCollector::requireOpticalFlowQueues(const uint32_t count) {
    m_opticalFlowQueueCount += count;
}

std::vector<const char*> VulkanRequirementCollector::instanceExtensionNames() const {
    std::vector<const char*> names;
    names.reserve(m_instanceExtensions.size());
    for (const std::string& extension : m_instanceExtensions) {
        names.push_back(extension.c_str());
    }
    return names;
}

std::vector<const char*> VulkanRequirementCollector::deviceExtensionNames() const {
    std::vector<const char*> names;
    names.reserve(m_deviceExtensions.size());
    for (const std::string& extension : m_deviceExtensions) {
        names.push_back(extension.c_str());
    }
    return names;
}

std::vector<const char*> VulkanRequirementCollector::vulkan12FeatureNames() const {
    std::vector<const char*> names;
    names.reserve(m_vulkan12Features.size());
    for (const std::string& feature : m_vulkan12Features) {
        names.push_back(feature.c_str());
    }
    return names;
}

std::vector<const char*> VulkanRequirementCollector::vulkan13FeatureNames() const {
    std::vector<const char*> names;
    names.reserve(m_vulkan13Features.size());
    for (const std::string& feature : m_vulkan13Features) {
        names.push_back(feature.c_str());
    }
    return names;
}

bool VulkanRequirementCollector::validateInstanceExtensions(const std::vector<VkExtensionProperties>& available,
                                                            std::string& missing) const {
    for (const std::string& required : m_instanceExtensions) {
        if (!containsExtension(available, required)) {
            missing = required;
            return false;
        }
    }
    missing.clear();
    return true;
}

bool VulkanRequirementCollector::validateDeviceExtensions(const std::vector<VkExtensionProperties>& available,
                                                          std::string& missing) const {
    for (const std::string& required : m_deviceExtensions) {
        if (!containsExtension(available, required)) {
            missing = required;
            return false;
        }
    }
    missing.clear();
    return true;
}

uint32_t VulkanRequirementCollector::requiredQueueCount(const RhiQueueType queue) const {
    switch (queue) {
    case RhiQueueType::Graphics: return m_graphicsQueueCount;
    case RhiQueueType::Compute: return m_computeQueueCount;
    case RhiQueueType::Transfer: return m_transferQueueCount;
    case RhiQueueType::Present: return m_presentQueueCount;
    }
    return 0u;
}

uint32_t VulkanRequirementCollector::additionalQueueCount(const RhiQueueType queue) const {
    switch (queue) {
    case RhiQueueType::Graphics: return m_additionalGraphicsQueueCount;
    case RhiQueueType::Compute: return m_additionalComputeQueueCount;
    case RhiQueueType::Transfer: return m_additionalTransferQueueCount;
    case RhiQueueType::Present: return m_additionalPresentQueueCount;
    }
    return 0u;
}

uint32_t VulkanRequirementCollector::opticalFlowQueueCount() const {
    return m_opticalFlowQueueCount;
}

bool VulkanRequirementCollector::containsExtension(const std::vector<VkExtensionProperties>& available,
                                                   const std::string& required) {
    return std::any_of(available.begin(), available.end(), [&required](const auto& extension) {
        return std::strcmp(extension.extensionName, required.c_str()) == 0;
    });
}
