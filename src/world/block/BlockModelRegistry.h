#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Block.h"
#include "BlockModel.h"

class ResourceMgr;

class BlockModelRegistry {
public:
    static void init(ResourceMgr* resourceMgr);
    [[nodiscard]] static const BlockModel* get(const std::string& name);
    [[nodiscard]] static AnimatedTextureRef resolveTextureRef(const std::string& textureName);

private:
    static std::unique_ptr<BlockModel> parseModel(const nlohmann::json& json, const std::string& name);
    static ModelElement parseElement(const nlohmann::json& json);
    static std::unique_ptr<ModelFace> parseFace(const nlohmann::json& json);
    static void resolveTextureVariables(BlockModel& model);
    static void validateFaceTextureVariables(const BlockModel& model);

    static std::unordered_map<std::string, std::unique_ptr<BlockModel>> s_models;
    static ResourceMgr* s_resourceMgr;
};
