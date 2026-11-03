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
Scene shadowScene() {
    auto scene = plane();
    scene.materials[0].baseColor = {1,1,1,1};
    scene.materials[0].emissive = {0,0,0};
    scene.materials[0].metallic = 0;
    scene.materials[0].roughness = 1;
    scene.materials.push_back(scene.materials[0]);
    scene.materials[1].baseColor.a = 0.25f;
    scene.images.push_back({"Cutout alpha", 1, 1, {255,255,255,64}});
    scene.textures.push_back({"Cutout", 0, -1});
    scene.materials[1].baseColorMap = {0,0};
    scene.primitives.push_back(scene.primitives[0]);
    scene.primitives[1].material = 1;
    for (auto& vertex : scene.primitives[1].vertices) vertex.color.a = 0.5f;
    scene.nodes.push_back(scene.nodes[0]);
    scene.nodes[1].primitives = {1};
    // Entirely outside the camera view, but upstream of the center receiver.
    scene.nodes[1].local.translation = {-1.2f,0,1.5f};
    scene.nodes[1].local.scale = {0.25f,0.25f,1};
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
            result.shadows = false;
            result.autoExposure = false;
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
        for (std::size_t i = 0; i < 8; ++i) {
            auto scene = GltfLoader{}.load(std::filesystem::path(TEST_ASSET_DIR) / "topologies.gltf");
            scene.nodes[0].primitives = {i};
            settings = baselineSettings();
            Renderer renderer(window, scene);
            frame("topology_" + std::to_string(i), renderer, scene);
        }
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
            settings.autoExposure = true;
            settings.exposureAdaptationSeconds = 0.001f;
            settings.toneMapping = true;
            frame("post_auto", renderer, scene);
            scene.materials[0].emissive = {8, 0, 0};
            settings.exposureAdaptationSeconds = 1.0f;
            frame("post_auto_transition", renderer, scene);
            settings.exposureAdaptationSeconds = 0.001f;
            frame("post_auto_bright", renderer, scene);
            scene.materials[0].emissive = {1, 0, 0};
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
        SDL_SetWindowSize(window, 256, 256);
        SDL_Delay(50);
        settings = baselineSettings();
        settings.worldLightDirection = {0.8f,0,-1};
        settings.ambientIntensity = 0.05f;
        {
            auto scene = shadowScene();
            Renderer renderer(window, scene);
            frame("shadow_off", renderer, scene);
            settings.shadows = true;
            for (const auto [distance, name] : {std::pair{30.0f, "shadow_cascade_0"},
                    {12.0f, "shadow_cascade_1"}, {6.0f, "shadow_cascade_2"}}) {
                settings.shadowDistance = distance;
                frame(name, renderer, scene);
            }
            settings.shadowDistance = 12;
            settings.shadowStrength = 0;
            frame("shadow_strength_zero", renderer, scene);
            settings.shadowStrength = 1;
            auto& caster = scene.materials[1];
            caster.alphaMode = AlphaMode::Mask;
            caster.alphaCutoff = 0.5f;
            frame("shadow_mask_discard", renderer, scene);
            caster.alphaCutoff = 0.02f;
            frame("shadow_mask_keep", renderer, scene);
            caster.alphaCutoff = 0.1f;
            frame("shadow_texture_cutout", renderer, scene);
            settings.baseColorTextures = false;
            frame("shadow_texture_disabled", renderer, scene);
            caster.alphaCutoff = 0.2f;
            frame("shadow_vertex_cutout", renderer, scene);
            settings.vertexColors = false;
            frame("shadow_vertex_disabled", renderer, scene);
            caster.alphaMode = AlphaMode::Blend;
            frame("shadow_blend_caster", renderer, scene);
            caster.alphaMode = AlphaMode::Opaque;
            scene.nodes[1].local.scale.x = -0.25f;
            frame("shadow_mirrored", renderer, scene);
            caster.doubleSided = true;
            scene.nodes[1].local.rotation = glm::angleAxis(glm::pi<float>(), glm::vec3(0,1,0));
            frame("shadow_double_back", renderer, scene);
            scene.materials[0].alphaMode = AlphaMode::Blend;
            frame("shadow_blend_receiver", renderer, scene);
            SDL_SetWindowSize(window, 320, 240);
            SDL_Delay(50);
            frame("shadow_resized", renderer, scene);
            SDL_SetWindowSize(window, 256, 256);
            SDL_Delay(50);
            settings.shadowDistance = 2;
            frame("shadow_out_of_range", renderer, scene);
            settings.shadowDistance = 12;
            scene.nodes[1].local.translation.x = -2.5f;
            frame("shadow_caster_moved", renderer, scene);
            scene.nodes[1].local.translation.x = -1.2f;
            frame("shadow_caster_returned", renderer, scene);
            settings.worldLightDirection.x = -0.8f;
            frame("shadow_light_moved", renderer, scene);
            settings.worldLightDirection.x = 0.8f;
            camera.position.z = 4;
            frame("shadow_camera_moved", renderer, scene);
            camera.position.z = 3;
            scene.materials[0].emissive = {0.1f,0,0};
            frame("shadow_emissive", renderer, scene);
            scene.materials[0].emissive = {0,0,0};
            settings.showShadowCascades = true;
            frame("shadow_debug", renderer, scene);
            settings.showShadowCascades = false;
            settings.worldLightEnabled = false;
            frame("shadow_light_disabled", renderer, scene);
        }
        settings.worldLightEnabled = true;
        settings.baseColorTextures = true;
        settings.vertexColors = true;
        for (const auto topology : {PrimitiveTopology::TriangleStrip, PrimitiveTopology::TriangleFan}) {
            auto scene = shadowScene();
            scene.primitives[1].topology = topology;
            scene.primitives[1].indices = topology == PrimitiveTopology::TriangleStrip ?
                std::vector<std::uint32_t>{0,1,3,2} : std::vector<std::uint32_t>{0,1,2,3};
            Renderer renderer(window, scene);
            frame(topology == PrimitiveTopology::TriangleStrip ? "shadow_strip" : "shadow_fan", renderer, scene);
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
