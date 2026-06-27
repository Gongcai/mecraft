#include "ContainerUiRegistry.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "../../Paths.h"
#include "../../engine/registry/NamespacedId.h"

namespace ui {

bool ContainerUiRegistry::s_initialized = false;
std::unordered_map<std::string, ContainerUiDef> ContainerUiRegistry::s_defs;

namespace {
constexpr const char* kContainerUiConfigDir = CONTAINER_UI_CONFIG_DIR;

const nlohmann::json& requireField(const nlohmann::json& owner,
                                   const std::string& ownerName,
                                   const char* fieldName) {
    if (!owner.contains(fieldName)) {
        throw std::runtime_error(ownerName + " is missing required field: " + fieldName);
    }
    return owner[fieldName];
}

std::string requireString(const nlohmann::json& owner,
                          const std::string& ownerName,
                          const char* fieldName) {
    const nlohmann::json& value = requireField(owner, ownerName, fieldName);
    if (!value.is_string()) {
        throw std::runtime_error(ownerName + " requires string field: " + fieldName);
    }
    const std::string parsed = value.get<std::string>();
    if (parsed.empty()) {
        throw std::runtime_error(ownerName + " requires non-empty field: " + fieldName);
    }
    return parsed;
}

float requireNumber(const nlohmann::json& owner,
                    const std::string& ownerName,
                    const char* fieldName) {
    const nlohmann::json& value = requireField(owner, ownerName, fieldName);
    if (!value.is_number()) {
        throw std::runtime_error(ownerName + " requires numeric field: " + fieldName);
    }
    return value.get<float>();
}

float requireFiniteNumber(const nlohmann::json& owner,
                          const std::string& ownerName,
                          const char* fieldName) {
    const float parsed = requireNumber(owner, ownerName, fieldName);
    if (!std::isfinite(parsed)) {
        throw std::runtime_error(ownerName + " requires finite numeric field: " + fieldName);
    }
    return parsed;
}

float requireNormalizedNumber(const nlohmann::json& owner,
                              const std::string& ownerName,
                              const char* fieldName) {
    const float parsed = requireFiniteNumber(owner, ownerName, fieldName);
    if (parsed < 0.0f || parsed > 1.0f) {
        throw std::runtime_error(ownerName + " requires normalized field in range [0, 1]: " + fieldName);
    }
    return parsed;
}

int requirePositiveInteger(const nlohmann::json& owner,
                           const std::string& ownerName,
                           const char* fieldName) {
    const nlohmann::json& value = requireField(owner, ownerName, fieldName);
    if (!value.is_number_integer()) {
        throw std::runtime_error(ownerName + " requires integer field: " + fieldName);
    }
    const int parsed = value.get<int>();
    if (parsed <= 0) {
        throw std::runtime_error(ownerName + " requires positive field: " + fieldName);
    }
    return parsed;
}

int requireNonNegativeInteger(const nlohmann::json& owner,
                              const std::string& ownerName,
                              const char* fieldName) {
    const nlohmann::json& value = requireField(owner, ownerName, fieldName);
    if (!value.is_number_integer()) {
        throw std::runtime_error(ownerName + " requires integer field: " + fieldName);
    }
    const int parsed = value.get<int>();
    if (parsed < 0) {
        throw std::runtime_error(ownerName + " requires non-negative field: " + fieldName);
    }
    return parsed;
}

bool requireBoolean(const nlohmann::json& owner,
                    const std::string& ownerName,
                    const char* fieldName) {
    const nlohmann::json& value = requireField(owner, ownerName, fieldName);
    if (!value.is_boolean()) {
        throw std::runtime_error(ownerName + " requires boolean field: " + fieldName);
    }
    return value.get<bool>();
}

std::string parseNamespacedField(const nlohmann::json& owner,
                                 const std::string& ownerName,
                                 const char* fieldName) {
    const NamespacedId id(requireString(owner, ownerName, fieldName));
    if (id.namespaceStr().empty() || id.path().empty()) {
        throw std::runtime_error(ownerName + " requires valid namespaced field: " + fieldName);
    }
    return id.full();
}

ContainerSlotGroupKind parseSlotGroupKind(const std::string& value, const std::string& ownerName) {
    if (value == "container") {
        return ContainerSlotGroupKind::Container;
    }
    if (value == "player_inventory") {
        return ContainerSlotGroupKind::PlayerInventory;
    }
    if (value == "crafting_input") {
        return ContainerSlotGroupKind::CraftingInput;
    }
    if (value == "crafting_result") {
        return ContainerSlotGroupKind::CraftingResult;
    }
    throw std::runtime_error(ownerName + " has unknown slot group kind: " + value);
}

ContainerProgressKind parseProgressKind(const std::string& value, const std::string& ownerName) {
    if (value == "burn") {
        return ContainerProgressKind::Burn;
    }
    if (value == "cook") {
        return ContainerProgressKind::Cook;
    }
    throw std::runtime_error(ownerName + " has unknown progress kind: " + value);
}

ContainerSlotGroupDef parseSlotGroup(const nlohmann::json& slotJson,
                                     const std::string& containerId,
                                     const int index) {
    if (!slotJson.is_object()) {
        throw std::runtime_error(containerId + ".slotGroups[" + std::to_string(index) + "] must be an object");
    }
    const std::string ownerName = containerId + ".slotGroups[" + std::to_string(index) + "]";
    ContainerSlotGroupDef group;
    group.id = requireString(slotJson, ownerName, "id");
    group.kind = parseSlotGroupKind(requireString(slotJson, ownerName, "kind"), ownerName);
    group.firstSlot = requireNonNegativeInteger(slotJson, ownerName, "firstSlot");
    group.columns = requirePositiveInteger(slotJson, ownerName, "columns");
    group.rows = requirePositiveInteger(slotJson, ownerName, "rows");
    group.x = requireNumber(slotJson, ownerName, "x");
    group.y = requireNumber(slotJson, ownerName, "y");
    group.slotSize = requireNumber(slotJson, ownerName, "slotSize");
    if (group.slotSize <= 0.0f) {
        throw std::runtime_error(ownerName + " requires positive field: slotSize");
    }
    group.columnGap = requireNumber(slotJson, ownerName, "columnGap");
    group.rowGap = requireNumber(slotJson, ownerName, "rowGap");
    group.row4ExtraGap = requireNumber(slotJson, ownerName, "row4ExtraGap");
    return group;
}

ContainerProgressDef parseProgress(const nlohmann::json& progressJson,
                                   const std::string& containerId,
                                   const int index) {
    if (!progressJson.is_object()) {
        throw std::runtime_error(containerId + ".progressBars[" + std::to_string(index) + "] must be an object");
    }
    const std::string ownerName = containerId + ".progressBars[" + std::to_string(index) + "]";
    ContainerProgressDef progress;
    progress.id = requireString(progressJson, ownerName, "id");
    progress.kind = parseProgressKind(requireString(progressJson, ownerName, "kind"), ownerName);
    progress.x = requireNumber(progressJson, ownerName, "x");
    progress.y = requireNumber(progressJson, ownerName, "y");
    progress.width = requireNumber(progressJson, ownerName, "width");
    progress.height = requireNumber(progressJson, ownerName, "height");
    if (progress.width <= 0.0f || progress.height <= 0.0f) {
        throw std::runtime_error(ownerName + " requires positive width and height");
    }
    progress.textureX = requireNumber(progressJson, ownerName, "textureX");
    progress.textureY = requireNumber(progressJson, ownerName, "textureY");
    progress.direction = requireString(progressJson, ownerName, "direction");
    if (progress.direction != "up" && progress.direction != "right") {
        throw std::runtime_error(ownerName + " has unknown progress direction: " + progress.direction);
    }
    return progress;
}

ContainerUiDef parseContainerUiDef(const nlohmann::json& root, const std::string& sourceName) {
    if (!root.is_object()) {
        throw std::runtime_error("Container UI file must contain an object: " + sourceName);
    }

    ContainerUiDef def;
    def.id = parseNamespacedField(root, sourceName, "id");
    def.behavior = parseNamespacedField(root, def.id, "behavior");
    def.backgroundTexture = requireString(root, def.id, "backgroundTexture");
    def.backgroundTexturePath = requireString(root, def.id, "backgroundTexturePath");
    def.width = requireFiniteNumber(root, def.id, "width");
    def.height = requireFiniteNumber(root, def.id, "height");
    if (def.width <= 0.0f || def.height <= 0.0f) {
        throw std::runtime_error(def.id + " requires positive width and height");
    }
    def.textureWidth = requireFiniteNumber(root, def.id, "textureWidth");
    def.textureHeight = requireFiniteNumber(root, def.id, "textureHeight");
    if (def.textureWidth <= 0.0f || def.textureHeight <= 0.0f) {
        throw std::runtime_error(def.id + " requires positive textureWidth and textureHeight");
    }
    def.anchorX = requireNormalizedNumber(root, def.id, "anchorX");
    def.anchorY = requireNormalizedNumber(root, def.id, "anchorY");
    def.pivotX = requireNormalizedNumber(root, def.id, "pivotX");
    def.pivotY = requireNormalizedNumber(root, def.id, "pivotY");
    def.offsetX = requireFiniteNumber(root, def.id, "offsetX");
    def.offsetY = requireFiniteNumber(root, def.id, "offsetY");
    def.scale = requireFiniteNumber(root, def.id, "scale");
    if (def.scale <= 0.0f) {
        throw std::runtime_error(def.id + " requires positive scale");
    }
    def.fitPadding = requireFiniteNumber(root, def.id, "fitPadding");
    if (def.fitPadding < 0.0f) {
        throw std::runtime_error(def.id + " requires non-negative fitPadding");
    }
    def.showPlayerPreview = requireBoolean(root, def.id, "showPlayerPreview");

    const nlohmann::json& slotGroups = requireField(root, def.id, "slotGroups");
    if (!slotGroups.is_array()) {
        throw std::runtime_error(def.id + " requires array field: slotGroups");
    }
    for (int i = 0; i < static_cast<int>(slotGroups.size()); ++i) {
        def.slotGroups.push_back(parseSlotGroup(slotGroups[static_cast<std::size_t>(i)], def.id, i));
    }

    const nlohmann::json& progressBars = requireField(root, def.id, "progressBars");
    if (!progressBars.is_array()) {
        throw std::runtime_error(def.id + " requires array field: progressBars");
    }
    for (int i = 0; i < static_cast<int>(progressBars.size()); ++i) {
        def.progressBars.push_back(parseProgress(progressBars[static_cast<std::size_t>(i)], def.id, i));
    }

    return def;
}

void loadContainerUiFile(const std::filesystem::path& path,
                         std::unordered_map<std::string, ContainerUiDef>& defs) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open container UI config: " + path.string());
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse container UI config " + path.string() + ": " + e.what());
    }

