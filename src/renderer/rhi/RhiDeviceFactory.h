#ifndef MECRAFT_RHI_DEVICE_FACTORY_H
#define MECRAFT_RHI_DEVICE_FACTORY_H

#include "renderer/rhi/RhiTypes.h"

#include <memory>

class RhiDevice;

namespace renderer::rhi {

[[nodiscard]] std::unique_ptr<RhiDevice> createRhiDevice(RhiBackend backend);
[[nodiscard]] std::unique_ptr<RhiDevice> createDefaultRhiDevice();

} // namespace renderer::rhi

#endif // MECRAFT_RHI_DEVICE_FACTORY_H
