#ifndef MECRAFT_POSTPROCESSRENDERER_H
#define MECRAFT_POSTPROCESSRENDERER_H

#include <glad/glad.h>
#include <glm/vec3.hpp>

class ResourceMgr;
class Shader;
class Window;

struct PostProcessEffects {
    bool underwaterEnabled = false;
    glm::vec3 underwaterTint = glm::vec3(0.42f, 0.62f, 0.95f);
    float underwaterStrength = 0.845f;
};

class PostProcessRenderer {
public:
    ~PostProcessRenderer();

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    // Start capturing world-space rendering into an off-screen scene target.
    void beginScene(const Window& window);

    // Composite captured scene to back buffer with active effects.
    void endSceneAndComposite(const Window& window);

    void setEffects(const PostProcessEffects& effects);

private:
    bool ensureRenderTargets(int width, int height);
    void destroyRenderTargets();
    void initFullscreenTriangle();
    void destroyFullscreenTriangle();

    Shader* m_postProcessShader = nullptr;

    GLuint m_sceneFbo = 0;
    GLuint m_sceneColorTex = 0;
    GLuint m_sceneDepthStencilRbo = 0;

    GLuint m_fullscreenVao = 0;

    int m_targetWidth = 0;
    int m_targetHeight = 0;
    bool m_sceneCaptured = false;

    PostProcessEffects m_effects{};
};

#endif //MECRAFT_POSTPROCESSRENDERER_H


