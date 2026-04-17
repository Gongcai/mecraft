#ifndef MECRAFT_GAMEPLAY_SERVICES_H
#define MECRAFT_GAMEPLAY_SERVICES_H

// Forward declarations for non-ECS services
class World;
class AudioEngine;
class InputContextManager;
class ResourceMgr;
class Player;
class DropSystem;
class ParticleSystem;
class UIRenderer;

namespace physics { class PhysicsSystem; }

namespace ecs {

struct GameplayServices {
    World*                     world               = nullptr;
    AudioEngine*               audioEngine         = nullptr;
    InputContextManager*       inputContextManager  = nullptr;
    ResourceMgr*               resourceMgr         = nullptr;
    Player*                    player              = nullptr;
    DropSystem*                dropSystem          = nullptr;
    ParticleSystem*            particleSystem      = nullptr;
    UIRenderer*                uiRenderer          = nullptr;
    physics::PhysicsSystem*    physicsSystem       = nullptr;
};

} // namespace ecs

#endif // MECRAFT_GAMEPLAY_SERVICES_H