    ContainerUiDef def = parseContainerUiDef(root, path.string());
    if (defs.find(def.id) != defs.end()) {
        throw std::runtime_error("Duplicate container UI id: " + def.id);
    }
    defs.emplace(def.id, std::move(def));
}
}

void ContainerUiRegistry::init() {
    if (s_initialized) {
        return;
    }

    const std::filesystem::path configDir(kContainerUiConfigDir);
    if (!std::filesystem::exists(configDir) || !std::filesystem::is_directory(configDir)) {
        throw std::runtime_error("Container UI config directory is missing: " + configDir.string());
    }

    std::unordered_map<std::string, ContainerUiDef> defs;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(configDir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        loadContainerUiFile(entry.path(), defs);
    }

    if (defs.empty()) {
        throw std::runtime_error("Container UI config directory contains no JSON definitions: " + configDir.string());
    }

    s_defs = std::move(defs);
    s_initialized = true;
}

void ContainerUiRegistry::ensureInitialized() {
    if (!s_initialized) {
        init();
    }
}

const ContainerUiDef& ContainerUiRegistry::require(const std::string& id) {
    ensureInitialized();
    const NamespacedId namespacedId(id);
    const std::string canonicalId = namespacedId.full();
    const auto it = s_defs.find(canonicalId);
    if (it == s_defs.end()) {
        throw std::runtime_error("Container UI is not registered: " + canonicalId);
    }
    return it->second;
}

bool ContainerUiRegistry::tryGet(const std::string& id, const ContainerUiDef*& outDef) {
    ensureInitialized();
    const NamespacedId namespacedId(id);
    const std::string canonicalId = namespacedId.full();
    const auto it = s_defs.find(canonicalId);
    if (it == s_defs.end()) {
        outDef = nullptr;
        return false;
    }
    outDef = &it->second;
    return true;
}

const std::unordered_map<std::string, ContainerUiDef>& ContainerUiRegistry::all() {
    ensureInitialized();
    return s_defs;
}

} // namespace ui
