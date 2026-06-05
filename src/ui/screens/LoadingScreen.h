#pragma once

#include "../core/UIScene.h"

#include <string>

class UIText;
class UIProgressBar;

class LoadingScreen : public UIScene {
public:
    void setProgress(float progress);
    void setStatusText(const std::string& text);
    void setDetailText(const std::string& text);

protected:
    void buildUI(ResourceMgr& resourceMgr) override;

private:
    UIText* m_title = nullptr;
    UIText* m_status = nullptr;
    UIText* m_detail = nullptr;
    UIProgressBar* m_progressBar = nullptr;
};
