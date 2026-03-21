//
// Created by Caiwe on 2026/3/21.
//

#include "ResourceMgr.h"


void ResourceMgr::init() {
}

void ResourceMgr::shutdown() {
}

Shader *ResourceMgr::loadShader(const std::string &name, const char *vertPath, const char *fragPath) {
    std::pair<std::string, std::unique_ptr<Shader>> key;
    key.first = name;
    key.second = std::make_unique<Shader>(vertPath,fragPath);
    m_shaders.insert(std::move(key));
    return m_shaders[name].get();
}

Shader *ResourceMgr::getShader(const std::string &name) {
    auto it = m_shaders.find(name);
    if (it != m_shaders.end()) {
        return it->second.get();
    }
    return nullptr;
}

GLuint ResourceMgr::loadTexture(const std::string &path, bool alpha) {
}

GLuint ResourceMgr::getTexture(const std::string &name) const {
}

void ResourceMgr::buildTextureAtlas(const std::string &directory, int tileSize) {
}

const TextureAtlas & ResourceMgr::getAtlas() const {
}
