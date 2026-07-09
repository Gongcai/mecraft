#include "renderer/rhi/RhiShaderSourceLoader.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace renderer::rhi {
namespace {
[[nodiscard]] std::string trimIncludeToken(std::string token) {
    const size_t first = token.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = token.find_last_not_of(" \t\r\n");
    token = token.substr(first, last - first + 1);
    if (token.size() >= 2 &&
        ((token.front() == '"' && token.back() == '"') ||
         (token.front() == '<' && token.back() == '>'))) {
        return token.substr(1, token.size() - 2);
    }
    return {};
}

[[nodiscard]] std::optional<std::string> readTextFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::stringstream stream;
    stream << file.rdbuf();
    if (file.bad()) {
        return std::nullopt;
    }
    return stream.str();
}

[[nodiscard]] std::optional<std::string> resolveIncludes(const std::string& source,
                                                         const std::string& sourcePath,
                                                         std::unordered_set<std::string>& includeStack) {
    const std::filesystem::path currentPath = std::filesystem::absolute(sourcePath).lexically_normal();
    const std::filesystem::path currentDir = currentPath.parent_path();

    std::stringstream input(source);
    std::string output;
    std::string line;
    while (std::getline(input, line)) {
        const size_t directiveStart = line.find_first_not_of(" \t");
        if (directiveStart != std::string::npos && line.compare(directiveStart, 8, "#include") == 0) {
            const std::string includeName = trimIncludeToken(line.substr(directiveStart + 8));
            if (!includeName.empty()) {
                const std::filesystem::path includePath =
                    std::filesystem::absolute(currentDir / includeName).lexically_normal();
                const std::string includeKey = includePath.string();
                if (!includeStack.insert(includeKey).second) {
                    return std::nullopt;
                }

                const std::optional<std::string> includeSource = readTextFile(includeKey);
                if (!includeSource.has_value()) {
                    includeStack.erase(includeKey);
                    return std::nullopt;
                }

                output += "#line 1 0\n";
                const std::optional<std::string> resolvedInclude =
                    resolveIncludes(*includeSource, includeKey, includeStack);
                includeStack.erase(includeKey);
                if (!resolvedInclude.has_value()) {
                    return std::nullopt;
                }
                output += *resolvedInclude;
                output += "\n#line 1 0\n";
                continue;
            }
        }

        output += line;
        output += '\n';
    }
    return output;
}
} // namespace

std::optional<std::string> loadShaderSource(const std::string& path) {
    const std::optional<std::string> source = readTextFile(path);
    if (!source.has_value()) {
        return std::nullopt;
    }

    const std::filesystem::path sourcePath = std::filesystem::absolute(path).lexically_normal();
    std::unordered_set<std::string> includeStack;
    includeStack.insert(sourcePath.string());
    return resolveIncludes(*source, sourcePath.string(), includeStack);
}

} // namespace renderer::rhi
