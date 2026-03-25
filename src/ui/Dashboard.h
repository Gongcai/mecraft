//
// Created by Caiwe on 2026/3/25.
//

#ifndef MECRAFT_DASHBOARD_H
#define MECRAFT_DASHBOARD_H
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_impl_glfw.h"
#include "../third_party/imgui/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include "../core/Camera.h"
#include "../core/Time.h"
#include "../core/Window.h"
#include "../player/Player.h"
#include "../world/World.h"
class Dashboard {
public:
    Dashboard();
    ~Dashboard();
    void init(const Window& window);
    void render( Player &player, World &world, Camera &camera);
private:
    void showPlayerStats( Player& player);
    void showWorldStats( World& world);
    void showCameraStats( Camera& camera);
    void showPerformanceStats();
};


#endif //MECRAFT_DASHBOARD_H