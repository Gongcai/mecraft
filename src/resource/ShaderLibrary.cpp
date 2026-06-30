#include "ShaderLibrary.h"

#include <stdexcept>

Shader* ShaderLibrary::load(const std::string& name, const char* vertPath, const char* fragPath) {
    if (m_shaders.find(name) != m_shaders.end()) {
        throw std::runtime_error("Duplicate shader resource: " + name);
    }

    auto shader = std::make_unique<Shader>(vertPath, fragPath);
    Shader* result = shader.get();
    m_shaders.emplace(name, std::move(shader));
    return result;
}

Shader* ShaderLibrary::get(const std::string& name) const {
    const auto it = m_shaders.find(name);
    if (it != m_shaders.end()) {
        return it->second.get();
    }
    return nullptr;
}

void ShaderLibrary::clear() {
    m_shaders.clear();
}
