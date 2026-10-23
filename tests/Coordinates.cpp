#include "scene/Camera.hpp"
#include "scene/GltfLoader.hpp"
#include "app/OrbitCameraController.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool close(glm::vec3 a, glm::vec3 b) { return glm::length(a-b) < 1e-4f; }
template<class F> void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Expected invalid input to be rejected");
}
}
int main() {
    try {
        Scene scene;
        scene.nodes.resize(2);
        scene.nodes[0].parent = 1; // Parent-first storage is not required.
        scene.nodes[0].local.translation = {1,0,0};
        scene.nodes[1].local.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0,1,0));
        scene.modelTransform.translation = {10,0,0};
        scene.updateWorldTransforms();
        require(close(glm::vec3(scene.nodes[0].world * glm::vec4(0,0,0,1)), {10,0,-1}), "Parent rotation/root placement composition");
        scene.nodes[1].parent = 0;
        rejects([&] { scene.updateWorldTransforms(); });

        Camera camera;
        camera.position = {3,4,5};
        camera.orientation = glm::angleAxis(0.7f, glm::vec3(0,1,0));
        require(close(glm::vec3(camera.view() * glm::vec4(camera.position,1)), {0,0,0}), "Camera origin must map to view origin");
        const auto identity = camera.view() * camera.cameraToWorld();
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
            require(std::abs(identity[i][j] - (i == j ? 1.0f : 0.0f)) < 1e-4f, "View must invert camera pose");
        const auto projection = camera.projection(16.0f/9.0f);
        const auto nearPoint = projection * glm::vec4(0,0,-camera.nearPlane,1);
        const auto farPoint = projection * glm::vec4(0,0,-camera.farPlane,1);
        require(std::abs(nearPoint.z / nearPoint.w) < 1e-4f, "Near plane must map to zero");
        require(std::abs(farPoint.z / farPoint.w - 1) < 1e-4f, "Far plane must map to one");
        require(projection[1][1] < 0, "Vulkan projection Y correction");
        rejects([&] { (void)camera.projection(0); });

        for (const char* name : {"hierarchy.gltf", "hierarchy.glb", "sparse.gltf"}) {
            auto loaded = GltfLoader{}.load(std::filesystem::path(TEST_ASSET_DIR) / name);
            require(loaded.primitives.size() == 1, "Mesh data must be shared between node instances");
            require(loaded.nodes[1].primitives == loaded.nodes[2].primitives, "Instances must reference shared geometry");
            require(close(glm::vec3(loaded.nodes[1].world[3]), {2,3,0}), "Imported parent transforms");
            require(loaded.nodes[2].local.authoredMatrix.has_value(), "Authored matrix must be preserved");
            require(glm::determinant(glm::mat3(loaded.nodes[2].world)) < 0, "Mirrored scale must be preserved");
            require(close(loaded.primitives[0].vertices[1].position, {1,0,0}), "Accessor decoding");
            require(close(loaded.primitives[0].vertices[0].normal, {0,0,1}), "Generate missing normals");
            auto bounds = loaded.worldBounds();
            require(close(bounds.min, {-3,0,0}) && close(bounds.max, {4,4,0}), "World bounds across instances");
            loaded.addGuides();
            require(close(loaded.worldBounds().min, bounds.min) && close(loaded.worldBounds().max, bounds.max), "Guides must not affect framing");
            OrbitCameraController controller;
            for (float aspect : {0.4f, 1.0f, 2.0f}) {
                controller.frame(bounds, aspect);
                const auto vp = controller.camera.projection(aspect) * controller.camera.view();
                for (int i = 0; i < 8; ++i) {
                    const auto p = vp * glm::vec4(i&1 ? bounds.max.x : bounds.min.x, i&2 ? bounds.max.y : bounds.min.y, i&4 ? bounds.max.z : bounds.min.z, 1);
                    const auto ndc = glm::vec3(p) / p.w;
                    require(p.w > 0 && std::abs(ndc.x) <= 1 && std::abs(ndc.y) <= 1 && ndc.z >= 0 && ndc.z <= 1, "Frame must contain all world bounds");
                }
            }
        }
        auto normalMapped = GltfLoader{}.load(std::filesystem::path(TEST_ASSET_DIR) / "normal_map.gltf");
        const auto& normalMaterial = normalMapped.materials.at(normalMapped.primitives.at(0).material);
        require(normalMapped.images.at(normalMapped.textures.at(normalMaterial.normalMap.texture).image).width == 1 && normalMapped.images.at(normalMapped.textures.at(normalMaterial.normalMap.texture).image).height == 1, "Decode embedded normal image dimensions");
        require(normalMapped.images.at(normalMapped.textures.at(normalMaterial.normalMap.texture).image).rgba == std::vector<std::uint8_t>({200,128,240,255}), "Normal pixels must remain linear RGBA8");
        require(std::abs(normalMaterial.normalScale - 0.35f) < 1e-5f && normalMaterial.normalMap.texCoord == 1, "Import normal scale and UV selection");
        require(normalMapped.primitives[0].vertices[1].uv == glm::vec2(1,0), "Use the normal texture's selected UV set");
        require(normalMapped.primitives[0].vertices[0].tangent == glm::vec4(0), "Ignore authored tangents when normals are missing");
        require(!Scene::cube().materials[0].normalMap.present(), "Untextured materials must retain mesh normals");
        rejects([] { (void)GltfLoader{}.load(std::filesystem::path(TEST_ASSET_DIR) / "normal_map_missing_uv.gltf"); });
        for (const char* name : {"pbr_material.gltf", "pbr_material.glb"}) {
            auto pbr = GltfLoader{}.load(std::filesystem::path(TEST_ASSET_DIR) / name);
            const auto& primitive = pbr.primitives.at(0);
            const auto& material = pbr.materials.at(primitive.material);
            require(pbr.primitives.size() == 1 && pbr.nodes.size() == 3 &&
                pbr.nodes[1].primitives == pbr.nodes[2].primitives, "Textured node instances share geometry");
            require(close(glm::vec3(pbr.nodes[1].world[3]), {2,3,0}), "Textured nodes retain parent TRS");
            require(pbr.materials[0].roughness == 1 && pbr.materials[0].metallic == 1 &&
                pbr.materials[0].baseColor == glm::vec4(1), "glTF default material factors");
            require(std::abs(material.metallic - 0.7f) < 1e-5f &&
                std::abs(material.roughness - 0.2f) < 1e-5f &&
                std::abs(material.occlusionStrength - 0.4f) < 1e-5f, "Import PBR factors and AO strength");
            require(material.baseColor == glm::vec4(0.8f,0.6f,0.4f,1), "Base color factor stays linear");
            for (const SceneTexture* map : {&material.baseColorMap, &material.metallicRoughnessMap,
                    &material.occlusionMap, &material.normalMap}) {
                const auto& texture = pbr.textures.at(map->texture);
                const auto& image = pbr.images.at(texture.image);
                const auto sampler = texture.sampler < 0 ? SceneSampler{} : pbr.samplers.at(texture.sampler);
                require(image.width == 1 && image.height == 1 &&
                    image.rgba == std::vector<std::uint8_t>({200,128,240,255}), "Decode all material images without altering channels");
                require(sampler.magFilter == 9728 && sampler.minFilter == 9984 &&
                    sampler.wrapS == 33071 && sampler.wrapT == 33648, "Preserve sampler filtering and wrapping");
            }
            require(primitive.vertices[1].baseColorUv == glm::vec2(0.75f,0.5f) &&
                primitive.vertices[1].occlusionUv == glm::vec2(0.75f,0.5f) &&
                primitive.vertices[1].metallicRoughnessUv == glm::vec2(1,0) &&
                primitive.vertices[1].uv == glm::vec2(1,0), "Each texture uses its own UV set");
        }
        rejects([] { (void)GltfLoader{}.load(std::filesystem::path(TEST_ASSET_DIR) / "pbr_missing_uv.gltf"); });
        OrbitCameraController controls;
        controls.frame(Scene::cube().worldBounds(), 1.0f);
        auto before = controls.camera.position;
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.state = SDL_BUTTON_LMASK;
        motion.motion.xrel = 50;
        motion.motion.yrel = 25;
        controls.handleEvent(motion, 720);
        require(!close(before, controls.camera.position), "Drag must orbit the camera");
        require(std::abs(glm::length(before) - glm::length(controls.camera.position)) < 1e-4f, "Orbit must preserve target distance");
        before = controls.camera.position;
        motion.motion.state = SDL_BUTTON_MMASK;
        controls.handleEvent(motion, 720);
        require(!close(before, controls.camera.position), "Middle drag must pan the camera");
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.y = 100000;
        controls.handleEvent(wheel, 720);
        require(std::isfinite(controls.camera.position.x) && controls.camera.nearPlane > 0 &&
            controls.camera.farPlane > controls.camera.nearPlane, "Extreme dolly must retain a valid camera");
        rejects([] { (void)GltfLoader{}.load(std::filesystem::path(TEST_ASSET_DIR) / "invalid.gltf"); });
        std::cout << "Coordinate, camera, hierarchy and accessor checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
