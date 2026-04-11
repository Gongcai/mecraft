#pragma once

#include <array>
#include <deque>
#include <functional>
#include <string>

class ConsoleDisplayBox
{
public:
    enum class MessageType {
        Normal,
        Warning,
        Success,
    };

    struct RenderParams {
        int screenW = 0;
        int screenH = 0;
        int x = 20;
        int inputY = 20;
        int inputBoxH = 34;
        int inputToFirstBoxGap = 12;
        int boxH = 34;
        int boxGap = 0;
        int horizontalMargin = 20;
        int minBoxW = 300;
        float boxWidthRatio = 0.68f;
        int textPadX = 10;
        int textPadY = 4;
        float textScale = 2.0f;
        float textAdvanceFactor = 0.86f;
        std::size_t visibleBoxes = 6;
        float holdSeconds = 5.0f;
        float fadeEndSeconds = 8.0f;
        std::array<float, 4> boxColor{0.0f, 0.0f, 0.0f, 0.55f};
        std::array<float, 4> normalTextColor{0.95f, 0.95f, 0.95f, 1.0f};
        std::array<float, 4> warningTextColor{0.95f, 0.35f, 0.35f, 1.0f};
        std::array<float, 4> successTextColor{0.45f, 0.90f, 0.50f, 1.0f};
    };

    using DrawRectFn = std::function<void(int, int, int, int, const std::array<float, 4>&)>;
    using RenderTextFn = std::function<void(const std::string&, float, float, float, const std::array<float, 4>&, float, float)>;

    void appendLine(const std::string& message,
                    double createdAtSec,
                    MessageType type = MessageType::Normal);
    void clear();
    void setMaxLines(std::size_t maxLines);
    [[nodiscard]] bool empty() const;

    void render(double nowSec,
                const RenderParams& params,
                const DrawRectFn& drawRect,
                const RenderTextFn& renderText);

private:
    struct Line {
        std::string text;
        double createdAtSec = 0.0;
        MessageType type = MessageType::Normal;
    };

    void pruneExpired(double nowSec, float fadeEndSeconds);
    static float computeLineAlpha(double ageSec, float holdSeconds, float fadeEndSeconds);

    std::deque<Line> m_lines;
    std::size_t m_maxLines = 64;
};

