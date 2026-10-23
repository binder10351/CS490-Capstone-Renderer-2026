#pragma once
#include "Transform.hpp"
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <map>

struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{}; // Normal-map UVs. Each material texture can select a different set.
    glm::vec2 baseColorUv{}, metallicRoughnessUv{}, occlusionUv{};
    glm::vec2 emissiveUv{};
    glm::vec4 color{1.0f};
    glm::vec4 tangent{}; // xyz tangent, w handedness; zero means derive from UVs.
};
struct Bounds {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};
    [[nodiscard]] bool valid() const { return glm::all(glm::lessThanEqual(min, max)); }
    void include(glm::vec3 p) { min = glm::min(min, p); max = glm::max(max, p); }
};
struct SceneImage {
    std::string name;
    std::uint32_t width = 0, height = 0;
    // RGBA channels; 16-bit samples use native-endian uint16 representations.
    std::vector<std::uint8_t> rgba;
    int bits = 8;
};
struct SceneSampler {
    int magFilter = 9729, minFilter = 9987;
    int wrapS = 10497, wrapT = 10497;
};
struct SceneTextureResource {
    std::string name;
    std::size_t image = 0;
    int sampler = -1;
};
// Material binding; pixels belong to Scene::images and can be shared by slots.
struct SceneTexture {
    int texture = -1;
    int texCoord = 0;
    [[nodiscard]] bool present() const { return texture >= 0; }
};
// Numeric values are part of the CPU/shader material interface.
enum class AlphaMode { Opaque = 0, Mask = 1, Blend = 2 };
struct SceneMaterial {
    std::string name;
    glm::vec4 baseColor{1.0f};
    float metallic = 1.0f;
    float roughness = 1.0f;
    glm::vec3 emissive{0.0f};
    SceneTexture normalMap, baseColorMap, metallicRoughnessMap, occlusionMap, emissiveMap;
    float occlusionStrength = 1.0f;
    float normalScale = 1.0f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};
struct MeshPrimitive {
    // All authored UV sets survive import; vertices contain the selected GPU streams.
    std::map<int, std::vector<glm::vec2>> texCoords;
    std::map<int, std::vector<glm::vec4>> colors;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t material = 0;
    Bounds bounds;
};
struct SceneMesh {
    std::string name;
    std::vector<std::size_t> primitives;
};
struct SceneNode {
    std::string name;
    int parent = -1;
    int mesh = -1;
    std::vector<std::size_t> children;
    Transform local;
    glm::mat4 world{1.0f};
    std::vector<std::size_t> primitives;
    bool guide = false;
};
struct Scene {
    // Placement of the complete imported model, independent of authored nodes.
    Transform modelTransform;
    std::vector<SceneNode> nodes;
    std::vector<std::size_t> roots;
    std::vector<SceneMesh> meshes;
    std::vector<SceneImage> images;
    std::vector<SceneTextureResource> textures;
    std::vector<SceneSampler> samplers;
    std::vector<std::string> unsupportedExtensions;
    std::vector<std::string> warnings;
    std::vector<MeshPrimitive> primitives;
    std::vector<SceneMaterial> materials{SceneMaterial{}};

    void updateWorldTransforms();
    [[nodiscard]] Bounds worldBounds() const;
    void addGuides();
    static Scene cube();
};
