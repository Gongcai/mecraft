#ifndef MECRAFT_CLUSTER_SPHERE_INTERSECTION_GLSL
#define MECRAFT_CLUSTER_SPHERE_INTERSECTION_GLSL

struct ClusterFrustumCorners {
    vec3 near00;
    vec3 near10;
    vec3 near01;
    vec3 near11;
    vec3 far00;
    vec3 far10;
    vec3 far01;
    vec3 far11;
};

vec3 clusterViewRay(mat4 inverseProjection, vec2 ndc) {
    vec4 viewPosition = inverseProjection * vec4(ndc, 1.0, 1.0);
    return (viewPosition.xyz / viewPosition.w) / -((viewPosition.z / viewPosition.w));
}

float clusterDepthBoundary(vec4 depthParameters, uint boundary) {
    float depth = exp((float(boundary) - depthParameters.w) / depthParameters.z);
    return clamp(depth, depthParameters.x, depthParameters.y);
}

ClusterFrustumCorners buildClusterFrustumCorners(
    mat4 inverseProjection,
    vec4 depthParameters,
    uvec3 cluster,
    uvec3 gridSize) {
    float x0 = float(cluster.x) / float(gridSize.x) * 2.0 - 1.0;
    float x1 = float(cluster.x + 1u) / float(gridSize.x) * 2.0 - 1.0;
    float y0 = float(cluster.y) / float(gridSize.y) * 2.0 - 1.0;
    float y1 = float(cluster.y + 1u) / float(gridSize.y) * 2.0 - 1.0;
    float nearDepth = clusterDepthBoundary(depthParameters, cluster.z);
    float farDepth = clusterDepthBoundary(depthParameters, cluster.z + 1u);
    vec3 ray00 = clusterViewRay(inverseProjection, vec2(x0, y0));
    vec3 ray10 = clusterViewRay(inverseProjection, vec2(x1, y0));
    vec3 ray01 = clusterViewRay(inverseProjection, vec2(x0, y1));
    vec3 ray11 = clusterViewRay(inverseProjection, vec2(x1, y1));

    ClusterFrustumCorners corners;
    corners.near00 = ray00 * nearDepth;
    corners.near10 = ray10 * nearDepth;
    corners.near01 = ray01 * nearDepth;
    corners.near11 = ray11 * nearDepth;
    corners.far00 = ray00 * farDepth;
    corners.far10 = ray10 * farDepth;
    corners.far01 = ray01 * farDepth;
    corners.far11 = ray11 * farDepth;
    return corners;
}

float clusterPlaneDistance(vec3 sphereCenter, vec3 planePoint, vec3 planeA, vec3 planeB, vec3 insidePoint) {
    vec3 normal = normalize(cross(planeA - planePoint, planeB - planePoint));
    if (dot(normal, insidePoint - planePoint) < 0.0) {
        normal = -normal;
    }
    return dot(normal, sphereCenter - planePoint);
}

float clusterPointTriangleDistanceSquared(vec3 point, vec3 a, vec3 b, vec3 c) {
    vec3 ab = b - a;
    vec3 ac = c - a;
    vec3 ap = point - a;
    float d1 = dot(ab, ap);
    float d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) {
        return dot(ap, ap);
    }

    vec3 bp = point - b;
    float d3 = dot(ab, bp);
    float d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) {
        return dot(bp, bp);
    }

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        float v = d1 / (d1 - d3);
        vec3 projection = a + v * ab;
        vec3 delta = point - projection;
        return dot(delta, delta);
    }

    vec3 cp = point - c;
    float d5 = dot(ab, cp);
    float d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) {
        return dot(cp, cp);
    }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        float w = d2 / (d2 - d6);
        vec3 projection = a + w * ac;
        vec3 delta = point - projection;
        return dot(delta, delta);
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        vec3 projection = b + w * (c - b);
        vec3 delta = point - projection;
        return dot(delta, delta);
    }

    vec3 normal = normalize(cross(ab, ac));
    float distance = dot(point - a, normal);
    return distance * distance;
}

bool clusterSphereIntersectsCluster(
    vec4 sphere,
    ClusterFrustumCorners corners) {
    if (sphere.w < 0.0) {
        return true;
    }
    vec3 center = sphere.xyz;
    float radius = sphere.w;
    vec3 frustumCenter = (corners.near00 + corners.near10 + corners.near01 + corners.near11 +
                          corners.far00 + corners.far10 + corners.far01 + corners.far11) * 0.125;

    float planeDistances[6];
    planeDistances[0] = clusterPlaneDistance(center, corners.near00, corners.near10, corners.near11, frustumCenter);
    planeDistances[1] = clusterPlaneDistance(center, corners.far00, corners.far11, corners.far10, frustumCenter);
    planeDistances[2] = clusterPlaneDistance(center, corners.near00, corners.far00, corners.far01, frustumCenter);
    planeDistances[3] = clusterPlaneDistance(center, corners.near10, corners.far11, corners.far10, frustumCenter);
    planeDistances[4] = clusterPlaneDistance(center, corners.near00, corners.near01, corners.far01, frustumCenter);
    planeDistances[5] = clusterPlaneDistance(center, corners.near01, corners.far11, corners.far01, frustumCenter);
    float minimumPlaneDistance = planeDistances[0];
    for (uint planeIndex = 1u; planeIndex < 6u; ++planeIndex) {
        minimumPlaneDistance = min(minimumPlaneDistance, planeDistances[planeIndex]);
    }
    if (minimumPlaneDistance < -radius) {
        return false;
    }
    if (minimumPlaneDistance >= 0.0) {
        return true;
    }

    float radiusSquared = radius * radius + 1.0e-5;
    return clusterPointTriangleDistanceSquared(center, corners.near00, corners.near10, corners.near11) <= radiusSquared ||
           clusterPointTriangleDistanceSquared(center, corners.near00, corners.near11, corners.near01) <= radiusSquared ||
           clusterPointTriangleDistanceSquared(center, corners.far00, corners.far11, corners.far10) <= radiusSquared ||
           clusterPointTriangleDistanceSquared(center, corners.far00, corners.far01, corners.far11) <= radiusSquared ||
           clusterPointTriangleDistanceSquared(center, corners.near00, corners.far00, corners.far01) <= radiusSquared ||
           clusterPointTriangleDistanceSquared(center, corners.near00, corners.far01, corners.near01) <= radiusSquared ||
           clusterPointTriangleDistanceSquared(center, corners.near10, corners.far11, corners.far10) <= radiusSquared ||
           clusterPointTriangleDistanceSquared(center, corners.near10, corners.far10, corners.far11) <= radiusSquared ||
           clusterPointTriangleDistanceSquared(center, corners.near00, corners.near10, corners.far10) <= radiusSquared ||
           clusterPointTriangleDistanceSquared(center, corners.near00, corners.far10, corners.far00) <= radiusSquared ||
           clusterPointTriangleDistanceSquared(center, corners.near01, corners.far01, corners.far11) <= radiusSquared ||
           clusterPointTriangleDistanceSquared(center, corners.near01, corners.far11, corners.near11) <= radiusSquared;
}

#endif
