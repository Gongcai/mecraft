#ifndef MECRAFT_NETWORK_INTERPOLATION_SYSTEM_H
#define MECRAFT_NETWORK_INTERPOLATION_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/CameraComponents.h"
#include "../../components/DropComponents.h"
#include "../../components/NetworkComponents.h"
#include "../../components/SteveComponents.h"
#include "../../components/TransformComponents.h"

namespace ecs {

class NetworkInterpolationSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<NetworkInterpolationComponent, TransformComponent>,
        std::tuple<TransformComponent, SpinVisualComponent, MobAIComponent, CameraStateComponent>
    >;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_NETWORK_INTERPOLATION_SYSTEM_H
