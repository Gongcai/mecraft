#pragma once

#include <functional>
#include <string>
#include <vector>
#include <filesystem>

#include "../core/UIScene.h"
#include "../core/Tween.h"

class UIPanel;
class UIText;
class UIButton;
class UIScrollArea;
class UIStackLayout;

/// World-selection screen shown after clicking "Start Game".
/// Lists all saves in the saves/ directory with thumbnails and metadata.
/// The user can double-click a save to load it, or click "Create New World".
class SaveListScreen : public UIScene {
public:
    struct SaveEntry {
        std::string displayName;
        std::string folderName;
        std::string lastPlayedUtc;
        std::string createdUtc;
        std::string screenshotPath;   // full filesystem path to thumb.png
        uint32_t seed = 0;
    };

    /// Fired when the user double-clicks a save entry (passes folder name).
    std::function<void(const std::string& worldFolder)> onSaveSelected;

    /// Fired when the user clicks "Create New World".
    std::function<void()> onCreateNewClicked;

    /// Fired when the user clicks "Back".
    std::function<void()> onBackClicked;

    void setSavesRoot(const std::filesystem::path& root) { m_savesRoot = root; }

    /// Re-scan the saves directory and rebuild the list.
    void refreshSaveList();

    void updateAnimations(float dt) override;

protected:
    void buildUI(ResourceMgr& resourceMgr) override;
    void onSceneEnter() override;

private:
    void rebuildList();

    UIPanel* m_bgPanel = nullptr;
    UIText* m_title = nullptr;
    UIScrollArea* m_scrollArea = nullptr;
    UIStackLayout* m_saveListStack = nullptr;
    UIButton* m_createButton = nullptr;
    UIButton* m_backButton = nullptr;
    UIText* m_emptyText = nullptr;

    Tween<float> m_titleSlideY;

    ResourceMgr* m_resMgr = nullptr;
    std::filesystem::path m_savesRoot;
    std::vector<SaveEntry> m_saves;

    static constexpr float kEntryHeight = 80.0f;
    static constexpr float kThumbSize = 64.0f;
    static constexpr float kEntrySpacing = 6.0f;
    static constexpr float kTitleDropDuration = 0.5f;
};
