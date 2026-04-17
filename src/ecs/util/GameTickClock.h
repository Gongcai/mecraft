#ifndef MECRAFT_GAME_TICK_CLOCK_H
#define MECRAFT_GAME_TICK_CLOCK_H

#include <cstdint>

namespace ecs {

class GameTickClock {
public:
    static constexpr double DEFAULT_TICK_RATE = 20.0;

    explicit GameTickClock(double tickRate = DEFAULT_TICK_RATE)
        : m_tickInterval(1.0 / tickRate) {}

    void advance(double deltaTime) {
        m_accumulator += deltaTime;
    }

    bool shouldTick() const {
        return m_accumulator >= m_tickInterval;
    }

    void consumeTick() {
        m_accumulator -= m_tickInterval;
        ++m_tickIndex;
    }

    uint64_t tickIndex() const { return m_tickIndex; }
    double tickInterval() const { return m_tickInterval; }
    double accumulator() const { return m_accumulator; }

    void setMaxTicksPerFrame(uint32_t max) { m_maxTicksPerFrame = max; }
    uint32_t maxTicksPerFrame() const { return m_maxTicksPerFrame; }

private:
    double   m_tickInterval     = 1.0 / DEFAULT_TICK_RATE;
    double   m_accumulator      = 0.0;
    uint64_t m_tickIndex        = 0;
    uint32_t m_maxTicksPerFrame = 4;
};

} // namespace ecs

#endif // MECRAFT_GAME_TICK_CLOCK_H
