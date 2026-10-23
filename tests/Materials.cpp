#include "scene/GltfLoader.hpp"
#include "renderer/GpuUniforms.hpp"
#include <json.hpp>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a-b) < 1e-5f; }
using Json = nlohmann::json;
struct TemporaryAsset {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("renderer-material-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".gltf");
    ~TemporaryAsset() { std::error_code error; std::filesystem::remove(path, error); }
    Scene load(const Json& value) const {
        { std::ofstream out(path); out << value; }
        return GltfLoader{}.load(path);
    }
};
template<class F> void rejects(const Json& original, F mutate) {
    auto value = original;
    mutate(value);
    bool rejected = false;
    try { (void)TemporaryAsset{}.load(value); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Invalid material or vertex input must be rejected");
}
}

int main() {
    try {
        for (const char* file : {"core_materials.gltf", "core_materials.glb"}) {
            auto scene = GltfLoader{}.load(std::filesystem::path(TEST_ASSET_DIR) / file);
            require(scene.images.size() == 1 && scene.textures.size() == 2 && scene.samplers.size() == 1,
                "Image and sampler storage must preserve sharing");
            require(scene.textures[0].image == scene.textures[1].image && scene.textures[1].sampler == -1,
                "Different texture/sampler combinations can reference one image");
            require(scene.images[0].rgba == std::vector<std::uint8_t>({200,128,240,255}), "Pixels survive parser destruction");
            require(scene.roots == std::vector<std::size_t>{0} && scene.nodes[0].children == std::vector<std::size_t>({1,2}),
                "Explicit roots and child lists preserve the hierarchy");
            require(scene.meshes.size() == 1 && scene.meshes[0].primitives == std::vector<std::size_t>({0,1}) &&
                scene.nodes[1].mesh == scene.nodes[2].mesh && scene.nodes[1].primitives == scene.nodes[2].primitives,
                "Mesh grouping and instances must reference shared primitives");
            const auto& material = scene.materials[1];
            require(material.name == "Core layered test" && material.alphaMode == AlphaMode::Mask &&
                close(material.alphaCutoff, 0.3f) && material.doubleSided, "Import core material metadata and raster settings");
            require(material.emissive == glm::vec3(0.1f,0.2f,0.3f) && material.emissiveMap.texture == 1 &&
                material.emissiveMap.texCoord == 2, "Emissive factor and independent texture binding");
            require(scene.materials[2].alphaMode == AlphaMode::Blend, "Import blend mode");
            for (std::size_t i : {0u, 3u}) {
                const auto& defaults = scene.materials[i];
                require(defaults.baseColor == glm::vec4(1) && defaults.metallic == 1 && defaults.roughness == 1 &&
                    defaults.emissive == glm::vec3(0) && defaults.normalScale == 1 && defaults.occlusionStrength == 1 &&
                    defaults.alphaMode == AlphaMode::Opaque && defaults.alphaCutoff == 0.5f && !defaults.doubleSided &&
                    !defaults.emissiveMap.present(), "Explicit and implicit materials use glTF defaults");
            }
            const auto& flat = scene.primitives[0];
            require(flat.vertices.size() == 6 && flat.texCoords.size() == 3 && flat.colors.size() == 2,
                "Retain all UV/color sets while splitting flat corners");
            require(flat.vertices[0].position == flat.vertices[3].position &&
                flat.vertices[0].normal == glm::vec3(0,0,1) && flat.vertices[3].normal == glm::vec3(0,1,0),
                "Shared positions must have per-face flat normals when NORMAL is absent");
            require(flat.vertices[0].tangent == glm::vec4(0), "Ignore tangents without authored normals");
            require(close(flat.vertices[0].color.g, 128.0f/255) && close(flat.vertices[0].color.a, 64.0f/255) &&
                flat.vertices[0].color == flat.vertices[3].color, "Normalized RGBA colors survive corner splitting");
            require(close(flat.vertices[1].uv.x, 128.0f/255) && flat.vertices[1].baseColorUv == glm::vec2(1,0) &&
                flat.vertices[1].emissiveUv == glm::vec2(0.3f,0.4f) && flat.texCoords.at(2)[3] == glm::vec2(0.1f,0.2f),
                "Select independent UV streams and preserve unused source sets");
            const auto& authored = scene.primitives[1];
            require(authored.vertices.size() == 4 && authored.vertices[0].tangent == glm::vec4(1,0,0,-1),
                "Authored normals and tangent handedness remain indexed");
            require(authored.vertices[0].color == glm::vec4(1,0.5f,0.25f,1), "RGB colors get opaque alpha");
        }
        const auto highPrecision = GltfLoader{}.load(std::filesystem::path(TEST_ASSET_DIR) / "core_materials_16.gltf");
        require(highPrecision.images[0].bits == 16 && highPrecision.images[0].rgba.size() == 8, "Preserve 16-bit PNG precision on the CPU");
        std::uint16_t red;
        std::memcpy(&red, highPrecision.images[0].rgba.data(), sizeof(red));
        require(red == 40000, "16-bit samples must remain unmodified");
        Json source;
        { std::ifstream in(std::filesystem::path(TEST_ASSET_DIR) / "core_materials.gltf"); in >> source; }
        rejects(source, [](Json& j) { j["materials"][0]["alphaMode"] = "INVALID"; });
        rejects(source, [](Json& j) { j["materials"][0]["alphaCutoff"] = -0.1; });
        rejects(source, [](Json& j) { j["materials"][0]["pbrMetallicRoughness"]["metallicFactor"] = 1.1; });
        rejects(source, [](Json& j) { j["materials"][0]["emissiveFactor"] = {0, -1, 0}; });
        rejects(source, [](Json& j) { j["materials"][0]["emissiveTexture"]["index"] = 99; });
        rejects(source, [](Json& j) { j["materials"][0]["emissiveTexture"]["texCoord"] = 7; });
        rejects(source, [](Json& j) { j["textures"][0]["source"] = 99; });
        rejects(source, [](Json& j) { j["textures"][0]["sampler"] = 99; });
        rejects(source, [](Json& j) { j["samplers"][0]["wrapS"] = 123; });
        rejects(source, [](Json& j) { j["accessors"][4]["count"] = 3; });
        rejects(source, [](Json& j) { j["accessors"][4]["normalized"] = false; });
        rejects(source, [](Json& j) { j["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_01"] = 1; });
        auto optional = source;
        optional["extensionsUsed"] = {"KHR_materials_clearcoat"};
        optional["materials"][0]["extensions"]["KHR_materials_clearcoat"]["clearcoatFactor"] = 1;
        const auto fallback = TemporaryAsset{}.load(optional);
        require(fallback.unsupportedExtensions == std::vector<std::string>{"KHR_materials_clearcoat"},
            "Expose ignored optional extensions to callers");
        rejects(optional, [](Json& j) { j["extensionsRequired"] = {"KHR_materials_clearcoat"}; });
        auto cutoff = source;
        cutoff["materials"][0]["alphaCutoff"] = 1.5;
        require(TemporaryAsset{}.load(cutoff).materials[1].alphaCutoff == 1.5f, "Cutoffs above one are valid");
        std::cout << "Core material, shared resource, UV/color and flat-normal checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
