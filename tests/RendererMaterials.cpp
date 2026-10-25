// Optional Vulkan integration test. --capture exposes deterministic frames to
// check_material_frames.py; the default mode also works without ImageMagick.
#include "renderer/Renderer.hpp"
#include "app/ViewerUi.hpp"
#include "scene/GltfLoader.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
Scene plane() {
    Scene scene;
    MeshPrimitive primitive;
    for (const auto p : {glm::vec3(-1,-1,0), glm::vec3(1,-1,0), glm::vec3(1,1,0), glm::vec3(-1,1,0)}) {
        Vertex v;
        v.position = p;
        v.normal = {0,0,1};
        primitive.vertices.push_back(v);
        primitive.bounds.include(p);
    }
    primitive.indices = {0,1,2, 0,2,3};
    scene.primitives.push_back(primitive);
    SceneNode node;
    node.primitives = {0};
    scene.nodes.push_back(node);
    scene.materials[0].baseColor = {0,0,0,0.25f};
    scene.materials[0].emissive = {1,0,0};
    scene.updateWorldTransforms();
    return scene;
}
}
int main(int argc, char** argv) {
    const bool capture = argc > 1 && std::string(argv[1]) == "--capture";
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 77; }
    auto* window = SDL_CreateWindow("Material render tests", 256, 256, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << SDL_GetError() << '\n'; SDL_Quit(); return 77; }
    try {
        Camera camera;
        // Keep legacy pixel expectations linear/clamped; exercise the new effects separately.
        const auto baselineSettings = [] {
            RenderSettings result;
            result.toneMapping = false;
            result.fxaa = false;
            return result;
        };
        RenderSettings settings = baselineSettings();
        auto frame = [&](const std::string& name, Renderer& renderer, Scene& scene, ViewerUi* ui = nullptr) {
            scene.updateWorldTransforms();
            renderer.updateMaterialFactors(scene);
            for (int i = 0; i < 3; ++i) {
                SDL_PumpEvents();
                if (ui) {
                    renderer.beginUiFrame();
                    bool guides = false;
                    ui->build(settings, guides);
                }
                renderer.drawFrame(scene, camera, false, settings, ui ? ui->drawData() : nullptr);
            }
            renderer.updateMaterialFactors(scene); // Wait for the captured frame.
            if (capture) {
                std::cout << "READY " << name << std::endl;
                std::string ack;
                if (!std::getline(std::cin, ack)) throw std::runtime_error("Capture controller disconnected");
            }
        };
        {
            auto scene = plane();
            Renderer renderer(window, scene);
            frame("opaque", renderer, scene);
            settings.toneMapping = true;
            settings.fxaa = true;
            frame("post_tone_mapping", renderer, scene);
            settings.exposure = -2.0f;
            frame("post_exposure", renderer, scene);
            settings.toneMapping = false;
            frame("post_tone_mapping_off", renderer, scene);
            settings.fxaa = false;
            frame("post_fxaa_off", renderer, scene);
            settings = baselineSettings();
            scene.materials[0].alphaMode = AlphaMode::Mask;
            frame("mask_discard", renderer, scene);
            scene.materials[0].alphaCutoff = 0.2f;
            frame("mask_keep", renderer, scene);
            scene.materials[0].alphaMode = AlphaMode::Blend;
            frame("blend", renderer, scene);
            scene.materials[0].alphaMode = AlphaMode::Opaque;
            scene.nodes[0].local.rotation = glm::angleAxis(glm::pi<float>(), glm::vec3(0,1,0));
            frame("culled_back", renderer, scene);
            scene.materials[0].doubleSided = true;
            frame("double_back", renderer, scene);
            scene.nodes[0].local.rotation = {1,0,0,0};
            scene.nodes[0].local.scale = {-1,1,1};
            scene.materials[0].doubleSided = false;
            frame("mirrored_front", renderer, scene);
            // Exercise every blend/double-sided/mirrored pipeline combination.
            for (int i = 0; i < 8; ++i) {
                scene.nodes[0].local.scale.x = i & 1 ? -1 : 1;
                scene.materials[0].doubleSided = (i & 2) != 0;
                scene.materials[0].alphaMode = i & 4 ? AlphaMode::Blend : AlphaMode::Opaque;
                frame("variant_" + std::to_string(i), renderer, scene);
            }
            scene.nodes[0].local.scale = {1,1,1};
            auto& m = scene.materials[0];
            m.alphaMode = AlphaMode::Opaque;
            m.doubleSided = false;
            m.emissive = {0,0,0};
            m.baseColor = {0.5f,0.5f,0.5f,1};
            m.metallic = 0;
            frame("lit_front", renderer, scene);
            m.doubleSided = true;
            scene.nodes[0].local.rotation = glm::angleAxis(glm::pi<float>(), glm::vec3(0,1,0));
            frame("lit_back", renderer, scene);
            SDL_SetWindowSize(window, 320, 240);
            SDL_Delay(50);
            frame("resized", renderer, scene);
            SDL_SetWindowSize(window, 256, 256);
            SDL_Delay(50);
        }
        {
            auto scene = plane();
            scene.materials[0].alphaMode = AlphaMode::Blend;
            scene.materials[0].baseColor.a = 0.5f;
            scene.materials.push_back(scene.materials[0]);
            scene.materials[1].emissive = {0,1,0};
            scene.primitives.push_back(scene.primitives[0]);
            scene.primitives[1].material = 1;
            scene.nodes.push_back(scene.nodes[0]);
            scene.nodes[0].primitives = {1}; // Near plane deliberately submitted first.
            scene.nodes[0].local.translation.z = 0.2f;
            Renderer renderer(window, scene);
            frame("sorted_blend", renderer, scene);
        }
        {
            auto scene = plane();
            scene.images.push_back({"Emissive pixel", 1, 1, {200,128,240,255}});
            scene.textures.push_back({"Shared texture", 0, -1});
            scene.materials[0].emissiveMap = {0,0};
            scene.materials[0].emissive = {1,1,1};
            // Avoid a direct specular contribution for exact emissive verification.
            for (auto& v : scene.primitives[0].vertices) v.normal = {0,0,-1};
            Renderer renderer(window, scene);
            frame("emissive_srgb", renderer, scene);
            settings.emissive = false;
            frame("emissive_off", renderer, scene);
            settings = baselineSettings();
        }
        {
            auto source = GltfLoader{}.load(std::filesystem::path(TEST_ASSET_DIR) / "core_materials_16.gltf");
            auto scene = plane();
            scene.images = std::move(source.images);
            scene.textures = std::move(source.textures);
            scene.samplers = std::move(source.samplers);
            scene.materials[0].emissiveMap = {0,0};
            scene.materials[0].emissive = {1,1,1};
            for (auto& v : scene.primitives[0].vertices) v.normal = {0,0,-1};
            Renderer renderer(window, scene);
            frame("emissive_16", renderer, scene);
        }
        {
            auto scene = plane();
            scene.materials[0].emissive = {0,0,0};
            scene.materials[0].baseColor = {1,1,1,1};
            scene.materials[0].metallic = 0;
            for (auto& v : scene.primitives[0].vertices) v.color = {1,0,0,1};
            Renderer renderer(window, scene);
            frame("vertex_color", renderer, scene);
            settings.vertexColors = false;
            frame("vertex_color_off", renderer, scene);
            settings.worldLightEnabled = false;
            frame("ambient_only", renderer, scene);
            settings.ambientIntensity = 0;
            frame("lights_off", renderer, scene);
            settings.worldLightEnabled = true;
            settings.worldLightIntensity = 0;
            frame("intensity_zero", renderer, scene);
            settings.worldLightIntensity = 2;
            frame("direct_only", renderer, scene);
            settings.diffuse = false;
            settings.specular = false;
            frame("shading_off", renderer, scene);
            settings = baselineSettings();
        }
        {
            auto scene = GltfLoader{}.load(std::filesystem::path(TEST_ASSET_DIR) / "core_materials.glb");
            Renderer renderer(window, scene);
            frame("loaded_glb", renderer, scene);
        }
        {
            ViewerUi ui(window);
            {
                auto scene = plane();
                Renderer renderer(window, scene, true);
                frame("ui", renderer, scene, &ui);
                SDL_SetWindowSize(window, 320, 240);
                SDL_Delay(50);
                frame("ui_resized", renderer, scene, &ui);
            }
            // Keep the context/SDL backend while replacing the model renderer.
            {
                auto scene = plane();
                Renderer renderer(window, scene, true);
                frame("ui_reloaded", renderer, scene, &ui);
            }
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "Vulkan material, factor update and resize checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
}
