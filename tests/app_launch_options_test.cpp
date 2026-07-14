#include "app/AppLaunchOptions.h"

#include <iostream>
#include <optional>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    AppLaunchOptions options;
    options.rhiBackend = RhiBackend::OpenGL;

    if (!requireTrue(resolveLaunchRhiBackend(options, RhiBackend::Vulkan) == RhiBackend::Vulkan,
                     "saved backend must select the next launch backend")) {
        return 1;
    }
    if (!requireTrue(resolveLaunchRhiBackend(options, std::nullopt) == RhiBackend::OpenGL,
                     "configured default must select the backend when no saved value exists")) {
        return 1;
    }

    options.rhiBackendExplicit = true;
    options.rhiBackend = RhiBackend::OpenGL;
    if (!requireTrue(resolveLaunchRhiBackend(options, RhiBackend::Vulkan) == RhiBackend::OpenGL,
                     "explicit command-line backend must override the saved backend")) {
        return 1;
    }

    return 0;
}
