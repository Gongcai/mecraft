#include "BlockModelRegistry.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

#include "../../resource/ResourceMgr.h"
#include "Paths.h"

std::unordered_map<std::string, std::unique_ptr<BlockModel>> BlockModelRegistry::s_models{};
ResourceMgr* BlockModelRegistry::s_resourceMgr = nullptr;

namespace {
constexpr const char* kModelFormat = "mecraft_model_v1";
constexpr const char* kBlockModelsDir = ASSETS_DIR "/models/blocks";

uint8_t directionBitFromName(const std::string& name) {
    if (name == "up") return static_cast<uint8_t>(1u << 0u);
    if (name == "down") return static_cast<uint8_t>(1u << 1u);
    if (name == "south") return static_cast<uint8_t>(1u << 2u);
    if (name == "north") return static_cast<uint8_t>(1u << 3u);
    if (name == "west") return static_cast<uint8_t>(1u << 4u);
    if (name == "east") return static_cast<uint8_t>(1u << 5u);
    throw std::runtime_error("Unknown model face direction: " + name);
}

int directionIndexFromName(const std::string& name) {
    if (name == "up") return 0;
    if (name == "down") return 1;
    if (name == "south") return 2;
    if (name == "north") return 3;
    if (name == "west") return 4;
    if (name == "east") return 5;
    throw std::runtime_error("Unknown model face direction: " + name);
}

std::array<float, 3> readVec3(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_array() || it->size() != 3) {
        throw std::runtime_error(std::string("Model element requires vec3 field: ") + key);
    }

    std::array<float, 3> out{};
    for (size_t i = 0; i < out.size(); ++i) {
        if (!(*it)[i].is_number()) {
            throw std::runtime_error(std::string("Model vec3 field contains non-number value: ") + key);
        }
        out[i] = (*it)[i].get<float>();
    }
    return out;
}

std::array<float, 4> readUv(const nlohmann::json& json) {
    const auto it = json.find("uv");
    if (it == json.end() || !it->is_array() || it->size() != 4) {
        throw std::runtime_error("Model face requires uv with four numbers");
    }

    std::array<float, 4> out{};
    for (size_t i = 0; i < out.size(); ++i) {
        if (!(*it)[i].is_number()) {
            throw std::runtime_error("Model face uv contains non-number value");
        }
        out[i] = (*it)[i].get<float>();
    }
    return out;
}

std::string makeModelName(const std::filesystem::path& base, const std::filesystem::path& file) {
    std::filesystem::path relative = std::filesystem::relative(file, base);
    relative.replace_extension();
    std::string path = relative.generic_string();
    return "block/" + path;
}

std::string resolveTextureVariable(const std::unordered_map<std::string, std::string>& textures,
                                   const std::string& key,
                                   std::unordered_set<std::string>& visiting) {
    const auto it = textures.find(key);
    if (it == textures.end()) {
        throw std::runtime_error("Unknown model texture variable: " + key);
    }

    const std::string& value = it->second;
    if (value.empty() || value.front() != '#') {
        return value;
    }

    const std::string nextKey = value.substr(1);
    if (!visiting.insert(nextKey).second) {
        throw std::runtime_error("Cyclic model texture variable reference: " + key);
    }
    const std::string resolved = resolveTextureVariable(textures, nextKey, visiting);
    visiting.erase(nextKey);
    return resolved;
}
}

void BlockModelRegistry::init(ResourceMgr* resourceMgr) {
    s_resourceMgr = resourceMgr;
    s_models.clear();

    const std::filesystem::path modelDir(kBlockModelsDir);
    if (!std::filesystem::exists(modelDir)) {
        return;
    }
    if (!std::filesystem::is_directory(modelDir)) {
        throw std::runtime_error("Block model path is not a directory: " + modelDir.string());
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(modelDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        std::ifstream file(entry.path());
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open block model file: " + entry.path().string());
        }

        nlohmann::json root;
        file >> root;
        const std::string name = makeModelName(modelDir, entry.path());
        s_models[name] = parseModel(root, name);
    }
}

const BlockModel* BlockModelRegistry::get(const std::string& name) {
    const auto it = s_models.find(name);
    return it != s_models.end() ? it->second.get() : nullptr;
}

AnimatedTextureRef BlockModelRegistry::resolveTextureRef(const std::string& textureName) {
    if (s_resourceMgr == nullptr) {
        throw std::runtime_error("Cannot resolve model texture without ResourceMgr: " + textureName);
    }

    const TextureAnimationInfo info = s_resourceMgr->getTextureAnimation(textureName);
    AnimatedTextureRef ref;
    ref.firstLayer = info.firstLayer;
    ref.frameCount = static_cast<uint16_t>(std::max(1, info.frameCount));
    ref.fps = info.fps;
    ref.isAnimated = info.isAnimated;
    return ref;
}

