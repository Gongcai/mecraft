//
// Created by Caiwe on 2026/3/24.
//

#ifndef MECRAFT_RAY_H
#define MECRAFT_RAY_H
struct Ray {
    glm::vec3 origin;    // 射线的起点
    glm::vec3 direction; // 射线的方向（通常是单位向量）

    Ray(const glm::vec3& o, const glm::vec3& d) : origin(o), direction(glm::normalize(d)) {}
};

struct RayHit {
    bool hit = false;
    glm::ivec3 blockPos;
    glm::ivec3 normal;   // 命中面的法线 → 用于计算放置位置
    float distance;
};
#endif //MECRAFT_RAY_H