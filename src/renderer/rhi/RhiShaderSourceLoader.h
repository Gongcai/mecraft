#ifndef MECRAFT_RHI_SHADER_SOURCE_LOADER_H
#define MECRAFT_RHI_SHADER_SOURCE_LOADER_H

#include <optional>
#include <string>
#include <vector>

namespace renderer::rhi {

struct RhiShaderSourceOptions {
    std::vector<std::string> preprocessorDefinitions;
};

[[nodiscard]] std::optional<std::string> loadShaderSource(const std::string& path);
[[nodiscard]] std::optional<std::string> loadShaderSource(
    const std::string& path,
    const RhiShaderSourceOptions& options);

} // namespace renderer::rhi

#endif // MECRAFT_RHI_SHADER_SOURCE_LOADER_H
