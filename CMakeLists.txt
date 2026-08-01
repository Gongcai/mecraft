cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE OR CMAKE_TOOLCHAIN_FILE STREQUAL "")
    if(NOT DEFINED MECRAFT_VCPKG_ROOT OR MECRAFT_VCPKG_ROOT STREQUAL "")
        if(CMAKE_HOST_WIN32)
            set(MECRAFT_VCPKG_ROOT "D:/vcpkg/vcpkg")
        else()
            set(MECRAFT_VCPKG_ROOT "$ENV{HOME}/.local/share/vcpkg")
        endif()
    endif()

    set(MECRAFT_VCPKG_ROOT "${MECRAFT_VCPKG_ROOT}" CACHE PATH "vcpkg root directory")
    set(MECRAFT_VCPKG_TOOLCHAIN "${MECRAFT_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    if(NOT EXISTS "${MECRAFT_VCPKG_TOOLCHAIN}")
        message(FATAL_ERROR "Missing vcpkg toolchain: ${MECRAFT_VCPKG_TOOLCHAIN}")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "${MECRAFT_VCPKG_TOOLCHAIN}" CACHE FILEPATH "vcpkg toolchain file")
endif()

if(WIN32)
    # Keep MSVC diagnostics stable so Ninja can filter /showIncludes output.
    set(ENV{VSLANG} "1033")
endif()

project(mecraft VERSION 0.1.0 LANGUAGES C CXX)

include(cmake/ClangFormat.cmake)

# ===========================================================================
# 全局设置
# ===========================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MECRAFT_ENABLE_IPO "Enable IPO/LTO for optimized configurations" ON)
option(MECRAFT_ENABLE_PCH "Enable precompiled headers" ON)
option(MECRAFT_BUILD_TESTS "Build Mecraft tests" ON)
option(MECRAFT_USE_SOURCE_ASSETS "Use the source-tree assets directory directly instead of packaged assets" OFF)
option(MECRAFT_RHI_BACKEND_OPENGL "Build OpenGL RHI backend" ON)
option(MECRAFT_RHI_BACKEND_VULKAN "Build Vulkan RHI backend" ON)
option(MECRAFT_ENABLE_FSR31 "Build AMD FidelityFX FSR 3.1.4 Vulkan upscaling" ON)
option(MECRAFT_ENABLE_STREAMLINE "Build NVIDIA Streamline 2.12.0 Vulkan integration" OFF)
set(MECRAFT_STREAMLINE_ROOT "" CACHE PATH "NVIDIA Streamline 2.12.0 SDK root")
set(MECRAFT_STREAMLINE_APPLICATION_ID "0" CACHE STRING "NVIDIA NGX application ID")
set(MECRAFT_DEFAULT_RHI_BACKEND "OpenGL" CACHE STRING "Default RHI backend")
set_property(CACHE MECRAFT_DEFAULT_RHI_BACKEND PROPERTY STRINGS OpenGL Vulkan)

if(NOT MECRAFT_RHI_BACKEND_OPENGL AND NOT MECRAFT_RHI_BACKEND_VULKAN)
    message(FATAL_ERROR "At least one RHI backend must be enabled")
endif()
if(MECRAFT_ENABLE_FSR31 AND NOT MECRAFT_RHI_BACKEND_VULKAN)
    message(FATAL_ERROR "MECRAFT_ENABLE_FSR31 requires the Vulkan RHI backend")
endif()
if(MECRAFT_ENABLE_STREAMLINE AND NOT WIN32)
    message(FATAL_ERROR "MECRAFT_ENABLE_STREAMLINE requires Windows")
endif()
if(MECRAFT_ENABLE_STREAMLINE AND NOT MECRAFT_RHI_BACKEND_VULKAN)
    message(FATAL_ERROR "MECRAFT_ENABLE_STREAMLINE requires the Vulkan RHI backend")
