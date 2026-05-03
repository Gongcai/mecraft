#ifndef MECRAFT_ECS_EVENT_BUS_H
#define MECRAFT_ECS_EVENT_BUS_H

#include <vector>

#include "../GameplayRegistry.h"

namespace ecs {

/// Frame-scoped event bus for one-producer-to-many-consumer patterns.
/// Producers push events during the frame; consumers drain at frame end.
template <typename T>
struct EventBus {
    std::vector<T> events;

    void push(const T& event) { events.push_back(event); }
    void push(T&& event) { events.push_back(std::move(event)); }

    [[nodiscard]] const std::vector<T>& peek() const { return events; }
    [[nodiscard]] std::vector<T> drain() {
        std::vector<T> result = std::move(events);
        events.clear();
        return result;
    }
    void clear() { events.clear(); }
    [[nodiscard]] bool empty() const { return events.empty(); }
    [[nodiscard]] size_t size() const { return events.size(); }
};

/// Helper: ensure an EventBus<T> exists in the registry context and return a reference.
template <typename T>
EventBus<T>& ensureEventBus(GameplayRegistry& registry) {
    if (!registry.ctxHas<EventBus<T>>()) {
        registry.ctxSet<EventBus<T>>();
    }
    return registry.ctxGet<EventBus<T>>();
}

} // namespace ecs

#endif // MECRAFT_ECS_EVENT_BUS_H
