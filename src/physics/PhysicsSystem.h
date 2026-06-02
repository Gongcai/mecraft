#ifndef MECRAFT_PHYSICS_SYSTEM_H
#define MECRAFT_PHYSICS_SYSTEM_H

#include "PhysicsInfo.h"

class IWorldView;
class World;

namespace physics {

class PhysicsSystem {
public:
    PhysicsSystem(const IWorldView* worldView);
    ~PhysicsSystem() = default;

    // 提供给外部调用的主更新接口
    void updateBody(PhysicsBody& body, const MoveIntent& intent, float dt);
    void updateBody(PhysicsBody& body, const MoveIntent& intent, float dt, const PhysicsTuning& tuningOverride);
    PhysicsTuning tuning;

private:
    const IWorldView* m_worldView;
    const World* m_concreteWorld = nullptr;
};

} // namespace physics

#endif // MECRAFT_PHYSICS_SYSTEM_H
