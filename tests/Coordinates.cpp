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
