#ifndef MECRAFT_FSR1_PASS_H
#define MECRAFT_FSR1_PASS_H

#include "RenderPass.h"

#include <glad/glad.h>
#include <glm/vec4.hpp>

class ResourceMgr;
class Shader;

class Fsr1Pass : public RenderPass {
public:
    ~Fsr1Pass() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "FSR1"; }

    bool execute(GLuint inputTex,
                 int inputWidth,
                 int inputHeight,
                 int outputWidth,
                 int outputHeight,
                 float sharpness);

private:
    bool ensureTargets(int width, int height);
    void destroyTargets();
    void initFullscreenTriangle();
    void destroyFullscreenTriangle();

    static void populateEasuConstants(glm::vec4& con0,
                                      glm::vec4& con1,
                                      glm::vec4& con2,
                                      glm::vec4& con3,
                                      float inputViewportWidth,
                                      float inputViewportHeight,
                                      float inputTextureWidth,
                                      float inputTextureHeight,
                                      float outputWidth,
                                      float outputHeight);
    static glm::vec4 populateRcasConstants(float sharpness);

    Shader* m_easuShader = nullptr;
    Shader* m_rcasShader = nullptr;
    GLuint m_easuFbo = 0;
    GLuint m_easuTex = 0;
    GLuint m_fullscreenVao = 0;
    int m_width = 0;
    int m_height = 0;
};

#endif // MECRAFT_FSR1_PASS_H
