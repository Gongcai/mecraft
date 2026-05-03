#ifndef MECRAFT_ECS_ISYSTEM_H
#define MECRAFT_ECS_ISYSTEM_H

#include <memory>
#include <tuple>
#include "SystemContext.h"

namespace ecs {

/// System dependency declaration (for validation and documentation).
template<typename RequiredTuple, typename WrittenTuple>
struct SystemDependency {
    using Required = RequiredTuple;
    using Written = WrittenTuple;
};

/// Empty dependency for systems that don't declare them yet.
using NoSystemDependency = SystemDependency<std::tuple<>, std::tuple<>>;

/// Base interface for all ECS systems in the fixed-update and tick pipelines.
class ISystem {
public:
    using Dependencies = NoSystemDependency;

    virtual ~ISystem() = default;

    /// Called once per frame (fixed-update) or once per tick.
    virtual void update(SystemContext& ctx) = 0;
};

} // namespace ecs

#endif // MECRAFT_ECS_ISYSTEM_H
