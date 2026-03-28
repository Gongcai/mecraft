//
// Created by Caiwe on 2026/3/21.
//

#include "ResourceMgr.h"
#include <iostream>
#include <filesystem>
#include <vector>
#include "../third_party/stb/stb_image.h"

std::pair<glm::vec2, glm::vec2> TextureAtlas::getUV(int tileIndex) const {
    if (tilesPerRow == 0 || atlasWidth == 0 || atlasHeight == 0) return {{0,0}, {0,0}};

    int tileCol = tileIndex % tilesPerRow;
    int tileRow = tileIndex / tilesPerRow;

    // UV起始：注意 Y从下往上递增或者从上往下递增取决于 stbi_set_flip_vertically_on_load。这里假设是自下往上
    float uMin = static_cast<float>(tileCol * tileSize) / atlasWidth;
    float vMin = static_cast<float>(tileRow * tileSize) / atlasHeight;

    float uMax = static_cast<float>((tileCol + 1) * tileSize) / atlasWidth;
    float vMax = static_cast<float>((tileRow + 1) * tileSize) / atlasHeight;

    // 可能需要稍加微缩防止边缘溢出渲染相邻格子（一般叫 bleeding，通常留点余量或只取中间避免）
    // 返回 pair： { 左下角UV(uMin, vMin), 右上角UV(uMax, vMax) }
    return { glm::vec2(uMin, vMin), glm::vec2(uMax, vMax) };
}

void ResourceMgr::init() {
    loadShader("chunk", "../assets/shaders/chunk.vs", "../assets/shaders/chunk.fs");
    loadShader("outline", "../assets/shaders/outline.vs", "../assets/shaders/outline.fs");
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
    //TODO: 完善贴图读取
    return 0;
}

GLuint ResourceMgr::getTexture(const std::string &name) const {
    auto it = m_textures.find(name);
    if (it != m_textures.end()) {
        return it->second;
    }
    return 0;
}

void ResourceMgr::buildTextureAtlas(const std::string &directory, int tileSize) {
    namespace fs = std::filesystem;
    std::vector<fs::path> imagePaths;
    if (fs::exists(directory)) {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.path().extension() == ".png") {
                imagePaths.push_back(entry.path());
            }
        }
    }

    if (imagePaths.empty()) {
        std::cerr << "Texture Atlas generated with 0 images!\n";
        return;
    }

    // 2. 简易布局：决定图集的大小（这里简单排成一排，也可以计算平方根排成正方形）
    // 为了简单起见，这里演示平铺拼成一行，或根据数量算成方阵
    int numTiles = imagePaths.size();
    int tilesPerRow = static_cast<int>(std::ceil(std::sqrt(numTiles))); // 组成尽可能的正方形
    int numRows = static_cast<int>(std::ceil(static_cast<float>(numTiles) / tilesPerRow));

    int atlasWidth = tilesPerRow * tileSize;
    int atlasHeight = numRows * tileSize;
    // RGBA 为 4 通道
    std::vector<unsigned char> atlasPixels(atlasWidth * atlasHeight * 4, 0);

    // 3. 逐个将图读取并塞入大内存里
    stbi_set_flip_vertically_on_load(true); // 是否翻转 Y 视你的坐标系习惯而定

    for (int i = 0; i < numTiles; ++i) {
        int width, height, channels;
        // 注意文件路径如果是宽字符可能有问题，统一转 string 处理
        unsigned char* data = stbi_load(imagePaths[i].string().c_str(), &width, &height, &channels, 4);

        if (data) {
            // 确保宽高和我们要的tileSize对得上
            if(width != tileSize || height != tileSize) {
                 std::cerr << "Warning: Texture size mismatch! " << imagePaths[i] << "\n";
            }

            // 计算当前这张图被放到了图集的行与列
            int tileCol = i % tilesPerRow;
            int tileRow = i / tilesPerRow;

            int startX = tileCol * tileSize;
            int startY = tileRow * tileSize;

            // 拷贝像素到图集的 buffer 中 (按行拷贝)
            for (int y = 0; y < tileSize && y < height; ++y) {
                for (int x = 0; x < tileSize && x < width; ++x) {
                    // atlas 和 原图单行的偏移
                    int destIndex = ((startY + y) * atlasWidth + (startX + x)) * 4;
                    int srcIndex = (y * width + x) * 4;
                    // RGBA 拷贝过去
                    atlasPixels[destIndex + 0] = data[srcIndex + 0];
                    atlasPixels[destIndex + 1] = data[srcIndex + 1];
                    atlasPixels[destIndex + 2] = data[srcIndex + 2];
                    atlasPixels[destIndex + 3] = data[srcIndex + 3];
                }
            }
            stbi_image_free(data);

            // 记录该名字对应的图集下标，方便外部通过名字查 tile 索引：m_textures[filename] = i;
            std::string texName = imagePaths[i].stem().string(); // 取文件名（不带扩展)
            m_textures[texName] = i;
        } else {
            std::cerr << "Failed to load image: " << imagePaths[i] << "\n";
        }
    }

    // 4. 将合并好的 pixels 上传到 GPU
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // 设置环绕，过滤参数 (对于体素风格游戏一般使用 Nearest (邻近) 滤波从而不模糊)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasWidth, atlasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlasPixels.data());
    glGenerateMipmap(GL_TEXTURE_2D); // 生成 mipmap (可选)
    glBindTexture(GL_TEXTURE_2D, 0);

    // 5. 将参数存入 m_atlas 结构体中备用
    m_atlas.textureID = textureID;
    m_atlas.atlasWidth = atlasWidth;
    m_atlas.atlasHeight = atlasHeight;
    m_atlas.tileSize = tileSize;
    m_atlas.tilesPerRow = tilesPerRow;
}

const TextureAtlas & ResourceMgr::getAtlas() const {
    return m_atlas;
}
