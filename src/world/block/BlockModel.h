#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ModelFace {
    std::string textureVar;
    std::array<float, 4> uv{};
    uint8_t cullfaceBits = 0;
    uint8_t uvRotation = 0;
    int8_t tintIndex = -1;
};

struct ModelElement {
    std::array<float, 3> from{};
    std::array<float, 3> to{};
    std::array<std::unique_ptr<ModelFace>, 6> faces{};
};

struct BlockModel {
    std::string name;
    bool ambientOcclusion = true;
    std::unordered_map<std::string, std::string> textures;
    std::vector<ModelElement> elements;
};

struct ModelTransform {
    uint8_t rotY = 0;
    uint8_t rotX = 0;
    uint8_t rotZ = 0;
};

struct ModelVariant {
    const BlockModel* model = nullptr;
    ModelTransform transform{};
};
