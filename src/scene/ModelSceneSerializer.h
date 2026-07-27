#ifndef MECRAFT_MODEL_SCENE_SERIALIZER_H
#define MECRAFT_MODEL_SCENE_SERIALIZER_H

#include "ModelSceneDocument.h"

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace scene {

/// Converts model scene documents to and from the versioned JSON representation.
class ModelSceneSerializer {
public:
    /// Validates document IDs, references, hierarchy, transforms, and environment values.
    [[nodiscard]] static bool validate(const ModelSceneDocument& document,
                                       std::string& error);

    /// Serializes a previously validated document to a JSON object.
    [[nodiscard]] static nlohmann::json serialize(
        const ModelSceneDocument& document);

    /// Parses and validates one complete versioned scene JSON object.
    [[nodiscard]] static bool deserialize(const nlohmann::json& value,
                                          ModelSceneDocument& document,
                                          std::string& error);

    /// Writes a scene through a temporary file and atomic rename sequence.
    [[nodiscard]] static bool saveToFile(const std::string& path,
                                         const ModelSceneDocument& document,
                                         std::string& error);

    /// Reads, parses, and validates a scene file without changing runtime state.
    [[nodiscard]] static bool loadFromFile(const std::string& path,
                                           ModelSceneDocument& document,
                                           std::string& error);
};

} // namespace scene

#endif // MECRAFT_MODEL_SCENE_SERIALIZER_H
