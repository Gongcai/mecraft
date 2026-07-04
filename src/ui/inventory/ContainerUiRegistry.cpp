#include "ContainerUiRegistry.h"

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

#include <nlohmann/json.hpp>

#include "../../Paths.h"
#include "../../engine/registry/NamespacedId.h"

namespace ui {

bool ContainerUiRegistry::s_initialized = false;
std::unordered_map<std::string, ContainerUiDef> ContainerUiRegistry::s_defs;

namespace {
constexpr const char* kContainerUiConfigDir = CONTAINER_UI_CONFIG_DIR;

bool fail(std::string& error, std::string message) {
    error = std::move(message);
    return false;
}

const nlohmann::json* findRequiredField(const nlohmann::json& owner,
                                        const std::string& ownerName,
                                        const char* fieldName,
                                        std::string& error) {
    const auto it = owner.find(fieldName);
    if (it == owner.end()) {
        fail(error, ownerName + " is missing required field: " + fieldName);
        return nullptr;
    }
    return &(*it);
}

bool readString(const nlohmann::json& owner,
                const std::string& ownerName,
                const char* fieldName,
                std::string& out,
                std::string& error) {
    const nlohmann::json* value = findRequiredField(owner, ownerName, fieldName, error);
    if (value == nullptr) {
        return false;
    }
    if (!value->is_string()) {
        return fail(error, ownerName + " requires string field: " + fieldName);
    }
    const std::string parsed = value->get<std::string>();
    if (parsed.empty()) {
        return fail(error, ownerName + " requires non-empty field: " + fieldName);
    }
    out = parsed;
    return true;
}

bool readNumber(const nlohmann::json& owner,
                const std::string& ownerName,
                const char* fieldName,
                float& out,
                std::string& error) {
    const nlohmann::json* value = findRequiredField(owner, ownerName, fieldName, error);
    if (value == nullptr) {
        return false;
    }
    if (!value->is_number()) {
        return fail(error, ownerName + " requires numeric field: " + fieldName);
    }
    out = value->get<float>();
    return true;
}

bool readFiniteNumber(const nlohmann::json& owner,
                      const std::string& ownerName,
                      const char* fieldName,
                      float& out,
                      std::string& error) {
    if (!readNumber(owner, ownerName, fieldName, out, error)) {
        return false;
    }
    if (!std::isfinite(out)) {
        return fail(error, ownerName + " requires finite numeric field: " + fieldName);
    }
    return true;
}

bool readNormalizedNumber(const nlohmann::json& owner,
                          const std::string& ownerName,
                          const char* fieldName,
                          float& out,
                          std::string& error) {
    if (!readFiniteNumber(owner, ownerName, fieldName, out, error)) {
        return false;
    }
    if (out < 0.0f || out > 1.0f) {
        return fail(error, ownerName + " requires normalized field in range [0, 1]: " + fieldName);
    }
    return true;
}

bool readInteger(const nlohmann::json& owner,
                 const std::string& ownerName,
                 const char* fieldName,
                 int& out,
                 std::string& error) {
    const nlohmann::json* value = findRequiredField(owner, ownerName, fieldName, error);
    if (value == nullptr) {
        return false;
    }
    if (!value->is_number_integer()) {
        return fail(error, ownerName + " requires integer field: " + fieldName);
    }
    const int64_t parsed = value->get<int64_t>();
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return fail(error, ownerName + " integer field is out of range: " + fieldName);
    }
    out = static_cast<int>(parsed);
    return true;
}

bool readPositiveInteger(const nlohmann::json& owner,
                         const std::string& ownerName,
                         const char* fieldName,
                         int& out,
                         std::string& error) {
    if (!readInteger(owner, ownerName, fieldName, out, error)) {
        return false;
    }
    if (out <= 0) {
        return fail(error, ownerName + " requires positive field: " + fieldName);
    }
    return true;
}

bool readNonNegativeInteger(const nlohmann::json& owner,
                            const std::string& ownerName,
                            const char* fieldName,
                            int& out,
                            std::string& error) {
    if (!readInteger(owner, ownerName, fieldName, out, error)) {
        return false;
    }
    if (out < 0) {
        return fail(error, ownerName + " requires non-negative field: " + fieldName);
    }
    return true;
}

bool readBoolean(const nlohmann::json& owner,
                 const std::string& ownerName,
                 const char* fieldName,
                 bool& out,
                 std::string& error) {
    const nlohmann::json* value = findRequiredField(owner, ownerName, fieldName, error);
    if (value == nullptr) {
        return false;
    }
    if (!value->is_boolean()) {
        return fail(error, ownerName + " requires boolean field: " + fieldName);
    }
    out = value->get<bool>();
    return true;
}

bool readNamespacedField(const nlohmann::json& owner,
                         const std::string& ownerName,
                         const char* fieldName,
                         std::string& out,
                         std::string& error) {
    std::string raw;
    if (!readString(owner, ownerName, fieldName, raw, error)) {
        return false;
    }
    const NamespacedId id(raw);
    if (id.namespaceStr().empty() || id.path().empty()) {
        return fail(error, ownerName + " requires valid namespaced field: " + fieldName);
    }
    out = id.full();
    return true;
}

bool parseSlotGroupKind(const std::string& value,
                        const std::string& ownerName,
                        ContainerSlotGroupKind& out,
                        std::string& error) {
    if (value == "container") {
        out = ContainerSlotGroupKind::Container;
        return true;
    }
    if (value == "player_inventory") {
        out = ContainerSlotGroupKind::PlayerInventory;
        return true;
    }
    if (value == "crafting_input") {
        out = ContainerSlotGroupKind::CraftingInput;
        return true;
    }
    if (value == "crafting_result") {
        out = ContainerSlotGroupKind::CraftingResult;
        return true;
    }
    return fail(error, ownerName + " has unknown slot group kind: " + value);
}

bool parseProgressKind(const std::string& value,
                       const std::string& ownerName,
                       ContainerProgressKind& out,
                       std::string& error) {
    if (value == "burn") {
        out = ContainerProgressKind::Burn;
        return true;
    }
    if (value == "cook") {
        out = ContainerProgressKind::Cook;
        return true;
    }
    return fail(error, ownerName + " has unknown progress kind: " + value);
}

bool parseSlotGroup(const nlohmann::json& slotJson,
                    const std::string& containerId,
                    const int index,
                    ContainerSlotGroupDef& group,
                    std::string& error) {
    if (!slotJson.is_object()) {
        return fail(error, containerId + ".slotGroups[" + std::to_string(index) + "] must be an object");
    }
    const std::string ownerName = containerId + ".slotGroups[" + std::to_string(index) + "]";
    std::string kind;
    if (!readString(slotJson, ownerName, "id", group.id, error) ||
        !readString(slotJson, ownerName, "kind", kind, error) ||
        !parseSlotGroupKind(kind, ownerName, group.kind, error) ||
        !readNonNegativeInteger(slotJson, ownerName, "firstSlot", group.firstSlot, error) ||
        !readPositiveInteger(slotJson, ownerName, "columns", group.columns, error) ||
        !readPositiveInteger(slotJson, ownerName, "rows", group.rows, error) ||
        !readNumber(slotJson, ownerName, "x", group.x, error) ||
        !readNumber(slotJson, ownerName, "y", group.y, error) ||
        !readNumber(slotJson, ownerName, "slotSize", group.slotSize, error)) {
        return false;
    }
    if (group.slotSize <= 0.0f) {
        return fail(error, ownerName + " requires positive field: slotSize");
    }
    return readNumber(slotJson, ownerName, "columnGap", group.columnGap, error) &&
           readNumber(slotJson, ownerName, "rowGap", group.rowGap, error) &&
           readNumber(slotJson, ownerName, "row4ExtraGap", group.row4ExtraGap, error);
}

bool parseProgress(const nlohmann::json& progressJson,
                   const std::string& containerId,
                   const int index,
                   ContainerProgressDef& progress,
                   std::string& error) {
    if (!progressJson.is_object()) {
        return fail(error, containerId + ".progressBars[" + std::to_string(index) + "] must be an object");
    }
    const std::string ownerName = containerId + ".progressBars[" + std::to_string(index) + "]";
    std::string kind;
    if (!readString(progressJson, ownerName, "id", progress.id, error) ||
        !readString(progressJson, ownerName, "kind", kind, error) ||
        !parseProgressKind(kind, ownerName, progress.kind, error) ||
        !readNumber(progressJson, ownerName, "x", progress.x, error) ||
        !readNumber(progressJson, ownerName, "y", progress.y, error) ||
        !readNumber(progressJson, ownerName, "width", progress.width, error) ||
        !readNumber(progressJson, ownerName, "height", progress.height, error)) {
        return false;
    }
    if (progress.width <= 0.0f || progress.height <= 0.0f) {
        return fail(error, ownerName + " requires positive width and height");
    }
    if (!readNumber(progressJson, ownerName, "textureX", progress.textureX, error) ||
        !readNumber(progressJson, ownerName, "textureY", progress.textureY, error) ||
        !readString(progressJson, ownerName, "direction", progress.direction, error)) {
        return false;
    }
    if (progress.direction != "up" && progress.direction != "right") {
        return fail(error, ownerName + " has unknown progress direction: " + progress.direction);
    }
    return true;
}

bool parseContainerUiDef(const nlohmann::json& root,
                         const std::string& sourceName,
                         ContainerUiDef& def,
                         std::string& error) {
    if (!root.is_object()) {
        return fail(error, "Container UI file must contain an object: " + sourceName);
    }

    def = ContainerUiDef{};
    if (!readNamespacedField(root, sourceName, "id", def.id, error) ||
        !readNamespacedField(root, def.id, "behavior", def.behavior, error) ||
        !readString(root, def.id, "backgroundTexture", def.backgroundTexture, error) ||
        !readString(root, def.id, "backgroundTexturePath", def.backgroundTexturePath, error) ||
        !readFiniteNumber(root, def.id, "width", def.width, error) ||
        !readFiniteNumber(root, def.id, "height", def.height, error)) {
        return false;
    }
    if (def.width <= 0.0f || def.height <= 0.0f) {
        return fail(error, def.id + " requires positive width and height");
    }
    if (!readFiniteNumber(root, def.id, "textureWidth", def.textureWidth, error) ||
        !readFiniteNumber(root, def.id, "textureHeight", def.textureHeight, error)) {
        return false;
    }
    if (def.textureWidth <= 0.0f || def.textureHeight <= 0.0f) {
        return fail(error, def.id + " requires positive textureWidth and textureHeight");
    }
    if (!readNormalizedNumber(root, def.id, "anchorX", def.anchorX, error) ||
        !readNormalizedNumber(root, def.id, "anchorY", def.anchorY, error) ||
        !readNormalizedNumber(root, def.id, "pivotX", def.pivotX, error) ||
        !readNormalizedNumber(root, def.id, "pivotY", def.pivotY, error) ||
        !readFiniteNumber(root, def.id, "offsetX", def.offsetX, error) ||
        !readFiniteNumber(root, def.id, "offsetY", def.offsetY, error) ||
        !readFiniteNumber(root, def.id, "scale", def.scale, error)) {
        return false;
    }
    if (def.scale <= 0.0f) {
        return fail(error, def.id + " requires positive scale");
    }
    if (!readFiniteNumber(root, def.id, "fitPadding", def.fitPadding, error)) {
        return false;
    }
    if (def.fitPadding < 0.0f) {
        return fail(error, def.id + " requires non-negative fitPadding");
    }
    if (!readBoolean(root, def.id, "showPlayerPreview", def.showPlayerPreview, error)) {
        return false;
    }

    const nlohmann::json* slotGroups = findRequiredField(root, def.id, "slotGroups", error);
    if (slotGroups == nullptr) {
        return false;
    }
    if (!slotGroups->is_array()) {
        return fail(error, def.id + " requires array field: slotGroups");
    }
    for (int i = 0; i < static_cast<int>(slotGroups->size()); ++i) {
        ContainerSlotGroupDef group;
        if (!parseSlotGroup((*slotGroups)[static_cast<std::size_t>(i)], def.id, i, group, error)) {
            return false;
        }
        def.slotGroups.push_back(std::move(group));
    }

    const nlohmann::json* progressBars = findRequiredField(root, def.id, "progressBars", error);
    if (progressBars == nullptr) {
        return false;
    }
    if (!progressBars->is_array()) {
        return fail(error, def.id + " requires array field: progressBars");
    }
    for (int i = 0; i < static_cast<int>(progressBars->size()); ++i) {
        ContainerProgressDef progress;
        if (!parseProgress((*progressBars)[static_cast<std::size_t>(i)], def.id, i, progress, error)) {
            return false;
        }
        def.progressBars.push_back(std::move(progress));
    }

    return true;
}

bool loadContainerUiFile(const std::filesystem::path& path,
                         std::unordered_map<std::string, ContainerUiDef>& defs,
                         std::string& error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return fail(error, "Failed to open container UI config: " + path.string());
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded()) {
        return fail(error, "Failed to parse container UI config " + path.string() + ": invalid JSON");
    }

