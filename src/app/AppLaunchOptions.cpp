#include "AppLaunchOptions.h"

#include "renderer/rhi/RhiDeviceFactory.h"

AppLaunchOptions::AppLaunchOptions()
    : rhiBackend(renderer::rhi::defaultRhiBackend()) {}

RhiBackend resolveLaunchRhiBackend(const AppLaunchOptions& options,
                                   const std::optional<RhiBackend> savedBackend) {
    if (options.rhiBackendExplicit) {
        return options.rhiBackend;
    }
    return savedBackend.value_or(options.rhiBackend);
}
