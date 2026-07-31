#include "renderer/upscaling/StreamlineRuntime.h"

#include <cassert>
#include <iostream>

namespace {

void printRequirements(const char* label, const std::vector<std::string>& values) {
    for (const std::string& value : values) {
        std::cout << label << '=' << value << '\n';
    }
}

} // namespace

int main() {
    StreamlineRuntime& runtime = StreamlineRuntime::instance();
    if (!runtime.initialize()) {
        std::cerr << runtime.lastError() << '\n';
        return 1;
    }

    const StreamlineVulkanRequirements& requirements = runtime.vulkanRequirements();
    std::cout << "Streamline Vulkan requirements: instanceExtensions=" << requirements.instanceExtensions.size()
              << " deviceExtensions=" << requirements.deviceExtensions.size()
              << " features12=" << requirements.features12.size() << " features13=" << requirements.features13.size()
              << " graphicsQueues=" << requirements.additionalGraphicsQueues
              << " computeQueues=" << requirements.additionalComputeQueues
              << " opticalFlowQueues=" << requirements.opticalFlowQueues << '\n';
    printRequirements("instanceExtension", requirements.instanceExtensions);
    printRequirements("deviceExtension", requirements.deviceExtensions);
    printRequirements("feature12", requirements.features12);
    printRequirements("feature13", requirements.features13);
    assert(runtime.initialized());

    if (!runtime.shutdown()) {
        std::cerr << runtime.lastError() << '\n';
        return 1;
    }
    assert(!runtime.initialized());
    return 0;
}
