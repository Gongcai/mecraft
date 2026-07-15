#include "renderer/rhi/vulkan/VulkanRequirementCollector.h"

#include <algorithm>
#include <cstring>

void VulkanRequirementCollector::requireInstanceExtension(const char* extension) {
    if (extension == nullptr || extension[0] == '\0') {
        return;
    }
    const std::string value(extension);
    if (std::find(m_instanceExtensions.begin(), m_instanceExtensions.end(), value) ==
        m_instanceExtensions.end()) {
        m_instanceExtensions.push_back(value);
    }
}

void VulkanRequirementCollector::requireDeviceExtension(const char* extension) {
    if (extension == nullptr || extension[0] == '\0') {
        return;
    }
    const std::string value(extension);
    if (std::find(m_deviceExtensions.begin(), m_deviceExtensions.end(), value) ==
        m_deviceExtensions.end()) {
        m_deviceExtensions.push_back(value);
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

bool VulkanRequirementCollector::validateInstanceExtensions(
    const std::vector<VkExtensionProperties>& available,
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

bool VulkanRequirementCollector::validateDeviceExtensions(
    const std::vector<VkExtensionProperties>& available,
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

bool VulkanRequirementCollector::containsExtension(
    const std::vector<VkExtensionProperties>& available,
    const std::string& required) {
    return std::any_of(available.begin(), available.end(), [&required](const auto& extension) {
        return std::strcmp(extension.extensionName, required.c_str()) == 0;
    });
}
