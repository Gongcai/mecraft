cmake_minimum_required(VERSION 3.20)
project(mecraft LANGUAGES C CXX)

# ===========================================================================
# 全局设置
# ===========================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MECRAFT_ENABLE_IPO "Enable IPO/LTO for optimized configurations" ON)
option(MECRAFT_ENABLE_PCH "Enable precompiled headers" ON)
option(MECRAFT_BUILD_TESTS "Build Mecraft tests" ON)
set(MECRAFT_ASSETS_DIR "${PROJECT_SOURCE_DIR}/assets" CACHE PATH "Absolute path to Mecraft runtime assets")
file(TO_CMAKE_PATH "${MECRAFT_ASSETS_DIR}" MECRAFT_ASSETS_DIR_COMPILE)

# 检查链接期优化支持
include(CheckIPOSupported)
if(MECRAFT_ENABLE_IPO)
    check_ipo_supported(RESULT MECRAFT_IPO_SUPPORTED OUTPUT MECRAFT_IPO_ERROR)
else()
    set(MECRAFT_IPO_SUPPORTED FALSE)
endif()

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

# 查找依赖

find_package(glfw3 REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(EnTT CONFIG REQUIRED)


# [应用层]
set(SRC_APP
        src/app/GameManager.cpp
        src/app/states/AppStateMachine.cpp
        src/app/states/GameplayAppState.cpp
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
        src/game/states/CommandState.cpp
)

# [核心基础]
set(SRC_CORE
        src/thread/ThreadPool.cpp
        src/resource/ResourceMgr.cpp
        src/locale/LocaleManager.cpp
)

# [世界与地形]
set(SRC_WORLD
        src/world/block/Block.cpp
        src/world/block/BlockSelection.cpp
        src/world/block/BlockStateRegistry.cpp
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
        src/world/gen/TerrainGenerator.cpp
        src/world/World.cpp
        src/world/DropSystem.cpp
        src/world/DayNightSystem.cpp
        src/world/WeatherSystem.cpp
)

# [渲染与UI]
set(SRC_RENDERER
        # Core framework
        src/renderer/core/Shader.cpp
        src/renderer/core/Renderer.cpp
        src/renderer/core/RenderScene.cpp

        # Render targets
        src/renderer/targets/CommonFrameTargets.cpp
        src/renderer/targets/DeferredFrameTargets.cpp
        src/renderer/targets/DeferredRenderTargets.cpp
        src/renderer/targets/ShadowTargets.cpp

        # Render passes
        src/renderer/passes/RenderPass.cpp
        src/renderer/passes/SsaoPass.cpp

        # Sub-renderers
        src/renderer/renderers/DropRenderer.cpp
        src/renderer/renderers/FirstPersonHeldItemRenderer.cpp
        src/renderer/renderers/GameplaySkyRenderer.cpp
        src/renderer/renderers/HumanoidRenderer.cpp
        src/renderer/renderers/PostProcessRenderer.cpp
        src/renderer/renderers/SkyboxRenderer.cpp

        # Mesh system
        src/renderer/mesh/ChunkMesher.cpp
        src/renderer/mesh/ChunkMeshingService.cpp
        src/renderer/mesh/ItemModelMesh.cpp
        src/renderer/mesh/MeshBuilderRegistry.cpp
        src/renderer/mesh/WorldRenderBuffer.cpp

        # Shadow system
        src/renderer/shadow/ShadowMatrices.cpp
        src/renderer/shadow/ShadowCasterCuller.cpp
        src/renderer/shadow/ShadowRenderer.cpp

        # Utility
        src/renderer/stb_image.cpp
)

set(SRC_UI
        # Core UI Framework
        src/ui/Dashboard.cpp
        src/ui/core/UIInputAdapter.cpp
        src/ui/core/UIRenderer.cpp
        src/ui/core/UIScene.cpp
        src/ui/core/ScreenTransition.cpp
        src/ui/core/UITheme.cpp
        src/ui/core/UIThemePresets.cpp

        # Font & Text
        src/ui/font/TextRenderer.cpp
        src/ui/font/GlyphAtlas.cpp

        # Layouts
        src/ui/layout/UILayout.cpp
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

        # HUD & Overlays
        src/ui/hud/CrosshairControl.cpp
        src/ui/hud/HotbarControl.cpp
        src/ui/hud/HudControl.cpp
        src/ui/hud/PickableOverlay.cpp
        src/ui/hud/Pickable.cpp
        src/ui/hud/ConsoleOverlay.cpp
        src/ui/hud/CommandInputOverlay.cpp

        # Inventory UI
        src/ui/inventory/ItemGridControl.cpp
        src/ui/inventory/CraftingGridControl.cpp
        src/ui/inventory/InventoryPanelControl.cpp
        src/ui/inventory/CreativeInventoryPanelControl.cpp
        src/ui/inventory/HeldItemPreviewControl.cpp

        # Screens
        src/ui/screens/MainMenuScreen.cpp
        src/ui/screens/PauseMenuScreen.cpp
)

# [实体与游戏系统]
set(SRC_ECS
        src/ecs/GameplayScene.cpp
        src/ecs/util/PlayerQuery.cpp
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
        src/ecs/systems/interaction/BlockTargetSystem.cpp
        src/ecs/systems/interaction/BlockBreakSystem.cpp
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
        src/ecs/systems/world/BlockSupportSystem.cpp
        src/ecs/systems/steve/SteveAnimationSystem.cpp
        src/ecs/systems/steve/SteveSyncSystem.cpp
        src/ecs/systems/steve/TransformHierarchySystem.cpp
        src/ecs/systems/mob/MobAISystem.cpp
        src/ecs/systems/mob/MobAnimationSystem.cpp
)

# [其他子系统]
set(SRC_PLAYER
        src/player/ActionMap.cpp
        src/player/Inventory.cpp
)
set(SRC_PHYSICS src/physics/PhysicsSystem.cpp)
set(SRC_PARTICLE src/particle/ParticleSystem.cpp src/particle/RainRenderer.cpp)
set(SRC_ITEM src/item/Item.cpp)
set(SRC_CRAFTING src/crafting/CraftingSystem.cpp)
set(SRC_AUDIO
        src/audio/AudioEngine.cpp
        src/audio/BgmSystem.cpp
        src/audio/AudioClip.cpp
        src/audio/AudioSource.cpp
        src/audio/AudioListener.cpp
)

# 合并所有核心代码
set(CORE_SOURCES
        ${SRC_APP} ${SRC_ENGINE} ${SRC_GAME} ${SRC_CORE}
        ${SRC_WORLD} ${SRC_RENDERER} ${SRC_UI}
        ${SRC_ECS} ${SRC_PLAYER} ${SRC_PHYSICS} ${SRC_PARTICLE}
        ${SRC_ITEM} ${SRC_CRAFTING} ${SRC_AUDIO}
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
        third_party/glad/glad.c
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_demo.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_impl_glfw.cpp
        third_party/imgui/imgui_impl_opengl3.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
)
target_compile_features(mecraft_third_party PUBLIC cxx_std_17)
set_target_properties(mecraft_third_party PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(mecraft_third_party SYSTEM PUBLIC third_party)
target_link_libraries(mecraft_third_party PUBLIC glfw)
target_compile_definitions(mecraft_third_party PUBLIC GLFW_INCLUDE_NONE)
if (MSVC)
    target_compile_options(mecraft_third_party PRIVATE /W0)
else()
    target_compile_options(mecraft_third_party PRIVATE -w)
endif()
mecraft_apply_msvc_defaults(mecraft_third_party)

# ===========================================================================
# 5. 构建核心库
# ===========================================================================
add_library(mecraft_core ${CORE_SOURCES})
target_compile_features(mecraft_core PUBLIC cxx_std_17)
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
        Freetype::Freetype
        EnTT::EnTT
)

# 统一宏定义
target_compile_definitions(mecraft_core PUBLIC
        GLFW_INCLUDE_NONE
        "MECRAFT_ASSETS_DIR=\"${MECRAFT_ASSETS_DIR_COMPILE}\""
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:MECRAFT_DEBUG>
)

# 基于 Target 的编译器选项（更严谨的代码质量控制）
mecraft_apply_project_warnings(mecraft_core)
mecraft_apply_msvc_defaults(mecraft_core)

if(MECRAFT_ENABLE_PCH)
    target_precompile_headers(mecraft_core REUSE_FROM mecraft_pch)
endif()

if(MECRAFT_IPO_SUPPORTED)
    set_property(TARGET mecraft_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET mecraft_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    set_property(TARGET mecraft_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE)
endif()

# ===========================================================================
# 6. 主程序入口
# ===========================================================================
add_executable(mecraft main.cpp)
target_compile_features(mecraft PRIVATE cxx_std_17)
target_link_libraries(mecraft PRIVATE mecraft_core)
if (MSVC)
    mecraft_apply_msvc_defaults(mecraft)
endif()
if(MECRAFT_IPO_SUPPORTED)
    set_property(TARGET mecraft PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET mecraft PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    set_property(TARGET mecraft PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE)
endif()

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
        if(MECRAFT_ENABLE_PCH)
            target_precompile_headers(${TEST_NAME} REUSE_FROM mecraft_pch)
        endif()
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endfunction()

    # 物理测试 (需要特殊宏定义)
    add_executable(physics_mvp_test tests/physics_mvp_test.cpp)
    target_compile_features(physics_mvp_test PRIVATE cxx_std_17)
    target_link_libraries(physics_mvp_test PRIVATE mecraft_core)
    target_compile_definitions(physics_mvp_test PRIVATE MECRAFT_NO_TEXTURES)
    mecraft_apply_project_warnings(physics_mvp_test)
    mecraft_apply_msvc_defaults(physics_mvp_test)
    if(MECRAFT_ENABLE_PCH)
        target_precompile_headers(physics_mvp_test REUSE_FROM mecraft_pch)
    endif()
    add_test(NAME physics_mvp_test COMMAND physics_mvp_test)

    # 测试列表
    add_mecraft_test(world_flat_test tests/world_flat_test.cpp)
    add_mecraft_test(terrain_tree_generation_test tests/terrain_tree_generation_test.cpp)
    add_mecraft_test(block_state_registry_test tests/block_state_registry_test.cpp)
    add_mecraft_test(block_selection_test tests/block_selection_test.cpp)
    add_mecraft_test(audio_test tests/audio_test.cpp)
    add_mecraft_test(audio_sync_system_test tests/audio_sync_system_test.cpp)
    add_mecraft_test(terrain_perf_test tests/terrain_perf_test.cpp)
    add_mecraft_test(day_night_system_test tests/day_night_system_test.cpp)
    add_mecraft_test(gameplay_sky_renderer_test tests/gameplay_sky_renderer_test.cpp)

    add_mecraft_test(ore_sampling_perf_test tests/ore_sampling_perf_test.cpp)
    add_mecraft_test(drop_system_test tests/drop_system_test.cpp)
    add_mecraft_test(keyboard_input_box_test tests/keyboard_input_box_test.cpp)
    add_mecraft_test(crosshair_control_test tests/crosshair_control_test.cpp)
    add_mecraft_test(hotbar_control_test tests/hotbar_control_test.cpp)
    add_mecraft_test(text_renderer_test tests/text_renderer_test.cpp)
    add_mecraft_test(command_input_overlay_test tests/command_input_overlay_test.cpp)
    add_mecraft_test(console_overlay_test tests/console_overlay_test.cpp)
    add_mecraft_test(pickable_overlay_test tests/pickable_overlay_test.cpp)
    add_mecraft_test(item_grid_control_test tests/item_grid_control_test.cpp)
    add_mecraft_test(inventory_panel_control_test tests/inventory_panel_control_test.cpp)
    add_mecraft_test(creative_inventory_panel_control_test tests/creative_inventory_panel_control_test.cpp)
    add_mecraft_test(inventory_drag_logic_test tests/inventory_drag_logic_test.cpp)
    add_mecraft_test(ui_renderer_scheduler_test tests/ui_renderer_scheduler_test.cpp)
    add_mecraft_test(ui_widget_scene_test tests/ui_widget_scene_test.cpp)
    add_mecraft_test(ui_focus_navigation_test tests/ui_focus_navigation_test.cpp)
    add_mecraft_test(player_intent_build_system_test tests/player_intent_build_system_test.cpp)
    add_mecraft_test(creative_flight_system_test tests/creative_flight_system_test.cpp)
    # add_mecraft_test(crafting_system_test tests/crafting_system_test.cpp)
    add_mecraft_test(item_registry_test tests/item_registry_test.cpp)
    add_mecraft_test(chunk_mesher_test tests/chunk_mesher_test.cpp)
    add_mecraft_test(water_spread_test tests/water_spread_test.cpp)
    add_mecraft_test(fluid_flow_test tests/fluid_flow_test.cpp)
    add_mecraft_test(thread_pool_test tests/thread_pool_test.cpp)
    add_mecraft_test(chunk_meshing_service_test tests/chunk_meshing_service_test.cpp)
    add_mecraft_test(chunk_section_test tests/chunk_section_test.cpp)
    add_mecraft_test(chunk_aggregation_test tests/chunk_aggregation_test.cpp)
    add_mecraft_test(meshing_perf_test tests/meshing_perf_test.cpp)
    add_mecraft_test(chunk_initial_lightmap_test tests/chunk_initial_lightmap_test.cpp)
    add_mecraft_test(light_solver_sky_rules_test tests/light_solver_sky_rules_test.cpp)
    add_mecraft_test(light_solver_cross_chunk_block_rules_test tests/light_solver_cross_chunk_block_rules_test.cpp)
    add_mecraft_test(light_solver_perf_test tests/light_solver_perf_test.cpp)
    add_mecraft_test(light_cross_chunk_removal_test tests/light_cross_chunk_removal_test.cpp)
    add_mecraft_test(light_service_cross_chunk_block_test tests/light_service_cross_chunk_block_test.cpp)
    add_mecraft_test(light_service_scheduler_test tests/light_service_scheduler_test.cpp)
endif()

