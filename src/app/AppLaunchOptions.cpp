#include "AppLaunchOptions.h"

#include "renderer/rhi/RhiDeviceFactory.h"

AppLaunchOptions::AppLaunchOptions()
    : rhiBackend(renderer::rhi::defaultRhiBackend()) {}
