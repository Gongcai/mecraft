#ifndef MECRAFT_I_DEFERRED_GEOMETRY_PROVIDER_H
#define MECRAFT_I_DEFERRED_GEOMETRY_PROVIDER_H

#include "../rhi/RhiHandles.h"

#include <glm/glm.hpp>

class FrameContext;
class RhiCommandList;

/// Read-only deferred targets required by external transparent geometry.
struct DeferredTransparentResources {
    RhiTextureViewHandle sceneColor;
    RhiTextureViewHandle opaqueDepth;
    RhiTextureViewHandle skyCapture;
    float reflectionCompositeStrength = 1.0f;
};

/// Supplies non-world geometry to the shared deferred rendering pipeline.
class IDeferredGeometryProvider {
public:
    virtual ~IDeferredGeometryProvider() = default;

    /// Uploads frame resources required before the G-buffer rendering scope begins.
    /// @param commandList Graphics command list recording the G-buffer pass.
    /// @param context Camera, timing, and environment state for the current frame.
    /// @return True when every geometry resource is ready for drawing.
    [[nodiscard]] virtual bool prepareGBuffer(
        RhiCommandList& commandList,
        const FrameContext& context) = 0;

    /// Draws opaque geometry into the active G-buffer rendering scope.
    /// @param commandList Command list with all deferred attachments bound.
    /// @param viewProjection Current raster view-projection matrix.
    /// @param previousViewProjection Previous view-projection used for velocity.
    virtual void renderToGBuffer(
        RhiCommandList& commandList,
        const glm::mat4& viewProjection,
        const glm::mat4& previousViewProjection) = 0;

    /// Draws opaque geometry into the active cascade shadow depth scope.
    /// @param commandList Command list with the selected cascade depth layer bound.
    /// @param shadowViewProjection Light-space view-projection for the cascade.
    virtual void renderToShadowMap(
        RhiCommandList& commandList,
        const glm::mat4& shadowViewProjection) = 0;

    /// Reports whether the provider owns alpha-blended forward geometry.
    /// @return True when at least one instantiated transparent primitive exists.
    [[nodiscard]] virtual bool hasTransparentGeometry() const = 0;

    /// Binds the resolved opaque scene inputs used by transparent shading.
    /// @param resources Scene color, depth, sky capture, and reflection strength.
    /// @return True when every transparent draw resource is ready.
    [[nodiscard]] virtual bool prepareTransparentResources(
        const DeferredTransparentResources& resources) = 0;

    /// Draws transparent geometry back-to-front into the active composite pass.
    /// @param commandList Command list with the transparent attachments active.
    /// @param cameraPosition World-space camera position used for draw sorting.
    /// @param reflectionCompositeStrength Global scene reflection contribution.
    virtual void renderTransparent(
        RhiCommandList& commandList,
        const glm::vec3& cameraPosition,
        float reflectionCompositeStrength) = 0;
};

#endif // MECRAFT_I_DEFERRED_GEOMETRY_PROVIDER_H
