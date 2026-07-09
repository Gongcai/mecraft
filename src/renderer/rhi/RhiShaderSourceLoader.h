#ifndef MECRAFT_RHI_SHADER_SOURCE_LOADER_H
#define MECRAFT_RHI_SHADER_SOURCE_LOADER_H

#include <optional>
#include <string>

namespace renderer::rhi {

[[nodiscard]] std::optional<std::string> loadShaderSource(const std::string& path);

} // namespace renderer::rhi

#endif // MECRAFT_RHI_SHADER_SOURCE_LOADER_H
