#pragma once
#include "Transform.hpp"
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{};
    glm::vec4 tangent{}; // xyz tangent, w handedness; zero means derive from UVs.
};
struct Bounds {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};
    [[nodiscard]] bool valid() const { return glm::all(glm::lessThanEqual(min, max)); }
    void include(glm::vec3 p) { min = glm::min(min, p); max = glm::max(max, p); }
};
struct SceneNormalMap {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
struct SceneMaterial {
    glm::vec4 baseColor{0.8f, 0.8f, 0.8f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.6f;
    glm::vec3 emissive{0.0f};
    SceneNormalMap normalMap;
    float normalScale = 1.0f;
    int normalTexCoord = 0;
};
struct MeshPrimitive {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t material = 0;
    Bounds bounds;
};
struct SceneNode {
    std::string name;
    int parent = -1;
    Transform local;
    glm::mat4 world{1.0f};
    std::vector<std::size_t> primitives;
    bool guide = false;
};
struct Scene {
    // Placement of the complete imported model, independent of authored nodes.
    Transform modelTransform;
    std::vector<SceneNode> nodes;
    std::vector<MeshPrimitive> primitives;
    std::vector<SceneMaterial> materials{SceneMaterial{}};

    void updateWorldTransforms();
    [[nodiscard]] Bounds worldBounds() const;
    void addGuides();
    static Scene cube();
};
