#include "LocaleManager.h"

#include <fstream>
#include <iostream>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "../Paths.h"

static std::string localePath(const char* langCode) {
    return std::string(LOCALE_DIR) + "/" + langCode + ".json";
}

bool LocaleManager::loadLanguage(const std::string& langCode) {
    const std::string path = localePath(langCode.c_str());
    std::ifstream file(path);
    if (!file.is_open()) {
#ifndef NDEBUG
        std::cerr << "[LocaleManager] Failed to open: " << path << std::endl;
#endif
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
#ifndef NDEBUG
        std::cerr << "[LocaleManager] Failed to parse: " << path << " - " << e.what() << std::endl;
#endif
        return false;
    }

    m_currentLang = langCode;
    m_uiStrings.clear();
    m_blockNames.clear();
    m_itemNames.clear();

    if (j.contains("ui") && j["ui"].is_object()) {
        for (auto& [key, val] : j["ui"].items()) {
            if (val.is_string()) m_uiStrings[key] = val.get<std::string>();
        }
    }
    if (j.contains("blocks") && j["blocks"].is_object()) {
        for (auto& [key, val] : j["blocks"].items()) {
            if (val.is_string()) m_blockNames[key] = val.get<std::string>();
        }
    }
    if (j.contains("items") && j["items"].is_object()) {
        for (auto& [key, val] : j["items"].items()) {
            if (val.is_string()) m_itemNames[key] = val.get<std::string>();
        }
    }

    return true;
}

void LocaleManager::loadSettings() {
    std::ifstream file(SETTINGS_PATH);
    if (!file.is_open()) {
        loadLanguage("en_us");
        return;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        loadLanguage("en_us");
        return;
    }

    std::string lang = "en_us";
    if (j.contains("language") && j["language"].is_string()) {
        lang = j["language"].get<std::string>();
    }
    if (!loadLanguage(lang)) {
        loadLanguage("en_us");
    }
}

void LocaleManager::saveSettings() const {
    nlohmann::json j;
    j["language"] = m_currentLang;
    std::ofstream file(SETTINGS_PATH);
    if (file.is_open()) {
        file << j.dump(2) << std::endl;
    }
}

void LocaleManager::setLanguage(const std::string& langCode) {
    loadLanguage(langCode);
}

const std::string& LocaleManager::getLanguage() const {
    return m_currentLang;
}

const std::string& LocaleManager::tr(std::string_view key) const {
    auto it = m_uiStrings.find(std::string(key));
    if (it != m_uiStrings.end()) return it->second;
    thread_local std::string fallback;
    fallback = std::string(key);
    return fallback;
}

std::string LocaleManager::getBlockName(std::string_view path) const {
    auto it = m_blockNames.find(std::string(path));
    if (it != m_blockNames.end()) return it->second;
    return displayNameFromPath(path);
}

std::string LocaleManager::getItemName(std::string_view path) const {
    auto it = m_itemNames.find(std::string(path));
    if (it != m_itemNames.end()) return it->second;
    auto blockIt = m_blockNames.find(std::string(path));
    if (blockIt != m_blockNames.end()) return blockIt->second;
    return displayNameFromPath(path);
}

std::string LocaleManager::displayNameFromPath(std::string_view path) {
    std::string result;
    result.reserve(path.size() + 4);
    bool capitalizeNext = true;
    for (char c : path) {
        if (c == '_') {
            result += ' ';
            capitalizeNext = true;
        } else if (capitalizeNext) {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            capitalizeNext = false;
        } else {
            result += c;
        }
    }
    return result;
}

std::vector<std::string> LocaleManager::getAvailableLanguages() {
    std::vector<std::string> langs;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(LOCALE_DIR, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            langs.push_back(entry.path().stem().string());
        }
    }
    return langs;
}

std::string LocaleManager::getLanguageDisplayName(const std::string& langCode) {
    const std::string path = localePath(langCode.c_str());
    std::ifstream file(path);
    if (!file.is_open()) return langCode;

    nlohmann::json j;
    try { file >> j; } catch (...) { return langCode; }

    if (j.contains("language_name") && j["language_name"].is_string()) {
        return j["language_name"].get<std::string>();
    }
    return langCode;
}
