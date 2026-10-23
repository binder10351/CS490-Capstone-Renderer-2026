#include "Scene.hpp"
#include <functional>
#include <stdexcept>

void Scene::updateWorldTransforms() {
    // Parent is authoritative when callers edit the hierarchy.
    roots.clear();
    for (auto& node : nodes) node.children.clear();
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto parent = nodes[i].parent;
        if (parent < -1 || parent >= static_cast<int>(nodes.size())) throw std::runtime_error("Invalid scene parent");
        if (parent < 0) roots.push_back(i);
        else nodes[parent].children.push_back(i);
    }
    std::vector<unsigned char> state(nodes.size(), 0);
    std::function<void(std::size_t)> visit = [&](std::size_t i) {
        if (state[i] == 2) return;
        if (state[i] == 1) throw std::runtime_error("Cycle in scene hierarchy");
        state[i] = 1;
        auto& node = nodes[i];
        glm::mat4 parent = node.guide ? glm::mat4(1.0f) : modelTransform.matrix();
        if (node.parent >= 0) {
            if (static_cast<std::size_t>(node.parent) >= nodes.size())
                throw std::runtime_error("Invalid scene parent");
            visit(static_cast<std::size_t>(node.parent));
            parent = nodes[node.parent].world;
        }
        node.world = parent * node.local.matrix();
        state[i] = 2;
    };
    for (std::size_t i = 0; i < nodes.size(); ++i) visit(i);
}

Bounds Scene::worldBounds() const {
    Bounds result;
    for (const auto& node : nodes) {
        if (node.guide) continue;
        for (auto index : node.primitives) {
            const auto& bounds = primitives.at(index).bounds;
            if (!bounds.valid()) continue;
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 p{corner & 1 ? bounds.max.x : bounds.min.x,
                    corner & 2 ? bounds.max.y : bounds.min.y,
                    corner & 4 ? bounds.max.z : bounds.min.z};
                result.include(glm::vec3(node.world * glm::vec4(p, 1.0f)));
            }
        }
    }
    return result;
}

Scene Scene::cube() {
    Scene scene;
    scene.materials[0].baseColor = {0.8f, 0.8f, 0.8f, 1.0f};
    scene.materials[0].metallic = 0.0f;
    scene.materials[0].roughness = 0.6f;
    MeshPrimitive mesh;
    const glm::vec3 normals[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (auto n : normals) {
        const auto u = glm::normalize(glm::cross(glm::abs(n.y) > 0.5f ? glm::vec3(0,0,1) : glm::vec3(0,1,0), n));
        const auto v = glm::cross(n, u);
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto uv : {glm::vec2(-1,-1), glm::vec2(1,-1), glm::vec2(1,1), glm::vec2(-1,1)}) {
            const auto p = (n + uv.x * u + uv.y * v) * 0.5f;
            mesh.vertices.push_back({p, n, (uv + 1.0f) * 0.5f});
            mesh.bounds.include(p);
        }
        for (auto i : {0u,1u,2u,0u,2u,3u}) mesh.indices.push_back(base + i);
    }
    scene.primitives.push_back(std::move(mesh));
    SceneNode node;
    node.name = "Cube";
    node.primitives = {0};
    node.mesh = 0;
    scene.meshes.push_back({"Cube", {0}});
    scene.roots.push_back(0);
    scene.nodes.push_back(node);
    scene.updateWorldTransforms();
    return scene;
}

void Scene::addGuides() {
    // Thin boxes use the existing mesh pipeline; guides never affect framing.
    const auto box = Scene::cube().primitives.front();
    auto guideMesh = [&](glm::vec3 color) {
        SceneMaterial material;
        material.baseColor = glm::vec4(0,0,0,1);
        material.emissive = color;
        materials.push_back(material);
        auto mesh = box;
        mesh.material = materials.size() - 1;
        primitives.push_back(std::move(mesh));
        return primitives.size() - 1;
    };
    const auto grid = guideMesh({0.12f,0.12f,0.12f});
    const auto xAxis = guideMesh({1,0,0});
    const auto yAxis = guideMesh({0,1,0});
    const auto zAxis = guideMesh({0,0.3f,1});
    auto line = [&](glm::vec3 center, glm::vec3 scale, std::size_t mesh) {
        SceneNode node;
        node.name = "Coordinate guide";
        node.guide = true;
        node.local.translation = center;
        node.local.scale = scale;
        node.primitives = {mesh};
        roots.push_back(nodes.size());
        nodes.push_back(std::move(node));
    };
    for (int i = -10; i <= 10; ++i) {
        if (i == 0) continue;
        line({static_cast<float>(i),0,0}, {0.008f,0.008f,20}, grid);
        line({0,0,static_cast<float>(i)}, {20,0.008f,0.008f}, grid);
    }
    line({1,0,0}, {2,0.02f,0.02f}, xAxis);
    line({0,1,0}, {0.02f,2,0.02f}, yAxis);
    line({0,0,1}, {0.02f,0.02f,2}, zAxis);
    updateWorldTransforms();
}