endif()
if(MECRAFT_ENABLE_STREAMLINE)
    if(MECRAFT_STREAMLINE_ROOT STREQUAL "")
        message(FATAL_ERROR "MECRAFT_STREAMLINE_ROOT must point to the Streamline 2.12.0 SDK")
    endif()
    get_filename_component(MECRAFT_STREAMLINE_ROOT
            "${MECRAFT_STREAMLINE_ROOT}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    if(NOT MECRAFT_STREAMLINE_APPLICATION_ID MATCHES "^[0-9]+$")
        message(FATAL_ERROR "MECRAFT_STREAMLINE_APPLICATION_ID must be an unsigned integer")
    endif()
    set(MECRAFT_STREAMLINE_INCLUDE_DIR "${MECRAFT_STREAMLINE_ROOT}/include")
    set(MECRAFT_STREAMLINE_BIN_DIR "${MECRAFT_STREAMLINE_ROOT}/bin/x64")
    set(MECRAFT_STREAMLINE_RUNTIME_FILES
            "${MECRAFT_STREAMLINE_BIN_DIR}/sl.interposer.dll"
            "${MECRAFT_STREAMLINE_BIN_DIR}/sl.common.dll"
            "${MECRAFT_STREAMLINE_BIN_DIR}/sl.dlss.dll"
            "${MECRAFT_STREAMLINE_BIN_DIR}/sl.dlss_g.dll"
            "${MECRAFT_STREAMLINE_BIN_DIR}/sl.reflex.dll"
            "${MECRAFT_STREAMLINE_BIN_DIR}/sl.pcl.dll"
            "${MECRAFT_STREAMLINE_BIN_DIR}/nvngx_dlss.dll"
            "${MECRAFT_STREAMLINE_BIN_DIR}/nvngx_dlssg.dll"
            "${MECRAFT_STREAMLINE_BIN_DIR}/NvLowLatencyVk.dll"
    )
    set(MECRAFT_STREAMLINE_REQUIRED_FILES
            "${MECRAFT_STREAMLINE_INCLUDE_DIR}/sl.h"
            "${MECRAFT_STREAMLINE_INCLUDE_DIR}/sl_version.h"
            ${MECRAFT_STREAMLINE_RUNTIME_FILES}
    )
    foreach(MECRAFT_STREAMLINE_FILE IN LISTS MECRAFT_STREAMLINE_REQUIRED_FILES)
        if(NOT EXISTS "${MECRAFT_STREAMLINE_FILE}")
            message(FATAL_ERROR "Missing Streamline 2.12.0 file: ${MECRAFT_STREAMLINE_FILE}")
        endif()
    endforeach()
    file(STRINGS "${MECRAFT_STREAMLINE_INCLUDE_DIR}/sl_version.h"
            MECRAFT_STREAMLINE_VERSION_DEFINES
            REGEX "^#define SL_VERSION_(MAJOR|MINOR|PATCH) ")
    string(JOIN ";" MECRAFT_STREAMLINE_VERSION_TEXT
            ${MECRAFT_STREAMLINE_VERSION_DEFINES})
    if(NOT MECRAFT_STREAMLINE_VERSION_TEXT MATCHES "SL_VERSION_MAJOR 2" OR
       NOT MECRAFT_STREAMLINE_VERSION_TEXT MATCHES "SL_VERSION_MINOR 12" OR
       NOT MECRAFT_STREAMLINE_VERSION_TEXT MATCHES "SL_VERSION_PATCH 0")
        message(FATAL_ERROR "MECRAFT_STREAMLINE_ROOT must contain Streamline 2.12.0")
    endif()
endif()
if(NOT MECRAFT_DEFAULT_RHI_BACKEND STREQUAL "OpenGL" AND
   NOT MECRAFT_DEFAULT_RHI_BACKEND STREQUAL "Vulkan")
    message(FATAL_ERROR
            "MECRAFT_DEFAULT_RHI_BACKEND must be OpenGL or Vulkan, got: ${MECRAFT_DEFAULT_RHI_BACKEND}")
endif()
if(MECRAFT_DEFAULT_RHI_BACKEND STREQUAL "OpenGL" AND NOT MECRAFT_RHI_BACKEND_OPENGL)
    message(FATAL_ERROR "OpenGL is the default RHI backend but MECRAFT_RHI_BACKEND_OPENGL is disabled")
endif()
if(MECRAFT_DEFAULT_RHI_BACKEND STREQUAL "Vulkan" AND NOT MECRAFT_RHI_BACKEND_VULKAN)
    message(FATAL_ERROR "Vulkan is the default RHI backend but MECRAFT_RHI_BACKEND_VULKAN is disabled")
endif()
if(MSVC)
    message(STATUS "Using MSVC")

    # 关闭警告 C4819: 无效的字符。避免中文注释被警告
    add_compile_options(
            "/wd4819"
    )

    # 关闭 /showIncludes
    string(REPLACE "/showIncludes" "" CMAKE_DEPFILE_FLAGS_C "${CMAKE_DEPFILE_FLAGS_C}")
    string(REPLACE "/showIncludes" "" CMAKE_DEPFILE_FLAGS_CXX "${CMAKE_DEPFILE_FLAGS_CXX}")
endif()
if(MECRAFT_USE_SOURCE_ASSETS)
    set(MECRAFT_RUNTIME_ASSETS_DIR "${PROJECT_SOURCE_DIR}/assets")
else()
    set(MECRAFT_RUNTIME_ASSETS_DIR "assets")
endif()
file(TO_CMAKE_PATH "${MECRAFT_RUNTIME_ASSETS_DIR}" MECRAFT_ASSETS_DIR_COMPILE)
file(GLOB_RECURSE MECRAFT_RUNTIME_ASSET_FILES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/assets/*")

# 检查链接期优化支持
include(CheckIPOSupported)
if(MECRAFT_ENABLE_IPO)
    check_ipo_supported(RESULT MECRAFT_IPO_SUPPORTED OUTPUT MECRAFT_IPO_ERROR)
else()
    set(MECRAFT_IPO_SUPPORTED FALSE)
endif()

if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E env VSLANG=1033")
endif()

function(mecraft_apply_ipo TARGET_NAME)
    if(MECRAFT_IPO_SUPPORTED)
        set_property(TARGET ${TARGET_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET ${TARGET_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
        set_property(TARGET ${TARGET_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE)
    endif()
endfunction()

function(mecraft_apply_msvc_defaults TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /utf-8 /FS)
    endif()
endfunction()

function(mecraft_apply_project_warnings TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4 /WX-)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -pedantic)
    endif()
endfunction()

function(mecraft_disable_rtti TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /GR-)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -fno-rtti)
    endif()
endfunction()

function(mecraft_disable_exceptions TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /EHs-c-)
        target_compile_definitions(${TARGET_NAME} PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -fno-exceptions)
    endif()
endfunction()

function(mecraft_copy_runtime_assets TARGET_NAME)
    if(NOT MECRAFT_USE_SOURCE_ASSETS)
        if(NOT TARGET mecraft_runtime_assets)
            set(ASSET_STAMP "${CMAKE_CURRENT_BINARY_DIR}/mecraft_runtime_assets.stamp")
            add_custom_command(
                    OUTPUT "${ASSET_STAMP}"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${PROJECT_SOURCE_DIR}/assets"
                            "${CMAKE_CURRENT_BINARY_DIR}/assets"
                    COMMAND ${CMAKE_COMMAND} -E touch "${ASSET_STAMP}"
                    DEPENDS ${MECRAFT_RUNTIME_ASSET_FILES}
                    COMMENT "Copying runtime assets"
                    VERBATIM
            )
            add_custom_target(mecraft_runtime_assets DEPENDS "${ASSET_STAMP}")
        endif()
        add_dependencies(${TARGET_NAME} mecraft_runtime_assets)
    endif()
endfunction()

# 查找依赖

find_package(glfw3 REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(Vorbis CONFIG REQUIRED COMPONENTS File)
find_package(Freetype REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)
find_package(spirv_cross_hlsl CONFIG REQUIRED)
find_package(spirv_cross_msl CONFIG REQUIRED)
find_package(spirv_cross_cpp CONFIG REQUIRED)
find_package(spirv_cross_reflect CONFIG REQUIRED)
find_package(spirv_cross_c CONFIG REQUIRED)


# [应用层]
set(SRC_APP
        src/app/AppLaunchOptions.cpp
        src/app/validation/ValidationSceneContract.cpp
        src/app/validation/ValidationVoxelFixture.cpp
        src/app/validation/ValidationRunController.cpp
        src/app/AppSettings.cpp
        src/app/GameResourceBootstrapper.cpp
        src/app/GameManager.cpp
        src/app/states/AppStateMachine.cpp
        src/app/states/GameplayAppState.cpp
        src/app/states/LoadingAppState.cpp
        src/app/states/MainMenuAppState.cpp
        src/app/states/ModelSceneAppState.cpp
)

set(SRC_SCENE
        src/scene/ModelSceneCommandHistory.cpp
        src/scene/ModelSceneDeferredRenderer.cpp
        src/scene/ModelSceneRuntime.cpp
        src/scene/ModelSceneSerializer.cpp
)

# [引擎层]
set(SRC_ENGINE
        src/engine/camera/Camera.cpp
        src/engine/input/InputManager.cpp
        src/engine/input/InputContextManager.cpp
        src/engine/platform/Window.cpp
        src/engine/platform/Time.cpp
        src/engine/registry/IdRegistry.cpp
        src/engine/registry/NamespacedId.cpp
)

# [游戏层]
set(SRC_GAME
        src/game/Game.cpp
        src/game/camera/CameraController.cpp
        src/game/states/GameStateMachine.cpp
        src/game/states/GameplayState.cpp
        src/game/states/GameplayStateEcsBridge.cpp
        src/game/states/SleepingState.cpp
        src/game/states/CommandState.cpp
        src/game/states/StateDependencies.cpp
        src/game/interaction/BlockInteractionDispatcher.cpp
        src/game/interaction/BlockInteractionRegistry.cpp
        src/game/redstone/RedstoneControlInteraction.cpp
        src/game/presentation/GameplayPresentationBuilder.cpp
        src/game/presentation/GameplayHudPresenter.cpp
        src/game/audio/AudioListenerSyncSystem.cpp
        src/game/inventory/ContainerBehaviorRegistry.cpp
        src/game/inventory/ContainerStateFactory.cpp
        src/game/inventory/BlockEntityInventoryLifecycle.cpp
        src/game/inventory/MachineInventoryLifecycle.cpp
        src/game/session/GameSession.cpp
        src/game/orchestrator/GameFrameOrchestrator.cpp
        src/game/debug/DebugFrameProfiler.cpp
        src/game/render/GameplayRenderRuntime.cpp
)

# [核心基础]
set(SRC_CORE
        src/thread/ThreadPool.cpp
        src/resource/AtmosphereLutProbe.cpp
        src/resource/BlockIconAtlasBuilder.cpp
        src/resource/BlockTextureCatalog.cpp
        src/resource/BlockTextureManifest.cpp
        src/resource/BlockTextureArrayBuilder.cpp
        src/resource/BlockTextureLibrary.cpp
        src/resource/CubemapLibrary.cpp
        src/resource/EnvironmentTextureLibrary.cpp
        src/resource/EntityTexturePreloader.cpp
        src/resource/ResourceMgr.cpp
        src/resource/TextureAtlas.cpp
        src/resource/TextureAtlasBuilders.cpp
        src/resource/TextureResampler.cpp
        src/resource/Texture2DLibrary.cpp
        src/resource/TextureSamplerController.cpp
        src/resource/UiTextureAtlasLibrary.cpp
        src/locale/LocaleManager.cpp
)

# [世界与地形]
set(SRC_WORLD
        src/world/block/Block.cpp
        src/world/block/BedBlock.cpp
        src/world/block/DoorBlock.cpp
        src/world/block/PistonBlock.cpp
        src/world/block/BlockCollision.cpp
        src/world/block/BlockModelRegistry.cpp
        src/world/block/BlockRandomTick.cpp
        src/world/block/BlockSelection.cpp
        src/world/block/BlockStateRegistry.cpp
        src/world/block/FarmlandRules.cpp
        src/world/chunk/Chunk.cpp
        src/world/chunk/SubChunk.cpp
        src/world/block/Palette.cpp
        src/world/chunk/BitPackedArray.cpp
        src/world/light/LightSolver.cpp
        src/world/light/LightService.cpp
        src/world/light/LightCache.cpp
        src/world/block/Placement.cpp
        src/world/block/PropIndices.cpp
        src/world/fluid/FluidRegistry.cpp
        src/world/fluid/FluidState.cpp
        src/world/fluid/FluidFlow.cpp
        src/world/fluid/FluidSystem.cpp
        src/world/block/BlockNeighborUpdateQueue.cpp
        src/world/redstone/WireContainerPlacement.cpp
        src/world/redstone/RedstoneUpdateQueue.cpp
        src/world/gen/TerrainGenerator.cpp
        src/world/WorldRaycast.cpp
        src/world/World.cpp
        src/world/DropSystem.cpp
        src/world/DayNightSystem.cpp
        src/world/WeatherSystem.cpp
)

# [渲染与UI]
set(SRC_RENDERER
        # Debug utilities
        src/renderer/debug/RenderDebugService.cpp

        # Contracts
        src/renderer/contracts/CameraPathContract.cpp
        src/renderer/contracts/ClusteredLightingContract.cpp
        src/renderer/contracts/ContentHashContract.cpp
        src/renderer/contracts/GpuLightContract.cpp
        src/renderer/contracts/LocalShadowContract.cpp
        src/renderer/contracts/GpuMaterialContract.cpp
        src/renderer/contracts/GpuSceneContract.cpp
        src/renderer/contracts/ReflectionProbeContract.cpp
        src/renderer/contracts/VoxelReflectionProbeSourceContract.cpp
        src/renderer/contracts/RenderFeatureContract.cpp

        # Lighting data producers
        src/renderer/lighting/VoxelLightRegistry.cpp

        # Capture
        src/renderer/capture/TextureCapture.cpp

        # RHI
        src/renderer/rhi/RhiGrowableBuffer.cpp
        src/renderer/rhi/RhiHash.cpp
        src/renderer/rhi/RhiRenderGraph.cpp
        src/renderer/rhi/RhiDeviceFactory.cpp
        src/renderer/rhi/RhiShaderCompiler.cpp
        src/renderer/rhi/RhiShaderSourceLoader.cpp

        # Core framework
        src/renderer/core/RenderResourceHub.cpp
        src/renderer/core/RenderScene.cpp
        src/renderer/core/DeferredPipeline.cpp
        src/renderer/core/ForwardPipeline.cpp
        src/renderer/core/BindlessDescriptorSlotAllocator.cpp

        # OpenGL helpers

        # Render targets
        src/renderer/targets/DeferredRenderTargets.cpp

        # Render passes
        src/renderer/passes/SsaoPass.cpp
        src/renderer/passes/SsgiPass.cpp
        src/renderer/passes/HiZPass.cpp
        src/renderer/passes/VelocityPass.cpp
        src/renderer/passes/ReflectionPass.cpp
        src/renderer/passes/ReflectionProbeCapturePass.cpp
        src/renderer/passes/ReflectionProbeGridPass.cpp
        src/renderer/passes/TemporalResolvePass.cpp
        src/renderer/passes/TemporalUpscalePass.cpp
        src/renderer/passes/MotionBlurPass.cpp
        src/renderer/passes/DepthOfFieldPass.cpp
        src/renderer/passes/LocalShadowPass.cpp
        src/renderer/passes/ClusteredLightingPass.cpp
        src/renderer/passes/DeferredLightingPass.cpp
        src/renderer/passes/CloudPass.cpp
        src/renderer/passes/SceneCompositePass.cpp
        src/renderer/passes/VolumetricPass.cpp
        src/renderer/passes/SkyCapturePass.cpp
        src/renderer/passes/SkyIblPass.cpp
        src/renderer/passes/GBufferPass.cpp
        src/renderer/passes/ShadowPass.cpp
        src/renderer/passes/WaterCompositePass.cpp
        src/renderer/passes/DebugPass.cpp
        src/renderer/passes/PostProcessPass.cpp
        src/renderer/passes/Fsr1Pass.cpp

        # Presentation
        src/renderer/presentation/PresentationController.cpp

        # Sub-renderers
        src/renderer/renderers/BlockEntityRenderer.cpp
        src/renderer/renderers/DropRenderer.cpp
        src/renderer/renderers/FallingBlockRenderer.cpp
        src/renderer/renderers/FirstPersonHeldItemRenderer.cpp
        src/renderer/renderers/GameplaySkyRenderer.cpp
        src/renderer/renderers/HumanoidRenderer.cpp
        src/renderer/renderers/HumanoidSkinLayoutCatalog.cpp
        src/renderer/renderers/GltfPunctualLightLoader.cpp
        src/renderer/renderers/SkyboxRenderer.cpp
        src/renderer/renderers/StaticMeshRenderer.cpp

        # Mesh system
        src/renderer/mesh/ChunkMesher.cpp
        src/renderer/mesh/ChunkMeshingService.cpp
        src/renderer/mesh/BlockMeshBuilder.cpp
        src/renderer/mesh/ItemModelMesh.cpp
        src/renderer/mesh/MeshBuilderRegistry.cpp
        src/renderer/mesh/WorldRenderBuffer.cpp
        src/renderer/mesh/TerrainRenderCache.cpp
        src/renderer/mesh/TerrainStreamingService.cpp
        src/renderer/mesh/TerrainRenderer.cpp
        src/renderer/mesh/TerrainRhiPipelineSet.cpp

        # Shadow system
        src/renderer/shadow/ShadowMatrices.cpp
        src/renderer/shadow/ShadowCasterCuller.cpp
        src/renderer/shadow/ShadowRenderer.cpp

        # Overlays
        src/renderer/overlays/BlockInteractionOverlayRenderer.cpp

        # Utility
        src/renderer/stb_image.cpp
        src/renderer/stb_image_write.cpp
)

if(MECRAFT_RHI_BACKEND_OPENGL)
    list(APPEND SRC_RENDERER
            src/renderer/rhi/gl/GlRhiDevice.cpp
            src/renderer/rhi/gl/GlRhiShaderCompiler.cpp
    )
endif()

if(MECRAFT_RHI_BACKEND_VULKAN)
    find_package(Vulkan REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    if(TARGET GPUOpen::VulkanMemoryAllocator)
        set(MECRAFT_VMA_TARGET GPUOpen::VulkanMemoryAllocator)
    elseif(TARGET VulkanMemoryAllocator::VulkanMemoryAllocator)
        set(MECRAFT_VMA_TARGET VulkanMemoryAllocator::VulkanMemoryAllocator)
    else()
        message(FATAL_ERROR "VulkanMemoryAllocator package does not provide a supported CMake target")
    endif()

    list(APPEND SRC_RENDERER
            src/renderer/rhi/vulkan/VkRhiCommandList.cpp
            src/renderer/rhi/vulkan/VkRhiCommandList.h
            src/renderer/rhi/vulkan/VkRhiConversions.h
            src/renderer/rhi/vulkan/VkRhiDevice.cpp
            src/renderer/rhi/vulkan/VkRhiDevice.h
            src/renderer/rhi/vulkan/VkRhiInterop.cpp
            src/renderer/rhi/vulkan/VkRhiInterop.h
            src/renderer/rhi/vulkan/VulkanRequirementCollector.cpp
            src/renderer/rhi/vulkan/VulkanRequirementCollector.h
            src/renderer/upscaling/Fsr31VulkanResourceContract.cpp
            src/renderer/upscaling/Fsr31VulkanResourceContract.h
    )
    if(MECRAFT_ENABLE_FSR31)
        list(APPEND SRC_RENDERER
                src/renderer/upscaling/Fsr31TemporalConfig.cpp
                src/renderer/upscaling/Fsr31TemporalConfig.h
                src/renderer/upscaling/Fsr31VulkanContext.cpp
                src/renderer/upscaling/Fsr31VulkanContext.h
        )
    endif()
    if(MECRAFT_ENABLE_STREAMLINE)
        list(APPEND SRC_RENDERER
                src/renderer/upscaling/DlssFrameGeneration.cpp
                src/renderer/upscaling/DlssFrameGeneration.h
                src/renderer/upscaling/DlssVulkanContext.cpp
                src/renderer/upscaling/DlssVulkanContext.h
                src/renderer/upscaling/StreamlineRuntime.cpp
                src/renderer/upscaling/StreamlineRuntime.h
        )
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set_source_files_properties(
                src/renderer/rhi/vulkan/VkRhiDevice.cpp
                PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers"
        )
    endif()
endif()

set(SRC_UI
        # Core UI Framework
        src/ui/Dashboard.cpp
        src/ui/imgui/ImGuiRhiRenderer.cpp
        src/ui/imgui/RenderSettingsImGui.cpp
        src/ui/core/UIInputAdapter.cpp
        src/ui/core/UIRenderer.cpp
        src/ui/core/UIScene.cpp
        src/ui/core/ScreenTransition.cpp
        src/ui/core/UIStyle.cpp
        src/ui/core/UITheme.cpp
        src/ui/core/UIThemePresets.cpp

        # Font & Text
        src/ui/font/TextRenderer.cpp
        src/ui/font/GlyphAtlas.cpp

        # Layouts
        src/ui/layout/UILayout.cpp
        src/ui/layout/UIBoxLayout.cpp
        src/ui/layout/UIGridLayout.cpp

        # Common Widgets
        src/ui/widgets/UIPanel.cpp
        src/ui/widgets/UIImage.cpp
        src/ui/widgets/UIText.cpp
        src/ui/widgets/UIButton.cpp
        src/ui/widgets/UICheckbox.cpp
        src/ui/widgets/UISlider.cpp
        src/ui/widgets/UIDropdown.cpp
        src/ui/widgets/UIScrollArea.cpp
        src/ui/widgets/KeyboardInputBox.cpp
        src/ui/widgets/ConsoleDisplayBox.cpp
        src/ui/widgets/UIProgressBar.cpp
        src/ui/widgets/UIToggle.cpp
        src/ui/widgets/UIRadioButton.cpp
        src/ui/widgets/UITextInput.cpp
        src/ui/widgets/UINumericSpinner.cpp
        src/ui/widgets/UITabControl.cpp
        src/ui/widgets/UIModal.cpp
        src/ui/widgets/UIContextMenu.cpp
        src/ui/widgets/UIToast.cpp

        # HUD & Overlays
        src/ui/hud/CrosshairControl.cpp
        src/ui/hud/HotbarControl.cpp
        src/ui/hud/HudControl.cpp
        src/ui/hud/Pickable.cpp
        src/ui/hud/ConsoleOverlay.cpp
        src/ui/hud/CommandInputOverlay.cpp

        # Inventory UI
        src/ui/inventory/ItemGridControl.cpp
        src/ui/inventory/CraftingGridControl.cpp
        src/ui/inventory/ContainerUiRegistry.cpp
        src/ui/inventory/DataDrivenContainerPanelControl.cpp
        src/ui/inventory/InventoryPanelControl.cpp
        src/ui/inventory/CreativeInventoryPanelControl.cpp

        # Screens
        src/ui/screens/MainMenuScreen.cpp
        src/ui/screens/PauseMenuScreen.cpp
        src/ui/screens/SettingsScreen.cpp
        src/ui/screens/SaveListScreen.cpp
        src/ui/screens/CreateWorldScreen.cpp
        src/ui/screens/LoadingScreen.cpp
)

# [实体与游戏系统]
set(SRC_ECS
        src/ecs/GameplayPipeline.cpp
        src/ecs/GameplayScene.cpp
        src/ecs/util/PlayerQuery.cpp
        src/ecs/util/ProjectileDefinitions.cpp
        src/ecs/entity/EntityDefinitionRegistry.cpp
        src/ecs/entity/EntityModelRegistry.cpp
        src/ecs/entity/EntityModelFactory.cpp
        src/ecs/entity/EntityFactory.cpp
        src/ecs/entity/SteveModelFactory.cpp
        src/ecs/entity/MobModelFactory.cpp
        src/ecs/systems/player/InputSamplingSystem.cpp
        src/ecs/systems/player/PlayerIntentBuildSystem.cpp
        src/ecs/systems/player/CharacterPhysicsSystem.cpp
        src/ecs/systems/player/PlayerRuntimeUpdateSystem.cpp
        src/ecs/systems/player/ViewBobSystem.cpp
        src/ecs/systems/player/FallDamageSystem.cpp
        src/ecs/systems/player/FallRollEffectSystem.cpp
        src/ecs/systems/player/HungerDepletionSystem.cpp
        src/ecs/systems/combat/DamageSystem.cpp
        src/ecs/systems/combat/DeathSystem.cpp
        src/ecs/systems/combat/HurtEffectDecaySystem.cpp
        src/ecs/systems/combat/PlayerMeleeSystem.cpp
        src/ecs/systems/combat/ProjectileSystem.cpp
        src/ecs/systems/interaction/BlockTargetSystem.cpp
        src/ecs/systems/interaction/BlockBreakSystem.cpp
        src/ecs/systems/interaction/SoilTillingSystem.cpp
        src/ecs/systems/interaction/BucketUseSystem.cpp
        src/ecs/systems/interaction/BlockPlaceSystem.cpp
        src/ecs/systems/item/ItemSpawnSystem.cpp
        src/ecs/systems/item/ItemPhysicsSystem.cpp
        src/ecs/systems/item/ItemMergeSystem.cpp
        src/ecs/systems/item/ItemPickupSystem.cpp
        src/ecs/systems/item/ItemLifetimeSystem.cpp
        src/ecs/systems/item/ItemPlacementResolveSystem.cpp
        src/ecs/systems/particle/ParticleSpawnSystem.cpp
        src/ecs/systems/particle/ParticleSimulationSystem.cpp
        src/ecs/systems/particle/ParticleCleanupSystem.cpp
        src/ecs/systems/audio/AudioSyncSystem.cpp
        src/ecs/systems/audio/PlayerFootstepAudioSystem.cpp
        src/ecs/systems/world/FluidTickSystem.cpp
        src/ecs/systems/world/FarmlandMoistureSystem.cpp
        src/ecs/systems/world/RandomTickSystem.cpp
        src/ecs/systems/world/BlockSupportSystem.cpp
        src/ecs/systems/world/PressurePlateSystem.cpp
        src/ecs/systems/world/RedstoneSystem.cpp
        src/ecs/systems/world/RedstoneDeviceActionSystem.cpp
        src/ecs/systems/world/HopperSystem.cpp
        src/ecs/systems/world/MovingBlockSystem.cpp
        src/ecs/systems/world/FallingBlockSpawnSystem.cpp
        src/ecs/systems/world/FallingBlockTickSystem.cpp
        src/ecs/systems/world/FallingBlockInterpolateSystem.cpp
        src/ecs/systems/steve/SteveAnimationSystem.cpp
        src/ecs/systems/steve/SteveSyncSystem.cpp
        src/ecs/systems/steve/TransformHierarchySystem.cpp
        src/ecs/systems/mob/MobAISystem.cpp
        src/ecs/systems/mob/MobAnimationSystem.cpp
        src/ecs/systems/network/NetworkInterpolationSystem.cpp
)

# [其他子系统]
set(SRC_PLAYER
        src/player/ActionMap.cpp
        src/player/Inventory.cpp
)
set(SRC_PHYSICS src/physics/PhysicsSystem.cpp)
set(SRC_PARTICLE src/particle/ParticleSystem.cpp src/particle/RainRenderer.cpp)
set(SRC_ITEM src/item/Item.cpp src/item/ItemUseDispatcher.cpp)
set(SRC_CRAFTING src/crafting/CraftingSystem.cpp src/crafting/SmeltingSystem.cpp)
set(SRC_AUDIO
        src/audio/AudioCatalog.cpp
        src/audio/AudioEngine.cpp
        src/audio/BgmSystem.cpp
        src/audio/AudioClip.cpp
        src/audio/AudioDecoders.cpp
        src/audio/AudioFileDiscovery.cpp
        src/audio/AudioSource.cpp
        src/audio/AudioListener.cpp
)

# [网络层]
set(SRC_NET
        src/net/InProcessTransport.cpp
        src/net/ENetTransport.cpp
)

# [服务端]
set(SRC_SERVER
        src/server/GameServer.cpp
        src/server/ChunkTicketManager.cpp
)

# [客户端]
set(SRC_CLIENT
        src/client/ClientWorld.cpp
        src/client/GameClient.cpp
        src/client/ClientEntityStore.cpp
)

# [存档系统]
set(SRC_SAVE
        src/save/SavePaths.cpp
        src/save/ChunkSerializer.cpp
        src/save/SaveManager.cpp
        src/save/PlayerSerializer.cpp
        src/save/RegionFile.cpp
)

# 合并所有核心代码
set(CORE_SOURCES
        ${SRC_APP} ${SRC_ENGINE} ${SRC_GAME} ${SRC_CORE}
        ${SRC_WORLD} ${SRC_RENDERER} ${SRC_UI} ${SRC_SCENE}
        ${SRC_ECS} ${SRC_PLAYER} ${SRC_PHYSICS} ${SRC_PARTICLE}
        ${SRC_ITEM} ${SRC_CRAFTING} ${SRC_AUDIO}
        ${SRC_NET} ${SRC_SERVER} ${SRC_CLIENT}
        ${SRC_SAVE}
)

# ===========================================================================
# 3. 预编译头
# ===========================================================================
if(MECRAFT_ENABLE_PCH)
    add_library(mecraft_pch OBJECT src/MecraftPch.cpp)
    target_compile_features(mecraft_pch PUBLIC cxx_std_17)
    target_include_directories(mecraft_pch PUBLIC src)
    target_link_libraries(mecraft_pch PUBLIC
            glm::glm
            nlohmann_json::nlohmann_json
            EnTT::EnTT
    )
    mecraft_apply_project_warnings(mecraft_pch)
    mecraft_apply_msvc_defaults(mecraft_pch)
    mecraft_disable_rtti(mecraft_pch)
    mecraft_disable_exceptions(mecraft_pch)
    mecraft_apply_ipo(mecraft_pch)
    target_precompile_headers(mecraft_pch PRIVATE
            <algorithm>
            <array>
            <chrono>
            <cstdint>
            <memory>
            <optional>
            <string>
            <unordered_map>
            <vector>
            <entt/entt.hpp>
            <glm/glm.hpp>
            <nlohmann/json.hpp>
    )
endif()

# ===========================================================================
# 4. 第三方源码库
# ===========================================================================
add_library(mecraft_third_party STATIC
        third_party/cgltf/cgltf_impl.c
        third_party/enet/enet_impl.cpp
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_demo.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_impl_glfw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imguizmo/ImGuizmo.cpp
        third_party/mikktspace/mikktspace.c
)
target_compile_features(mecraft_third_party PUBLIC cxx_std_17)
set_target_properties(mecraft_third_party PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(mecraft_third_party SYSTEM PUBLIC
        third_party
        third_party/imgui
        third_party/imguizmo
)
target_link_libraries(mecraft_third_party PUBLIC glfw)
target_compile_definitions(mecraft_third_party PUBLIC
        GLFW_INCLUDE_NONE
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:MECRAFT_DEBUG>
        $<$<CONFIG:RelWithDebInfo>:MECRAFT_ENABLE_CONSOLE_OUTPUT>
)
if (MSVC)
    target_compile_options(mecraft_third_party PRIVATE /W0)
else()
    target_compile_options(mecraft_third_party PRIVATE -w)
endif()
mecraft_apply_msvc_defaults(mecraft_third_party)

if(MECRAFT_ENABLE_FSR31)
    set(MECRAFT_FSR31_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fidelityfx_fsr31")
    add_library(mecraft_fidelityfx_fsr31 STATIC
            ${MECRAFT_FSR31_ROOT}/sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
            ${MECRAFT_FSR31_ROOT}/sdk/src/backends/vk/ffx_vk.cpp
            ${MECRAFT_FSR31_ROOT}/sdk/src/backends/shared/ffx_shader_blobs.cpp
            ${MECRAFT_FSR31_ROOT}/sdk/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp
            ${MECRAFT_FSR31_ROOT}/sdk/src/shared/ffx_assert.cpp
            ${MECRAFT_FSR31_ROOT}/sdk/src/shared/ffx_breadcrumbs_list.cpp
            ${MECRAFT_FSR31_ROOT}/sdk/src/shared/ffx_message.cpp
            ${MECRAFT_FSR31_ROOT}/sdk/src/shared/ffx_object_management.cpp
    )
    target_compile_features(mecraft_fidelityfx_fsr31 PUBLIC cxx_std_17)
    target_compile_definitions(mecraft_fidelityfx_fsr31 PRIVATE FFX_FSR3UPSCALER)
    target_include_directories(mecraft_fidelityfx_fsr31 SYSTEM PUBLIC
            ${MECRAFT_FSR31_ROOT}/sdk/include
    )
    target_include_directories(mecraft_fidelityfx_fsr31 SYSTEM PRIVATE
            ${MECRAFT_FSR31_ROOT}/sdk/src/shared
            ${MECRAFT_FSR31_ROOT}/sdk/src/components
            ${MECRAFT_FSR31_ROOT}/sdk/src/backends/shared
            ${MECRAFT_FSR31_ROOT}/generated/vk
    )
    target_link_libraries(mecraft_fidelityfx_fsr31 PUBLIC Vulkan::Vulkan)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(mecraft_fidelityfx_fsr31 PRIVATE
                -w
                -include ${MECRAFT_FSR31_ROOT}/sdk/ffx_linux_compat.h
        )
    elseif(MSVC)
        target_compile_options(mecraft_fidelityfx_fsr31 PRIVATE /W0)
    endif()
endif()

# ===========================================================================
# 5. 构建核心库
# ===========================================================================
if(MECRAFT_RHI_BACKEND_OPENGL)
    add_library(mecraft_rhi_gl_loader OBJECT third_party/glad/glad.c)
    set_target_properties(mecraft_rhi_gl_loader PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(mecraft_rhi_gl_loader SYSTEM PRIVATE third_party)
    if(MSVC)
        target_compile_options(mecraft_rhi_gl_loader PRIVATE /W0)
    else()
        target_compile_options(mecraft_rhi_gl_loader PRIVATE -w)
    endif()
    mecraft_apply_msvc_defaults(mecraft_rhi_gl_loader)
endif()

add_library(mecraft_core ${CORE_SOURCES})
target_compile_features(mecraft_core PUBLIC cxx_std_17)
if(MECRAFT_RHI_BACKEND_OPENGL)
    target_sources(mecraft_core PRIVATE $<TARGET_OBJECTS:mecraft_rhi_gl_loader>)
endif()
set_target_properties(mecraft_core PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
)
target_compile_definitions(mecraft_core PRIVATE MECRAFT_CORE_BUILD)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(mecraft_core PUBLIC MECRAFT_SHARED)
endif()



# 设置包含路径：注意这里将 third_party 标记为 SYSTEM，编译器将不再报告里面的警告
target_include_directories(mecraft_core PUBLIC src)
target_include_directories(mecraft_core SYSTEM PUBLIC third_party)

# 统一链接依赖
target_link_libraries(mecraft_core PUBLIC
        mecraft_third_party
        glm::glm
        nlohmann_json::nlohmann_json
        OpenAL::OpenAL
        Vorbis::vorbisfile
        Freetype::Freetype
        EnTT::EnTT
        nfd::nfd
        glslang::glslang
        glslang::glslang-default-resource-limits
        glslang::SPIRV
        spirv-cross-c
)

# RHI backend selection is an implementation detail of the device factory.
target_compile_definitions(mecraft_core PRIVATE
        $<$<STREQUAL:${MECRAFT_DEFAULT_RHI_BACKEND},OpenGL>:MECRAFT_DEFAULT_RHI_OPENGL>
        $<$<STREQUAL:${MECRAFT_DEFAULT_RHI_BACKEND},Vulkan>:MECRAFT_DEFAULT_RHI_VULKAN>
        $<$<BOOL:${MECRAFT_RHI_BACKEND_OPENGL}>:MECRAFT_RHI_BACKEND_OPENGL>
        $<$<BOOL:${MECRAFT_RHI_BACKEND_VULKAN}>:MECRAFT_RHI_BACKEND_VULKAN>
)

# Shared compile definitions required by public project headers and consumers.
target_compile_definitions(mecraft_core PUBLIC
        GLFW_INCLUDE_NONE
        "MECRAFT_ASSETS_DIR=\"${MECRAFT_ASSETS_DIR_COMPILE}\""
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:MECRAFT_DEBUG>
        $<$<CONFIG:RelWithDebInfo>:MECRAFT_ENABLE_CONSOLE_OUTPUT>
)

if(MECRAFT_RHI_BACKEND_VULKAN)
    target_link_libraries(mecraft_core PRIVATE Vulkan::Vulkan ${MECRAFT_VMA_TARGET})
endif()
if(MECRAFT_ENABLE_FSR31)
    target_link_libraries(mecraft_core PRIVATE mecraft_fidelityfx_fsr31)
    target_compile_definitions(mecraft_core PUBLIC MECRAFT_ENABLE_FSR31)
endif()
if(MECRAFT_ENABLE_STREAMLINE)
    target_include_directories(mecraft_core SYSTEM PRIVATE
            "${MECRAFT_STREAMLINE_INCLUDE_DIR}")
    target_compile_definitions(mecraft_core PUBLIC
            MECRAFT_ENABLE_STREAMLINE
            MECRAFT_STREAMLINE_APPLICATION_ID=${MECRAFT_STREAMLINE_APPLICATION_ID})
endif()

# 基于 Target 的编译器选项（更严谨的代码质量控制）
mecraft_apply_project_warnings(mecraft_core)
mecraft_apply_msvc_defaults(mecraft_core)
mecraft_disable_rtti(mecraft_core)
mecraft_disable_exceptions(mecraft_core)

if(MECRAFT_ENABLE_PCH)
    target_precompile_headers(mecraft_core REUSE_FROM mecraft_pch)
endif()

mecraft_apply_ipo(mecraft_core)

# ===========================================================================
# 6. 主程序入口
# ===========================================================================
add_executable(mecraft main.cpp)
target_compile_features(mecraft PRIVATE cxx_std_17)
target_link_libraries(mecraft PRIVATE mecraft_core)
mecraft_copy_runtime_assets(mecraft)
if (MSVC)
    mecraft_apply_msvc_defaults(mecraft)
endif()
mecraft_disable_rtti(mecraft)
mecraft_disable_exceptions(mecraft)
mecraft_apply_ipo(mecraft)

if(MECRAFT_ENABLE_STREAMLINE)
    function(mecraft_deploy_streamline TARGET_NAME)
        foreach(MECRAFT_STREAMLINE_FILE IN LISTS MECRAFT_STREAMLINE_RUNTIME_FILES)
            get_filename_component(MECRAFT_STREAMLINE_FILE_NAME
                    "${MECRAFT_STREAMLINE_FILE}" NAME)
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${MECRAFT_STREAMLINE_FILE}"
                            "$<TARGET_FILE_DIR:${TARGET_NAME}>/${MECRAFT_STREAMLINE_FILE_NAME}"
                    VERBATIM)
        endforeach()
    endfunction()
    mecraft_deploy_streamline(mecraft)
endif()

# ===========================================================================
# 6b. Dedicated Server
# ===========================================================================
add_executable(mecraft_server dedicated_server.cpp)
target_compile_features(mecraft_server PRIVATE cxx_std_17)
target_link_libraries(mecraft_server PRIVATE mecraft_core)
target_compile_definitions(mecraft_server PRIVATE MECRAFT_NO_TEXTURES)
mecraft_copy_runtime_assets(mecraft_server)
if (MSVC)
    mecraft_apply_msvc_defaults(mecraft_server)
endif()
mecraft_disable_rtti(mecraft_server)
mecraft_disable_exceptions(mecraft_server)
if(MECRAFT_ENABLE_PCH)
    target_precompile_headers(mecraft_server REUSE_FROM mecraft_pch)
endif()
mecraft_apply_ipo(mecraft_server)

# ===========================================================================
# 7. 测试用例
# ===========================================================================
if(MECRAFT_BUILD_TESTS)
    enable_testing()

    # 辅助函数：减少重复代码
    function(add_mecraft_test TEST_NAME SOURCE_FILE)
        add_executable(${TEST_NAME} ${SOURCE_FILE})
        target_compile_features(${TEST_NAME} PRIVATE cxx_std_17)
        target_link_libraries(${TEST_NAME} PRIVATE mecraft_core)
        mecraft_apply_project_warnings(${TEST_NAME})
        mecraft_apply_msvc_defaults(${TEST_NAME})
        mecraft_disable_rtti(${TEST_NAME})
        mecraft_disable_exceptions(${TEST_NAME})
        if(MECRAFT_ENABLE_PCH)
            target_precompile_headers(${TEST_NAME} REUSE_FROM mecraft_pch)
        endif()
        mecraft_apply_ipo(${TEST_NAME})
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
        set_tests_properties(${TEST_NAME} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endfunction()

    # 物理测试 (需要特殊宏定义)
    add_executable(physics_mvp_test tests/physics_mvp_test.cpp)
    target_compile_features(physics_mvp_test PRIVATE cxx_std_17)
    target_link_libraries(physics_mvp_test PRIVATE mecraft_core)
    target_compile_definitions(physics_mvp_test PRIVATE MECRAFT_NO_TEXTURES)
    mecraft_apply_project_warnings(physics_mvp_test)
    mecraft_apply_msvc_defaults(physics_mvp_test)
    mecraft_disable_rtti(physics_mvp_test)
    mecraft_disable_exceptions(physics_mvp_test)
    if(MECRAFT_ENABLE_PCH)
        target_precompile_headers(physics_mvp_test REUSE_FROM mecraft_pch)
    endif()
    mecraft_apply_ipo(physics_mvp_test)
    mecraft_copy_runtime_assets(physics_mvp_test)
    add_test(NAME physics_mvp_test COMMAND physics_mvp_test)

    # 测试列表
    add_mecraft_test(world_flat_test tests/world_flat_test.cpp)
    add_mecraft_test(terrain_tree_generation_test tests/terrain_tree_generation_test.cpp)
    add_mecraft_test(block_state_registry_test tests/block_state_registry_test.cpp)
    mecraft_copy_runtime_assets(block_state_registry_test)
    add_mecraft_test(block_selection_test tests/block_selection_test.cpp)
    mecraft_copy_runtime_assets(block_selection_test)
    add_mecraft_test(camera_controller_test tests/camera_controller_test.cpp)
    add_mecraft_test(presentation_interpolation_test tests/presentation_interpolation_test.cpp)
    add_mecraft_test(audio_asset_decode_test tests/audio_asset_decode_test.cpp)
    add_mecraft_test(audio_catalog_test tests/audio_catalog_test.cpp)
    add_mecraft_test(audio_decoder_registry_test tests/audio_decoder_registry_test.cpp)
    add_mecraft_test(audio_test tests/audio_test.cpp)
    add_mecraft_test(audio_sync_system_test tests/audio_sync_system_test.cpp)
    add_mecraft_test(projectile_definition_test tests/projectile_definition_test.cpp)
    add_mecraft_test(terrain_perf_test tests/terrain_perf_test.cpp)
    add_mecraft_test(day_night_system_test tests/day_night_system_test.cpp)
    add_mecraft_test(model_scene_hierarchy_test tests/model_scene_hierarchy_test.cpp)
    add_mecraft_test(model_scene_command_history_test tests/model_scene_command_history_test.cpp)
    add_mecraft_test(model_scene_serializer_test tests/model_scene_serializer_test.cpp)
    add_mecraft_test(sleeping_state_test tests/sleeping_state_test.cpp)
    add_mecraft_test(gameplay_sky_renderer_test tests/gameplay_sky_renderer_test.cpp)
    add_mecraft_test(rhi_shader_compiler_test tests/rhi_shader_compiler_test.cpp)
    add_test(
        NAME renderer_removed_feature_test
        COMMAND ${CMAKE_COMMAND}
                -DMECRAFT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
                -P ${PROJECT_SOURCE_DIR}/tests/renderer_removed_feature_test.cmake)
    add_mecraft_test(app_launch_options_test tests/app_launch_options_test.cpp)
    add_mecraft_test(camera_path_contract_test tests/camera_path_contract_test.cpp)
    target_compile_definitions(camera_path_contract_test PRIVATE
            MECRAFT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_mecraft_test(validation_run_controller_test tests/validation_run_controller_test.cpp)
    target_compile_definitions(validation_run_controller_test PRIVATE
            MECRAFT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_mecraft_test(validation_scene_contract_test tests/validation_scene_contract_test.cpp)
    target_compile_definitions(validation_scene_contract_test PRIVATE
            MECRAFT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_mecraft_test(reference_capture_manifest_test tests/reference_capture_manifest_test.cpp)
    target_compile_definitions(reference_capture_manifest_test PRIVATE
            MECRAFT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_mecraft_test(texture_capture_test tests/texture_capture_test.cpp)
    add_mecraft_test(temporal_frame_contract_test tests/temporal_frame_contract_test.cpp)
    add_mecraft_test(clustered_lighting_contract_test
            tests/clustered_lighting_contract_test.cpp)
    target_compile_definitions(clustered_lighting_contract_test PRIVATE
            MECRAFT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_mecraft_test(sky_ibl_contract_test tests/sky_ibl_contract_test.cpp)
    target_compile_definitions(sky_ibl_contract_test PRIVATE
            MECRAFT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_mecraft_test(gpu_light_contract_test tests/gpu_light_contract_test.cpp)
    target_compile_definitions(gpu_light_contract_test PRIVATE
            MECRAFT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_mecraft_test(local_shadow_contract_test
            tests/local_shadow_contract_test.cpp)
    target_compile_definitions(local_shadow_contract_test PRIVATE
            MECRAFT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_mecraft_test(reflection_probe_contract_test
            tests/reflection_probe_contract_test.cpp)
    target_compile_definitions(reflection_probe_contract_test PRIVATE
            MECRAFT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_mecraft_test(voxel_reflection_probe_source_contract_test
            tests/voxel_reflection_probe_source_contract_test.cpp)
    add_mecraft_test(gltf_punctual_light_test
            tests/gltf_punctual_light_test.cpp)
    target_compile_definitions(gltf_punctual_light_test PRIVATE
            MECRAFT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_mecraft_test(voxel_light_registry_test
            tests/voxel_light_registry_test.cpp)
    add_mecraft_test(gpu_material_contract_test tests/gpu_material_contract_test.cpp)
    target_compile_definitions(gpu_material_contract_test PRIVATE
            MECRAFT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_mecraft_test(gpu_scene_contract_test tests/gpu_scene_contract_test.cpp)
    target_compile_definitions(gpu_scene_contract_test PRIVATE
            MECRAFT_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_mecraft_test(render_feature_contract_test tests/render_feature_contract_test.cpp)
    add_mecraft_test(gpu_timing_history_test tests/gpu_timing_history_test.cpp)
    add_mecraft_test(rhi_render_graph_test tests/rhi_render_graph_test.cpp)
    add_mecraft_test(presentation_controller_test tests/presentation_controller_test.cpp)
    add_mecraft_test(presented_frame_rate_sampler_test tests/presented_frame_rate_sampler_test.cpp)
    if(MECRAFT_RHI_BACKEND_OPENGL)
        add_mecraft_test(rhi_core_test tests/rhi_core_test.cpp)
    endif()
    add_mecraft_test(rhi_device_factory_test tests/rhi_device_factory_test.cpp)
    target_compile_definitions(rhi_device_factory_test PRIVATE
            $<$<BOOL:${MECRAFT_RHI_BACKEND_OPENGL}>:MECRAFT_TEST_RHI_OPENGL>
            $<$<BOOL:${MECRAFT_RHI_BACKEND_VULKAN}>:MECRAFT_TEST_RHI_VULKAN>
            $<$<STREQUAL:${MECRAFT_DEFAULT_RHI_BACKEND},OpenGL>:MECRAFT_TEST_DEFAULT_RHI_OPENGL>
            $<$<STREQUAL:${MECRAFT_DEFAULT_RHI_BACKEND},Vulkan>:MECRAFT_TEST_DEFAULT_RHI_VULKAN>
    )
    if(MECRAFT_RHI_BACKEND_VULKAN)
        add_mecraft_test(fsr31_resource_contract_test tests/fsr31_resource_contract_test.cpp)
        if(MECRAFT_ENABLE_FSR31)
            add_mecraft_test(fsr31_sdk_test tests/fsr31_sdk_test.cpp)
            target_link_libraries(fsr31_sdk_test PRIVATE mecraft_fidelityfx_fsr31)
        endif()
        add_mecraft_test(vulkan_rhi_smoke_test tests/vulkan_rhi_smoke_test.cpp)
        add_mecraft_test(vulkan_requirement_collector_test tests/vulkan_requirement_collector_test.cpp)
        if(MECRAFT_ENABLE_STREAMLINE)
            mecraft_deploy_streamline(vulkan_rhi_smoke_test)
            add_mecraft_test(dlss_temporal_config_test tests/dlss_temporal_config_test.cpp)
            add_mecraft_test(streamline_runtime_test tests/streamline_runtime_test.cpp)
            mecraft_deploy_streamline(streamline_runtime_test)
        endif()
        set(MECRAFT_VULKAN_LAYER_DIR
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/vulkan/explicit_layer.d")
        set_tests_properties(vulkan_rhi_smoke_test PROPERTIES
                ENVIRONMENT "VK_ADD_LAYER_PATH=${MECRAFT_VULKAN_LAYER_DIR}")
    endif()

    add_mecraft_test(ore_sampling_perf_test tests/ore_sampling_perf_test.cpp)
    add_mecraft_test(drop_system_test tests/drop_system_test.cpp)
    add_mecraft_test(death_system_test tests/death_system_test.cpp)
    add_mecraft_test(block_break_tool_test tests/block_break_tool_test.cpp)
    add_mecraft_test(farmland_moisture_and_tilling_test tests/farmland_moisture_and_tilling_test.cpp)
    add_mecraft_test(mob_ai_hurt_effect_test tests/mob_ai_hurt_effect_test.cpp)
    mecraft_copy_runtime_assets(mob_ai_hurt_effect_test)
    add_mecraft_test(chest_inventory_lifecycle_test tests/chest_inventory_lifecycle_test.cpp)
    add_mecraft_test(keyboard_input_box_test tests/keyboard_input_box_test.cpp)
    add_mecraft_test(crosshair_control_test tests/crosshair_control_test.cpp)
    add_mecraft_test(hotbar_control_test tests/hotbar_control_test.cpp)
    add_mecraft_test(text_renderer_test tests/text_renderer_test.cpp)
    add_mecraft_test(command_input_overlay_test tests/command_input_overlay_test.cpp)
    add_mecraft_test(console_overlay_test tests/console_overlay_test.cpp)
    add_mecraft_test(item_grid_control_test tests/item_grid_control_test.cpp)
    add_mecraft_test(crafting_grid_control_test tests/crafting_grid_control_test.cpp)
    add_mecraft_test(container_behavior_registry_test tests/container_behavior_registry_test.cpp)
    mecraft_copy_runtime_assets(container_behavior_registry_test)
    add_mecraft_test(block_interaction_registry_test tests/block_interaction_registry_test.cpp)
    mecraft_copy_runtime_assets(block_interaction_registry_test)
    add_mecraft_test(container_state_factory_test tests/container_state_factory_test.cpp)
    mecraft_copy_runtime_assets(container_state_factory_test)
    add_mecraft_test(container_ui_registry_test tests/container_ui_registry_test.cpp)
    mecraft_copy_runtime_assets(container_ui_registry_test)
    add_mecraft_test(smelting_system_test tests/smelting_system_test.cpp)
    add_mecraft_test(inventory_panel_control_test tests/inventory_panel_control_test.cpp)
    add_mecraft_test(creative_inventory_panel_control_test tests/creative_inventory_panel_control_test.cpp)
    add_mecraft_test(inventory_drag_logic_test tests/inventory_drag_logic_test.cpp)
    add_mecraft_test(ui_renderer_scheduler_test tests/ui_renderer_scheduler_test.cpp)
    add_mecraft_test(ui_widget_scene_test tests/ui_widget_scene_test.cpp)
    add_mecraft_test(ui_focus_navigation_test tests/ui_focus_navigation_test.cpp)
    add_mecraft_test(ui_box_layout_test tests/ui_box_layout_test.cpp)
    add_mecraft_test(ui_grid_layout_test tests/ui_grid_layout_test.cpp)
    add_mecraft_test(ui_scroll_area_test tests/ui_scroll_area_test.cpp)
    add_mecraft_test(ui_text_input_modifiers_test tests/ui_text_input_modifiers_test.cpp)
    add_mecraft_test(ui_style_resolver_test tests/ui_style_resolver_test.cpp)
    add_mecraft_test(player_intent_build_system_test tests/player_intent_build_system_test.cpp)
    add_mecraft_test(creative_flight_system_test tests/creative_flight_system_test.cpp)
    add_mecraft_test(entity_skin_layout_test tests/entity_skin_layout_test.cpp)
    add_mecraft_test(humanoid_skin_layout_catalog_test tests/humanoid_skin_layout_catalog_test.cpp)
    add_mecraft_test(crafting_system_test tests/crafting_system_test.cpp)
    add_mecraft_test(item_registry_test tests/item_registry_test.cpp)
    add_mecraft_test(chunk_mesher_test tests/chunk_mesher_test.cpp)
    add_mecraft_test(redstone_basic_test tests/redstone_basic_test.cpp)
    mecraft_copy_runtime_assets(redstone_basic_test)
    add_mecraft_test(redstone_device_action_test tests/redstone_device_action_test.cpp)
    mecraft_copy_runtime_assets(redstone_device_action_test)
    add_mecraft_test(redstone_pressure_plate_test tests/redstone_pressure_plate_test.cpp)
    mecraft_copy_runtime_assets(redstone_pressure_plate_test)
    add_mecraft_test(redstone_repeater_test tests/redstone_repeater_test.cpp)
    mecraft_copy_runtime_assets(redstone_repeater_test)
    add_mecraft_test(redstone_observer_test tests/redstone_observer_test.cpp)
    mecraft_copy_runtime_assets(redstone_observer_test)
    add_mecraft_test(redstone_comparator_test tests/redstone_comparator_test.cpp)
    mecraft_copy_runtime_assets(redstone_comparator_test)
    add_mecraft_test(redstone_piston_test tests/redstone_piston_test.cpp)
    mecraft_copy_runtime_assets(redstone_piston_test)
    add_mecraft_test(redstone_control_interaction_test tests/redstone_control_interaction_test.cpp)
    mecraft_copy_runtime_assets(redstone_control_interaction_test)
    add_mecraft_test(redstone_advanced_test tests/redstone_advanced_test.cpp)
    mecraft_copy_runtime_assets(redstone_advanced_test)
    add_mecraft_test(redstone_performance_benchmark tests/redstone_performance_benchmark.cpp)
    mecraft_copy_runtime_assets(redstone_performance_benchmark)
    add_mecraft_test(wire_container_parts_test tests/wire_container_parts_test.cpp)
    mecraft_copy_runtime_assets(wire_container_parts_test)
    add_mecraft_test(wire_container_placement_test tests/wire_container_placement_test.cpp)
    mecraft_copy_runtime_assets(wire_container_placement_test)
    add_mecraft_test(hopper_system_test tests/hopper_system_test.cpp)
    mecraft_copy_runtime_assets(hopper_system_test)
    add_mecraft_test(bucket_use_test tests/bucket_use_test.cpp)
    mecraft_copy_runtime_assets(bucket_use_test)
    add_mecraft_test(water_spread_test tests/water_spread_test.cpp)
    add_mecraft_test(fluid_flow_test tests/fluid_flow_test.cpp)
    add_mecraft_test(thread_pool_test tests/thread_pool_test.cpp)
    add_mecraft_test(chunk_meshing_service_test tests/chunk_meshing_service_test.cpp)
    add_mecraft_test(terrain_render_cache_test tests/terrain_render_cache_test.cpp)
    add_mecraft_test(chunk_section_test tests/chunk_section_test.cpp)
    add_mecraft_test(chunk_aggregation_test tests/chunk_aggregation_test.cpp)
    add_mecraft_test(meshing_perf_test tests/meshing_perf_test.cpp)
    add_mecraft_test(chunk_generation_pipeline_perf_test tests/chunk_generation_pipeline_perf_test.cpp)
    add_mecraft_test(chunk_generation_window_perf_test tests/chunk_generation_window_perf_test.cpp)
    add_mecraft_test(chunk_initial_lightmap_test tests/chunk_initial_lightmap_test.cpp)
    add_mecraft_test(light_solver_sky_rules_test tests/light_solver_sky_rules_test.cpp)
    add_mecraft_test(light_solver_cross_chunk_block_rules_test tests/light_solver_cross_chunk_block_rules_test.cpp)
    add_mecraft_test(light_solver_perf_test tests/light_solver_perf_test.cpp)
    add_mecraft_test(light_cross_chunk_removal_test tests/light_cross_chunk_removal_test.cpp)
    add_mecraft_test(light_service_cross_chunk_block_test tests/light_service_cross_chunk_block_test.cpp)
    add_mecraft_test(light_service_scheduler_test tests/light_service_scheduler_test.cpp)

    # Multiplayer / C/S architecture tests
    add_mecraft_test(in_process_transport_test tests/in_process_transport_test.cpp)
    add_mecraft_test(client_server_authority_test tests/client_server_authority_test.cpp)
    add_mecraft_test(client_world_test tests/client_world_test.cpp)
    add_mecraft_test(client_entity_store_test tests/client_entity_store_test.cpp)
    add_mecraft_test(network_interpolation_system_test tests/network_interpolation_system_test.cpp)
    add_mecraft_test(game_server_test tests/game_server_test.cpp)
    add_mecraft_test(chunk_ticket_manager_test tests/chunk_ticket_manager_test.cpp)
    add_mecraft_test(chunk_save_serializer_test tests/chunk_save_serializer_test.cpp)
endif()

# ===========================================================================
# 8. Install / package
# ===========================================================================
include(GNUInstallDirs)

if(WIN32)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/run_mecraft.bat" CONTENT
"@echo off\r\ncd /d \"%~dp0\"\r\nstart \"\" \"%~dp0mecraft.exe\"\r\n")
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/run_mecraft_server.bat" CONTENT
"@echo off\r\ncd /d \"%~dp0\"\r\nmecraft_server.exe %*\r\npause\r\n")

    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)
endif()

install(TARGETS mecraft mecraft_server
        RUNTIME DESTINATION "."
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/assets/"
        DESTINATION "assets"
)

if(WIN32)
    install(CODE [[
        file(GLOB _mecraft_runtime_dlls
                LIST_DIRECTORIES FALSE
                "$<TARGET_FILE_DIR:mecraft>/*.dll"
                "$<TARGET_FILE_DIR:mecraft_server>/*.dll"
        )
        if(_mecraft_runtime_dlls)
            file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}" TYPE FILE FILES ${_mecraft_runtime_dlls})
        endif()
    ]])
    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/run_mecraft.bat"
            "${CMAKE_CURRENT_BINARY_DIR}/run_mecraft_server.bat"
            DESTINATION "."
    )
endif()

set(CPACK_PACKAGE_NAME "mecraft")
set(CPACK_PACKAGE_VENDOR "Mecraft")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
