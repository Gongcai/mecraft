#ifndef MECRAFT_I_DEFERRED_GEOMETRY_PROVIDER_H
#define MECRAFT_I_DEFERRED_GEOMETRY_PROVIDER_H

#include "../rhi/RhiHandles.h"
#include "../contracts/ClusteredLightingContract.h"
#include "../contracts/GpuLightContract.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

class FrameContext;
class RhiCommandList;

/// Read-only deferred targets required by external transparent geometry.
struct DeferredTransparentResources {
    // Full-mip opaque HDR color used by rough reflection and transmission.
    RhiTextureViewHandle sceneColor;
    RhiTextureViewHandle opaqueDepth;
    RhiTextureViewHandle skyCapture;
    float reflectionCompositeStrength = 1.0f;
};

/// Persistent Forward+ resources shared with external transparent geometry.
struct DeferredClusteredLightingResources {
    RhiBindGroupLayoutHandle bindGroupLayout;
    RhiBindGroupHandle bindGroup;
    renderer::contracts::ClusterGrid grid;
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

    /// Builds the complete camera-relative light snapshot owned by this
    /// geometry provider before clustered-light resources are prepared.
    /// @param cameraPosition World-space camera position used as the floating origin.
    /// @param lights Destination replaced with a complete normalized snapshot.
    /// @param error Receives a precise asset, identity, or transform failure.
    /// @return True when every visible light was resolved without partial output.
    [[nodiscard]] virtual bool collectGpuLights(
        const glm::vec3& cameraPosition,
        std::vector<renderer::contracts::GpuLight>& lights,
        std::string& error) = 0;

    /// Publishes the current clustered-light descriptor set and grid before
    /// any frame commands are recorded.
    /// @param resources Valid Vulkan Forward+ resources for this frame.
    /// @return True when every owned transparent renderer accepted them.
    [[nodiscard]] virtual bool configureClusteredLighting(
        const DeferredClusteredLightingResources& resources) = 0;

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
