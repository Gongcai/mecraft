#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class LocaleManager {
public:
    bool loadLanguage(const std::string& langCode);
    void loadSettings();
    void saveSettings() const;

    void setLanguage(const std::string& langCode);
    [[nodiscard]] const std::string& getLanguage() const;

    [[nodiscard]] const std::string& tr(std::string_view key) const;
    [[nodiscard]] std::string getBlockName(std::string_view path) const;
    [[nodiscard]] std::string getItemName(std::string_view path) const;

    [[nodiscard]] static std::vector<std::string> getAvailableLanguages();
    [[nodiscard]] static std::string getLanguageDisplayName(const std::string& langCode);

private:
    static std::string displayNameFromPath(std::string_view path);

    std::string m_currentLang = "en_us";
    std::unordered_map<std::string, std::string> m_uiStrings;
    std::unordered_map<std::string, std::string> m_blockNames;
    std::unordered_map<std::string, std::string> m_itemNames;
};
