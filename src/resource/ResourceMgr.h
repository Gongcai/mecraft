//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_RESOURCEMGR_H
#define MECRAFT_RESOURCEMGR_H
#include <memory>
#include <string>
#include <unordered_map>
#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <utility>
#include "../renderer/Shader.h"
struct TextureAtlas {
    GLuint textureID = 0;
    int atlasWidth  = 0;     // 像素宽度
    int atlasHeight = 0;     // 像素高度
    int tileSize    = 16;    // 每个贴图块的像素尺寸
    int tilesPerRow = 0;

    // 根据 tile 索引计算 UV 坐标 (左下, 右上)
    [[nodiscard]] std::pair<glm::vec2, glm::vec2> getUV(int tileIndex) const;
};

class ResourceMgr {
public:
    void init();
    void shutdown();

    Shader *loadShader(const std::string &name,
                       const char *vertPath,
                       const char *fragPath);

    Shader *getShader(const std::string &name);

    GLuint loadTexture(const std::string& path, bool alpha = false);
    [[nodiscard]] GLuint getTexture(const std::string& name) const;

    // 纹理图集
    void buildTextureAtlas(const std::string& directory, int tileSize = 16);
    [[nodiscard]] const TextureAtlas& getAtlas() const;

private:
    std::unordered_map<std::string, std::unique_ptr<Shader>> m_shaders;
    std::unordered_map<std::string, GLuint> m_textures;
    TextureAtlas m_atlas;
};



#endif //MECRAFT_RESOURCEMGR_H