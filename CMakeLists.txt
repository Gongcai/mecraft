cmake_minimum_required(VERSION 3.20)
project(mecraft LANGUAGES C CXX)

# ===========================================================================
# 全局设置
# ===========================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 开启链接期优化
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# 查找依赖

find_package(glfw3 REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(EnTT CONFIG REQUIRED)


# [第三方库]
set(SRC_THIRD_PARTY
        assets/third_party/glad/glad.c
        assets/third_party/imgui/imgui.cpp
        assets/third_party/imgui/imgui_demo.cpp
        assets/third_party/imgui/imgui_draw.cpp
        assets/third_party/imgui/imgui_impl_glfw.cpp
        assets/third_party/imgui/imgui_impl_opengl3.cpp
        assets/third_party/imgui/imgui_tables.cpp
        assets/third_party/imgui/imgui_widgets.cpp
        assets/third_party/FastNoise/FastNoiseLite.h
)

# [核心逻辑]
set(SRC_CORE
        src/core/Window.cpp
        src/core/Camera.cpp
        src/core/InputManager.cpp
        src/core/InputContextManager.cpp
        src/core/GameManager.cpp
        src/core/app_states/AppStateMachine.cpp
        src/core/app_states/GameplayAppState.cpp
        src/core/Game.cpp
        src/core/Time.cpp
        src/core/GameStateMachine.cpp
        src/core/states/GameplayState.cpp
        src/core/states/CommandState.cpp
        src/core/NamespacedId.cpp
        src/core/IdRegistry.cpp
        src/core/CameraController.cpp
        src/thread/ThreadPool.cpp
        src/resource/ResourceMgr.cpp
        src/locale/LocaleManager.cpp
)

# [世界与地形]
set(SRC_WORLD
        src/world/Block.cpp
        src/world/BlockSelection.cpp
        src/world/BlockStateRegistry.cpp
        src/world/Chunk.cpp
        src/world/SubChunk.cpp
        src/world/Palette.cpp
        src/world/BitPackedArray.cpp
        src/world/LightSolver.cpp
        src/world/LightService.cpp
        src/world/LightCache.cpp
        src/world/Placement.cpp
        src/world/PropIndices.cpp
        src/world/FluidRegistry.cpp
        src/world/FluidState.cpp
        src/world/FluidFlow.cpp
        src/world/FluidSystem.cpp
        src/world/BlockNeighborUpdateQueue.cpp
        src/world/TerrainGenerator.cpp
        src/world/World.cpp
        src/world/DropSystem.cpp
        src/world/DayNightSystem.cpp
)

# [渲染与UI]
set(SRC_RENDERER
        src/renderer/Shader.cpp
        src/renderer/Renderer.cpp
        src/renderer/DropRenderer.cpp
        src/renderer/FirstPersonHeldItemRenderer.cpp
        src/renderer/ItemModelMesh.cpp
        src/renderer/MeshBuilderRegistry.cpp
        src/renderer/PostProcessRenderer.cpp
        src/renderer/DeferredRenderTargets.cpp
        src/renderer/stb_image.cpp
        src/renderer/ChunkMesher.cpp
        src/renderer/ChunkMeshingService.cpp
        src/renderer/GameplaySkyRenderer.cpp
        src/renderer/HumanoidRenderer.cpp
        src/renderer/SkyboxRenderer.cpp
        src/renderer/WorldRenderBuffer.cpp
)

set(SRC_UI
        src/ui/Dashboard.cpp
        src/ui/KeyboardInputBox.cpp
        src/ui/ConsoleDisplayBox.cpp
        src/ui/CrosshairControl.cpp
        src/ui/TextRenderer.cpp
        src/ui/GlyphAtlas.cpp
        src/ui/UILayout.cpp
        src/ui/HotbarControl.cpp
        src/ui/HudControl.cpp
        src/ui/HeldItemPreviewControl.cpp
        src/ui/CommandInputOverlay.cpp
        src/ui/ConsoleOverlay.cpp
        src/ui/PickableOverlay.cpp
        src/ui/Pickable.cpp
        src/ui/UIInputAdapter.cpp
        src/ui/ItemGridControl.cpp
        src/ui/InventoryPanelControl.cpp
        src/ui/CreativeInventoryPanelControl.cpp
        src/ui/CraftingGridControl.cpp
        src/ui/UIRenderer.cpp
        src/ui/UIPanel.cpp
        src/ui/UIText.cpp
        src/ui/UIButton.cpp
        src/ui/UIScene.cpp
        src/ui/ScreenTransition.cpp
        src/ui/UITheme.cpp
        src/ui/UIThemePresets.cpp
        src/ui/UISlider.cpp
        src/ui/UICheckbox.cpp
        src/ui/UIDropdown.cpp
        src/ui/UIScrollArea.cpp
        src/ui/UIImage.cpp
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
set(SRC_PARTICLE src/particle/ParticleSystem.cpp)
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
        ${SRC_CORE} ${SRC_WORLD} ${SRC_RENDERER} ${SRC_UI}
        ${SRC_ECS} ${SRC_PLAYER} ${SRC_PHYSICS} ${SRC_PARTICLE}
        ${SRC_ITEM} ${SRC_CRAFTING} ${SRC_AUDIO}
)

# ===========================================================================
# 3. 构建核心静态库
# ===========================================================================
add_library(mecraft_core STATIC ${CORE_SOURCES} ${SRC_THIRD_PARTY})



# 设置包含路径：注意这里将 third_party 标记为 SYSTEM，编译器将不再报告里面的警告
target_include_directories(mecraft_core PUBLIC src)
target_include_directories(mecraft_core SYSTEM PUBLIC assets/third_party)

# 统一链接依赖
target_link_libraries(mecraft_core PUBLIC
        glfw
        glm::glm
        nlohmann_json::nlohmann_json
        OpenAL::OpenAL
        Freetype::Freetype
        EnTT::EnTT
)

# 统一宏定义
target_compile_definitions(mecraft_core PUBLIC
        GLFW_INCLUDE_NONE
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:MECRAFT_DEBUG>
)

# 基于 Target 的编译器选项（更严谨的代码质量控制）
if (MSVC)
    target_compile_options(mecraft_core PRIVATE /utf-8 /W4 /WX-)
else()
    target_compile_options(mecraft_core PRIVATE -Wall -Wextra -pedantic)
endif()

# ===========================================================================
# 4. 主程序入口
# ===========================================================================
add_executable(mecraft main.cpp)
target_link_libraries(mecraft PRIVATE mecraft_core)
if (MSVC)
    target_compile_options(mecraft PRIVATE /utf-8)
endif()

# ===========================================================================
# 5. 测试用例
# ===========================================================================
enable_testing()

# 辅助宏：减少重复代码
macro(add_mecraft_test TEST_NAME SOURCE_FILE)
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    target_link_libraries(${TEST_NAME} PRIVATE mecraft_core)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endmacro()

# 物理测试 (需要特殊宏定义)
add_executable(physics_mvp_test tests/physics_mvp_test.cpp)
target_link_libraries(physics_mvp_test PRIVATE mecraft_core)
target_compile_definitions(physics_mvp_test PRIVATE MECRAFT_NO_TEXTURES)
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
add_mecraft_test(thread_pool_test tests/thread_pool_test.cpp)
add_mecraft_test(chunk_meshing_service_test tests/chunk_meshing_service_test.cpp)
add_mecraft_test(chunk_section_test tests/chunk_section_test.cpp)
add_mecraft_test(chunk_aggregation_test tests/chunk_aggregation_test.cpp)
add_mecraft_test(meshing_perf_test tests/meshing_perf_test.cpp)
add_mecraft_test(chunk_initial_lightmap_test tests/chunk_initial_lightmap_test.cpp)
add_mecraft_test(light_solver_sky_rules_test tests/light_solver_sky_rules_test.cpp)
add_mecraft_test(light_solver_cross_chunk_block_rules_test tests/light_solver_cross_chunk_block_rules_test.cpp)
add_mecraft_test(light_solver_perf_test tests/light_solver_perf_test.cpp)
add_mecraft_test(light_service_cross_chunk_block_test tests/light_service_cross_chunk_block_test.cpp)
add_mecraft_test(light_service_scheduler_test tests/light_service_scheduler_test.cpp)

