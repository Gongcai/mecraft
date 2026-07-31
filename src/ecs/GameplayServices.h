#ifndef MECRAFT_GAMEPLAY_SERVICES_H
#define MECRAFT_GAMEPLAY_SERVICES_H

#include <cassert>

// Forward declarations for non-ECS services
class World;
class IWorldView;
namespace client {
class GameClient;
}
class AudioEngine;
class InputContextManager;
class ResourceMgr;
class DropSystem;
class ParticleSystem;
class UIRenderer;
class CameraController;

namespace physics {
class PhysicsSystem;
}

namespace ecs {

/// Lightweight RAII wrapper for an optionally-bound service pointer.
/// Provides named access with a debug-mode null check.
template <typename T> class OptionalService {
public:
    OptionalService() = default;
    explicit OptionalService(T* ptr) : m_ptr(ptr) {}

    OptionalService& operator=(T* ptr) {
        m_ptr = ptr;
        return *this;
    }

    /// Returns the raw pointer (may be nullptr).
    [[nodiscard]] T* get() const { return m_ptr; }

    /// Dereference with assert — aborts in debug mode if nullptr.
    [[nodiscard]] T& require() const {
        assert(m_ptr != nullptr && "OptionalService::require() called on null service");
        return *m_ptr;
    }

    /// Implicit bool — true when the service has been bound.
    [[nodiscard]] explicit operator bool() const { return m_ptr != nullptr; }

    /// Arrow operator — debug-asserts non-null.
    T* operator->() const {
        assert(m_ptr != nullptr && "OptionalService::operator->() called on null service");
        return m_ptr;
    }

    /// Dereference operator — debug-asserts non-null.
    T& operator*() const {
        assert(m_ptr != nullptr && "OptionalService::operator*() called on null service");
        return *m_ptr;
    }

private:
    T* m_ptr = nullptr;
};

struct GameplayServices {
    // ── Service slots ──
    // Assign during init; access via the OptionalService API or raw get().

    OptionalService<World> world;
    OptionalService<const IWorldView> worldView;
    OptionalService<client::GameClient> gameClient;
    OptionalService<AudioEngine> audioEngine;
    OptionalService<InputContextManager> inputContextManager;
    OptionalService<ResourceMgr> resourceMgr;
    OptionalService<DropSystem> dropSystem;
    OptionalService<ParticleSystem> particleSystem;
    OptionalService<UIRenderer> uiRenderer;
    OptionalService<physics::PhysicsSystem> physicsSystem;
    OptionalService<CameraController> cameraController;
};

} // namespace ecs

#endif // MECRAFT_GAMEPLAY_SERVICES_H