    ContainerUiDef def;
    if (!parseContainerUiDef(root, path.string(), def, error)) {
        return false;
    }
    if (defs.find(def.id) != defs.end()) {
        return fail(error, "Duplicate container UI id: " + def.id);
    }
    defs.emplace(def.id, std::move(def));
    return true;
}
}

bool ContainerUiRegistry::init() {
    if (s_initialized) {
        return true;
    }

    const std::filesystem::path configDir(kContainerUiConfigDir);
    std::error_code fsError;
    if (!std::filesystem::exists(configDir, fsError) ||
        fsError ||
        !std::filesystem::is_directory(configDir, fsError) ||
        fsError) {
        std::cerr << "Container UI config directory is missing: " << configDir.string() << '\n';
        return false;
    }

    std::unordered_map<std::string, ContainerUiDef> defs;
    std::filesystem::directory_iterator it(configDir, fsError);
    if (fsError) {
        std::cerr << "Failed to enumerate container UI config directory: "
                  << configDir.string() << ": " << fsError.message() << '\n';
        return false;
    }
    const std::filesystem::directory_iterator end;
    for (; it != end; it.increment(fsError)) {
        if (fsError) {
            std::cerr << "Failed to advance container UI config iterator: "
                      << configDir.string() << ": " << fsError.message() << '\n';
            return false;
        }
        const std::filesystem::directory_entry& entry = *it;
        if (!entry.is_regular_file(fsError) || fsError || entry.path().extension() != ".json") {
            fsError.clear();
            continue;
        }
        std::string error;
        if (!loadContainerUiFile(entry.path(), defs, error)) {
            std::cerr << error << '\n';
            return false;
        }
    }

    if (defs.empty()) {
        std::cerr << "Container UI config directory contains no JSON definitions: " << configDir.string() << '\n';
        return false;
    }

    s_defs = std::move(defs);
    s_initialized = true;
    return true;
}

bool ContainerUiRegistry::ensureInitialized() {
    return s_initialized || init();
}

const ContainerUiDef& ContainerUiRegistry::require(const std::string& id) {
    if (!ensureInitialized()) {
        std::cerr << "Container UI registry failed to initialize\n";
        std::abort();
    }
    const NamespacedId namespacedId(id);
    const std::string canonicalId = namespacedId.full();
    const auto it = s_defs.find(canonicalId);
    if (it == s_defs.end()) {
        std::cerr << "Container UI is not registered: " << canonicalId << '\n';
        std::abort();
    }
    return it->second;
}

bool ContainerUiRegistry::tryGet(const std::string& id, const ContainerUiDef*& outDef) {
    if (!ensureInitialized()) {
        outDef = nullptr;
        return false;
    }
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
    if (!ensureInitialized()) {
        std::cerr << "Container UI registry failed to initialize\n";
        std::abort();
    }
    return s_defs;
}

} // namespace ui