std::unique_ptr<BlockModel> BlockModelRegistry::parseModel(const nlohmann::json& json, const std::string& name) {
    if (!json.is_object()) {
        throw std::runtime_error("Block model root must be an object: " + name);
    }
    const auto formatIt = json.find("format");
    if (formatIt == json.end() || !formatIt->is_string() || formatIt->get<std::string>() != kModelFormat) {
        throw std::runtime_error("Block model has unsupported format: " + name);
    }

    auto model = std::make_unique<BlockModel>();
    model->name = name;
    if (const auto aoIt = json.find("ambientocclusion"); aoIt != json.end()) {
        if (!aoIt->is_boolean()) {
            throw std::runtime_error("Block model ambientocclusion must be boolean: " + name);
        }
        model->ambientOcclusion = aoIt->get<bool>();
    }

    const auto texturesIt = json.find("textures");
    if (texturesIt == json.end() || !texturesIt->is_object()) {
        throw std::runtime_error("Block model requires textures object: " + name);
    }
    for (auto it = texturesIt->begin(); it != texturesIt->end(); ++it) {
        if (!it.value().is_string()) {
            throw std::runtime_error("Block model texture value must be string: " + name + "." + it.key());
        }
        model->textures[it.key()] = it.value().get<std::string>();
    }
    resolveTextureVariables(*model);

    const auto elementsIt = json.find("elements");
    if (elementsIt == json.end() || !elementsIt->is_array()) {
        throw std::runtime_error("Block model requires elements array: " + name);
    }
    model->elements.reserve(elementsIt->size());
    for (const nlohmann::json& elementJson : *elementsIt) {
        model->elements.push_back(parseElement(elementJson));
    }
    validateFaceTextureVariables(*model);
    return model;
}

ModelElement BlockModelRegistry::parseElement(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("Model element must be an object");
    }

    ModelElement element;
    element.from = readVec3(json, "from");
    element.to = readVec3(json, "to");

    const auto facesIt = json.find("faces");
    if (facesIt == json.end() || !facesIt->is_object()) {
        throw std::runtime_error("Model element requires faces object");
    }

    for (auto it = facesIt->begin(); it != facesIt->end(); ++it) {
        const int direction = directionIndexFromName(it.key());
        element.faces[static_cast<size_t>(direction)] = parseFace(it.value());
    }
    return element;
}

std::unique_ptr<ModelFace> BlockModelRegistry::parseFace(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("Model face must be an object");
    }

    const auto textureIt = json.find("texture");
    if (textureIt == json.end() || !textureIt->is_string()) {
        throw std::runtime_error("Model face requires texture string");
    }
    const std::string textureVar = textureIt->get<std::string>();
    if (textureVar.empty() || textureVar.front() != '#') {
        throw std::runtime_error("Model face texture must reference a texture variable");
    }

    auto face = std::make_unique<ModelFace>();
    face->textureVar = textureVar;
    face->uv = readUv(json);

    if (const auto cullIt = json.find("cullface"); cullIt != json.end()) {
        if (!cullIt->is_string()) {
            throw std::runtime_error("Model face cullface must be string");
        }
        face->cullfaceBits = directionBitFromName(cullIt->get<std::string>());
    }

    if (const auto rotationIt = json.find("rotation"); rotationIt != json.end()) {
        if (!rotationIt->is_number_integer()) {
            throw std::runtime_error("Model face rotation must be integer");
        }
        const int rotation = rotationIt->get<int>();
        if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
            throw std::runtime_error("Model face rotation must be 0, 90, 180, or 270");
        }
        face->uvRotation = static_cast<uint16_t>(rotation);
    }

    if (const auto tintIt = json.find("tintindex"); tintIt != json.end()) {
        if (!tintIt->is_number_integer()) {
            throw std::runtime_error("Model face tintindex must be integer");
        }
        face->tintIndex = static_cast<int8_t>(std::clamp(tintIt->get<int>(), -1, 127));
    }
    return face;
}

void BlockModelRegistry::resolveTextureVariables(BlockModel& model) {
    std::unordered_map<std::string, std::string> resolved;
    resolved.reserve(model.textures.size());
    for (const auto& [key, value] : model.textures) {
        std::unordered_set<std::string> visiting;
        visiting.insert(key);
        resolved[key] = resolveTextureVariable(model.textures, key, visiting);
    }
    model.textures = std::move(resolved);
}

void BlockModelRegistry::validateFaceTextureVariables(const BlockModel& model) {
    for (const ModelElement& element : model.elements) {
        for (const std::unique_ptr<ModelFace>& face : element.faces) {
            if (!face) {
                continue;
            }
            const std::string textureKey = face->textureVar.substr(1);
            if (model.textures.find(textureKey) == model.textures.end()) {
                throw std::runtime_error("Model face references undefined texture variable: " +
                                         model.name + "." + textureKey);
            }
        }
    }
}
