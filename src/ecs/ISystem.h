#ifndef MECRAFT_ECS_ISYSTEM_H
#define MECRAFT_ECS_ISYSTEM_H

#include <functional>
#include <memory>
#include "SystemContext.h"

namespace ecs {

/// Base interface for all ECS systems in the fixed-update and tick pipelines.
class ISystem {
public:
    virtual ~ISystem() = default;

    /// Called once per frame (fixed-update) or once per tick.
    virtual void update(SystemContext& ctx) = 0;
};

/// Transitional adapter that wraps a legacy static-update call (or lambda)
/// into the ISystem interface.  Allows gradual migration without rewriting
/// every system at once.
class LegacySystemAdapter : public ISystem {
public:
    using UpdateFn = std::function<void(SystemContext&)>;

    explicit LegacySystemAdapter(UpdateFn fn) : m_fn(std::move(fn)) {}

    void update(SystemContext& ctx) override { m_fn(ctx); }

private:
    UpdateFn m_fn;
};

/// Helper factory for concise adapter construction.
template <typename Fn>
std::unique_ptr<ISystem> makeLegacySystem(Fn&& fn) {
    return std::make_unique<LegacySystemAdapter>(std::forward<Fn>(fn));
}

} // namespace ecs

#endif // MECRAFT_ECS_ISYSTEM_H
