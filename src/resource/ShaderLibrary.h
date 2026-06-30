#ifndef MECRAFT_SHADER_LIBRARY_H
#define MECRAFT_SHADER_LIBRARY_H

#include "../renderer/core/Shader.h"

#include <memory>
#include <string>
#include <unordered_map>

class ShaderLibrary {
public:
    Shader* load(const std::string& name, const char* vertPath, const char* fragPath);
    [[nodiscard]] Shader* get(const std::string& name) const;
    void clear();

private:
    std::unordered_map<std::string, std::unique_ptr<Shader>> m_shaders;
};

#endif // MECRAFT_SHADER_LIBRARY_H
