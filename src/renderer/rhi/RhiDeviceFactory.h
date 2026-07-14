#ifndef MECRAFT_RHI_DEVICE_FACTORY_H
#define MECRAFT_RHI_DEVICE_FACTORY_H

#include "renderer/rhi/RhiTypes.h"

#include <memory>
#include <optional>
#include <string_view>

class RhiDevice;

namespace renderer::rhi {

[[nodiscard]] RhiBackend defaultRhiBackend();
[[nodiscard]] const char* rhiBackendDisplayName(RhiBackend backend);
[[nodiscard]] const char* rhiBackendConfigName(RhiBackend backend);
[[nodiscard]] std::optional<RhiBackend> parseRhiBackend(std::string_view name);
[[nodiscard]] bool isRhiBackendAvailable(RhiBackend backend);
[[nodiscard]] std::unique_ptr<RhiDevice> createRhiDevice(RhiBackend backend);
[[nodiscard]] std::unique_ptr<RhiDevice> createDefaultRhiDevice();

} // namespace renderer::rhi

#endif // MECRAFT_RHI_DEVICE_FACTORY_H
