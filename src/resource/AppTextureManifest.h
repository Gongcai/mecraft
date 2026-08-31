#ifndef MECRAFT_APP_TEXTURE_MANIFEST_H
#define MECRAFT_APP_TEXTURE_MANIFEST_H

#include <string>

class ResourceMgr;

namespace resource {

/// Loads application-level textures declared in a JSON manifest (app_textures.json).
///
/// The manifest declares two sections:
///   - "textures":  individual 2D textures (shader utility images, GUI sheets, entity skins).
///   - "cubemaps":  six-faced cubemaps keyed by a name (e.g. the menu skybox).
///
/// Each "textures" entry requires "name" and "path" (relative to ASSETS_DIR). Optional
/// fields: "kind" ("2d" default or "gui"), "srgb" (bool, default false), "flip"
/// (bool, default true for gui, false for 2d), and "queueSharing" ("exclusive" default
/// or "graphicsComputeConcurrent"). GUI textures route to loadGuiTexture; plain 2D textures
/// route to loadTexture2D.
///
/// @param resourceMgr   Destination resource manager that stores the loaded textures.
/// @param manifestPath  Absolute path to the manifest JSON file.
/// @return true when the manifest parses and every declared texture loads successfully.
///         Any parse error or failed load aborts startup (no silent fallback).
[[nodiscard]] bool loadAppTextureManifest(ResourceMgr& resourceMgr, const std::string& manifestPath);

} // namespace resource

#endif // MECRAFT_APP_TEXTURE_MANIFEST_H
