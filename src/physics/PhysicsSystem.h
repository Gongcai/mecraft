#ifndef MECRAFT_PHYSICS_SYSTEM_H
#define MECRAFT_PHYSICS_SYSTEM_H

#include "PhysicsInfo.h"

class World;

namespace physics {

class PhysicsSystem {
public:
    PhysicsSystem(World* world);
    ~PhysicsSystem() = default;

    // 提供给外部调用的主更新接口
    void updateBody(PhysicsBody& body, const MoveIntent& intent, float dt);
    PhysicsTuning tuning;
private:
    World* m_world;
};

} // namespace physics

#endif // MECRAFT_PHYSICS_SYSTEM_H

