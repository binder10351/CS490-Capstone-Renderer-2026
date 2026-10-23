#include "GltfLoader.hpp"
#include <tiny_gltf.h>
#include <cstring>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace {
[[noreturn]] void invalid(const std::string& message) { throw std::runtime_error("glTF: " + message); }
const unsigned char* element(const tinygltf::Model& model, int viewIndex, std::size_t offset, std::size_t index,
    std::size_t width, std::size_t stride) {
    if (viewIndex < 0 || static_cast<std::size_t>(viewIndex) >= model.bufferViews.size()) invalid("invalid buffer view");
    const auto& view = model.bufferViews[viewIndex];
    if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= model.buffers.size()) invalid("invalid buffer");
    const auto& bytes = model.buffers[view.buffer].data;
    if (stride < width || offset > view.byteLength || width > view.byteLength - offset ||
        index > (view.byteLength - offset - width) / stride) invalid("accessor exceeds buffer view");
    const auto relative = offset + index * stride;
    if (view.byteOffset > bytes.size() || view.byteLength > bytes.size() - view.byteOffset) invalid("buffer view exceeds buffer");
    return bytes.data() + view.byteOffset + relative;
}
template<class T> T read(const unsigned char* data) { T value; std::memcpy(&value, data, sizeof(value)); return value; }
double component(const unsigned char* p, int type, bool normalized) {
    switch (type) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT: return read<float>(p);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return normalized ? read<std::uint8_t>(p) / 255.0 : read<std::uint8_t>(p);
    case TINYGLTF_COMPONENT_TYPE_BYTE: return normalized ? std::max(read<std::int8_t>(p) / 127.0, -1.0) : read<std::int8_t>(p);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return normalized ? read<std::uint16_t>(p) / 65535.0 : read<std::uint16_t>(p);
    case TINYGLTF_COMPONENT_TYPE_SHORT: return normalized ? std::max(read<std::int16_t>(p) / 32767.0, -1.0) : read<std::int16_t>(p);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: return read<std::uint32_t>(p);
    default: invalid("unsupported accessor component type");
    }
}
std::vector<double> accessorData(const tinygltf::Model& model, int accessorIndex, int expectedType) {
    if (accessorIndex < 0 || static_cast<std::size_t>(accessorIndex) >= model.accessors.size()) invalid("invalid accessor");
    const auto& a = model.accessors[accessorIndex];
    if (a.type != expectedType) invalid("unexpected accessor shape");
    const int components = tinygltf::GetNumComponentsInType(a.type);
    const int bytes = tinygltf::GetComponentSizeInBytes(a.componentType);
    if (components <= 0 || bytes <= 0 || a.count > std::numeric_limits<std::size_t>::max() / components) invalid("invalid accessor size");
    const auto width = static_cast<std::size_t>(components * bytes);
    std::vector<double> result(a.count * components, 0);
    auto decode = [&](const unsigned char* p, std::size_t index) {
        for (int c = 0; c < components; ++c) {
            const auto value = component(p + c * bytes, a.componentType, a.normalized);
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) invalid("non-finite vertex data");
            result[index * components + c] = value;
        }
    };
    if (a.bufferView >= 0) {
        if (static_cast<std::size_t>(a.bufferView) >= model.bufferViews.size()) invalid("invalid accessor view");
        const auto stride = model.bufferViews[a.bufferView].byteStride;
        for (std::size_t i = 0; i < a.count; ++i)
            decode(element(model, a.bufferView, a.byteOffset, i, width, stride ? stride : width), i);
    }
    if (a.sparse.isSparse) {
        const auto& sparse = a.sparse;
        const int indexType = sparse.indices.componentType;
        if (indexType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE && indexType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
            indexType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) invalid("invalid sparse index type");
        const auto indexBytes = static_cast<std::size_t>(tinygltf::GetComponentSizeInBytes(indexType));
        if (sparse.count < 0 || static_cast<std::size_t>(sparse.count) > a.count) invalid("invalid sparse count");
        std::size_t previous = 0;
        for (int i = 0; i < sparse.count; ++i) {
            const auto index = static_cast<std::size_t>(component(element(model, sparse.indices.bufferView,
                sparse.indices.byteOffset, i, indexBytes, indexBytes), indexType, false));
            if (index >= a.count || (i && index <= previous)) invalid("invalid sparse index");
            previous = index;
            decode(element(model, sparse.values.bufferView, sparse.values.byteOffset, i, width, width), index);
        }
    }
    return result;
}
std::vector<double> vertexData(const tinygltf::Model& model, int accessor, int shape, bool allowNormalized = false) {
    if (accessor < 0 || accessor >= static_cast<int>(model.accessors.size())) invalid("invalid vertex accessor");
    const auto& source = model.accessors[accessor];
    const bool floating = source.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && !source.normalized;
    const bool normalized = allowNormalized && source.normalized &&
        (source.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE || source.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
    if (!floating && !normalized) invalid("invalid vertex attribute component type");
    return accessorData(model, accessor, shape);
}
template<class Info>
SceneTexture textureData(const tinygltf::Model& model, const Info& info) {
    if (info.index < -1 || info.index >= static_cast<int>(model.textures.size())) invalid("invalid texture index");
    if (info.texCoord < 0) invalid("invalid texture coordinate set");
    return {info.index, info.texCoord};
}
SceneSampler samplerData(const tinygltf::Sampler& sampler) {
    SceneSampler result;
    result.magFilter = sampler.magFilter < 0 ? 9729 : sampler.magFilter;
    result.minFilter = sampler.minFilter < 0 ? 9987 : sampler.minFilter;
    result.wrapS = sampler.wrapS;
    result.wrapT = sampler.wrapT;
    if (result.magFilter != 9728 && result.magFilter != 9729) invalid("invalid magnification filter");
    if (result.minFilter != 9728 && result.minFilter != 9729 &&
        (result.minFilter < 9984 || result.minFilter > 9987)) invalid("invalid minification filter");
    for (int wrap : {result.wrapS, result.wrapT})
        if (wrap != 33071 && wrap != 33648 && wrap != 10497) invalid("invalid texture wrapping");
    return result;
}
SceneImage imageData(const tinygltf::Image& image) {
    SceneImage result;
    result.name = image.name;
    const bool byteImage = image.bits == 8 && image.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    const bool shortImage = image.bits == 16 && image.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
    if (image.width <= 0 || image.height <= 0 || (!byteImage && !shortImage) || image.component < 1 || image.component > 4)
        invalid("textures must decode to 8-bit or 16-bit images");
    result.bits = image.bits;
    const std::size_t sampleBytes = static_cast<std::size_t>(image.bits / 8);
    const auto width = static_cast<std::size_t>(image.width), height = static_cast<std::size_t>(image.height);
    if (width > std::numeric_limits<std::size_t>::max() / height / 4 / sampleBytes) invalid("texture is too large");
    const auto pixels = width * height;
    if (image.image.size() != pixels * image.component * sampleBytes) invalid("invalid image size");
    result.width = static_cast<std::uint32_t>(width);
    result.height = static_cast<std::uint32_t>(height);
    result.rgba.resize(pixels * 4 * sampleBytes, 255);
    for (std::size_t i = 0; i < pixels; ++i) {
        const auto* pixel = image.image.data() + i * image.component * sampleBytes;
        auto* rgba = result.rgba.data() + i * 4 * sampleBytes;
        for (int c = 0; c < 3; ++c)
            std::memcpy(rgba + c * sampleBytes, pixel + (image.component < 3 ? 0 : c) * sampleBytes, sampleBytes);
        if (image.component == 2 || image.component == 4)
            std::memcpy(rgba + 3 * sampleBytes, pixel + (image.component - 1) * sampleBytes, sampleBytes);
    }
    return result;
}
void unit(double value, const char* field) {
    if (!std::isfinite(value) || value < 0 || value > 1) invalid(std::string("invalid ") + field);
}

}

Scene GltfLoader::load(const std::filesystem::path& filePath) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string error, warning;
    auto extension = filePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool binary = extension == ".glb";
    const bool ok = binary ? loader.LoadBinaryFromFile(&model, &error, &warning, filePath.string()) :
        loader.LoadASCIIFromFile(&model, &error, &warning, filePath.string());
    if (!ok) invalid("could not load " + filePath.string() + ": " + error);
    if (!model.extensionsRequired.empty()) invalid("required extensions are not supported: " + model.extensionsRequired.front());
    Scene scene;
    scene.unsupportedExtensions = model.extensionsUsed;
    for (const auto& name : scene.unsupportedExtensions)
        scene.warnings.push_back("Unsupported optional glTF extension: " + name + "; using core fallback.");
    if (!warning.empty()) scene.warnings.push_back(warning);
    for (const auto& image : model.images) scene.images.push_back(imageData(image));
    for (const auto& sampler : model.samplers) scene.samplers.push_back(samplerData(sampler));
    for (const auto& source : model.textures) {
        if (source.source < 0 || source.source >= static_cast<int>(scene.images.size())) invalid("invalid texture image");
        if (source.sampler < -1 || source.sampler >= static_cast<int>(model.samplers.size())) invalid("invalid texture sampler");
        scene.textures.push_back({source.name, static_cast<std::size_t>(source.source),
            source.sampler});
    }
    for (const auto& source : model.materials) {
        SceneMaterial material;
        material.name = source.name;
        material.alphaCutoff = static_cast<float>(source.alphaCutoff);
        if (!std::isfinite(source.alphaCutoff) || source.alphaCutoff < 0 || !std::isfinite(material.alphaCutoff)) invalid("invalid alpha cutoff");
        if (source.alphaMode == "OPAQUE") material.alphaMode = AlphaMode::Opaque;
        else if (source.alphaMode == "MASK") material.alphaMode = AlphaMode::Mask;
        else if (source.alphaMode == "BLEND") material.alphaMode = AlphaMode::Blend;
        else invalid("invalid alpha mode");
        material.doubleSided = source.doubleSided;
        const auto& pbr = source.pbrMetallicRoughness;
        if (pbr.baseColorFactor.size() != 4 || source.emissiveFactor.size() != 3) invalid("invalid material factor dimensions");
        for (double v : pbr.baseColorFactor) unit(v, "base color factor");
        for (double v : source.emissiveFactor) unit(v, "emissive factor");
        unit(pbr.metallicFactor, "metallic factor");
        unit(pbr.roughnessFactor, "roughness factor");
        material.baseColor = glm::vec4(pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2], pbr.baseColorFactor[3]);
        material.metallic = static_cast<float>(pbr.metallicFactor);
        material.roughness = static_cast<float>(pbr.roughnessFactor);
        material.emissive = glm::vec3(source.emissiveFactor[0], source.emissiveFactor[1], source.emissiveFactor[2]);
        material.baseColorMap = textureData(model, pbr.baseColorTexture);
        material.metallicRoughnessMap = textureData(model, pbr.metallicRoughnessTexture);
        material.occlusionMap = textureData(model, source.occlusionTexture);
        material.normalMap = textureData(model, source.normalTexture);
        material.occlusionStrength = static_cast<float>(source.occlusionTexture.strength);
        material.normalScale = static_cast<float>(source.normalTexture.scale);
        material.emissiveMap = textureData(model, source.emissiveTexture);
        if (!std::isfinite(material.normalScale) || !std::isfinite(material.occlusionStrength) ||
            material.occlusionStrength < 0 || material.occlusionStrength > 1) invalid("invalid texture strength");
        scene.materials.push_back(std::move(material));
    }
    std::vector<std::vector<std::size_t>> meshes(model.meshes.size());
    for (std::size_t m = 0; m < model.meshes.size(); ++m) {
        for (const auto& source : model.meshes[m].primitives) {
            if (source.mode != -1 && source.mode != TINYGLTF_MODE_TRIANGLES) invalid("only triangle primitives are supported");
            if (!source.targets.empty()) invalid("morph targets are not yet supported");
            const auto position = source.attributes.find("POSITION");
            if (position == source.attributes.end()) invalid("primitive has no POSITION");
            const auto positions = vertexData(model, position->second, TINYGLTF_TYPE_VEC3);
            MeshPrimitive mesh;
            mesh.vertices.resize(positions.size() / 3);
            if (mesh.vertices.empty() || mesh.vertices.size() > UINT32_MAX) invalid("invalid vertex count");
            for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
                mesh.vertices[i].position = glm::vec3(positions[i*3], positions[i*3+1], positions[i*3+2]);
                mesh.vertices[i].normal = glm::vec3(0);
                mesh.bounds.include(mesh.vertices[i].position);
            }
            const auto normal = source.attributes.find("NORMAL");
            if (normal != source.attributes.end()) {
                const auto values = vertexData(model, normal->second, TINYGLTF_TYPE_VEC3);
                if (values.size() != positions.size()) invalid("normal count differs from positions");
                for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
                    mesh.vertices[i].normal = glm::vec3(values[i*3], values[i*3+1], values[i*3+2]);
            }
            if (source.material < -1 || source.material >= static_cast<int>(model.materials.size())) invalid("invalid material");
            mesh.material = static_cast<std::size_t>(source.material + 1);
            const auto& material = scene.materials[mesh.material];
            for (const auto& [semantic, accessor] : source.attributes) {
                if (semantic.rfind("TEXCOORD_", 0) != 0) continue;
                const auto suffix = semantic.substr(9);
                if (suffix.empty() || (suffix.size() > 1 && suffix[0] == '0') || suffix.find_first_not_of("0123456789") != std::string::npos) invalid("invalid UV semantic");
                int set;
                try { set = std::stoi(suffix); } catch (...) { invalid("invalid UV set"); }
                const auto values = vertexData(model, accessor, TINYGLTF_TYPE_VEC2, true);
                if (values.size() / 2 != mesh.vertices.size()) invalid("UV count differs from positions");
                auto& uv = mesh.texCoords[set];
                for (std::size_t i = 0; i < mesh.vertices.size(); ++i) uv.emplace_back(values[i*2], values[i*2+1]);
            }
            auto loadUv = [&](const SceneTexture& texture, glm::vec2 Vertex::* member) {
                if (!texture.present()) return;
                const auto uv = mesh.texCoords.find(texture.texCoord);
                if (uv == mesh.texCoords.end()) invalid("texture references missing texture coordinates");
                for (std::size_t i = 0; i < mesh.vertices.size(); ++i) mesh.vertices[i].*member = uv->second[i];
            };
            loadUv(material.normalMap, &Vertex::uv);
            loadUv(material.baseColorMap, &Vertex::baseColorUv);
            loadUv(material.metallicRoughnessMap, &Vertex::metallicRoughnessUv);
            loadUv(material.occlusionMap, &Vertex::occlusionUv);
            loadUv(material.emissiveMap, &Vertex::emissiveUv);
            for (const auto& [semantic, accessor] : source.attributes) {
                if (semantic.rfind("COLOR_", 0) != 0) continue;
                const auto suffix = semantic.substr(6);
                if (suffix.empty() || (suffix.size() > 1 && suffix[0] == '0') || suffix.find_first_not_of("0123456789") != std::string::npos) invalid("invalid color semantic");
                int set;
                try { set = std::stoi(suffix); } catch (...) { invalid("invalid color set"); }
                if (accessor < 0 || accessor >= static_cast<int>(model.accessors.size())) invalid("invalid color accessor");
                const auto type = model.accessors[accessor].type;
                if (type != TINYGLTF_TYPE_VEC3 && type != TINYGLTF_TYPE_VEC4) invalid("invalid color shape");
                const auto values = vertexData(model, accessor, type, true);
                const std::size_t channels = type == TINYGLTF_TYPE_VEC3 ? 3 : 4;
                if (values.size() / channels != mesh.vertices.size()) invalid("color count differs from positions");
                auto& colors = mesh.colors[set];
                for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
                    glm::vec4 c(1);
                    for (std::size_t j = 0; j < channels; ++j) c[j] = static_cast<float>(values[i*channels+j]);
                    if (set == 0) c = glm::clamp(c, glm::vec4(0), glm::vec4(1));
                    colors.push_back(c);
                    if (set == 0) mesh.vertices[i].color = c;
                }
            }
            const auto tangent = source.attributes.find("TANGENT");
            if (tangent != source.attributes.end()) {
                const auto values = vertexData(model, tangent->second, TINYGLTF_TYPE_VEC4);
                if (values.size() / 4 != mesh.vertices.size()) invalid("tangent count differs from positions");
                for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
                    const glm::vec4 t(values[i*4], values[i*4+1], values[i*4+2], values[i*4+3]);
                    if (glm::length(glm::vec3(t)) < 1e-8f || (t.w != 1.0f && t.w != -1.0f)) invalid("invalid tangent");
                    mesh.vertices[i].tangent = t;
                }
            }
            if (source.indices >= 0) {
                if (static_cast<std::size_t>(source.indices) >= model.accessors.size()) invalid("invalid index accessor");
                const auto& a = model.accessors[source.indices];
                if (a.normalized || (a.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
                    a.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT && a.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)) invalid("invalid index type");
                for (auto value : accessorData(model, source.indices, TINYGLTF_TYPE_SCALAR)) {
                    if (value < 0 || value >= mesh.vertices.size()) invalid("index exceeds vertex count");
                    mesh.indices.push_back(static_cast<std::uint32_t>(value));
                }
            } else {
                for (std::uint32_t i = 0; i < mesh.vertices.size(); ++i) mesh.indices.push_back(i);
            }
            if (mesh.indices.empty() || mesh.indices.size() % 3 || mesh.indices.size() > UINT32_MAX) invalid("invalid triangle index count");
            if (normal == source.attributes.end()) {
                // glTF requires flat normals when NORMAL is absent. Split indexed
                // corners, including all retained UV sets, so edges stay sharp.
                std::vector<Vertex> corners;
                std::map<int, std::vector<glm::vec2>> cornerUvs;
                std::map<int, std::vector<glm::vec4>> cornerColors;
                corners.reserve(mesh.indices.size());
                for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
                    const auto a = mesh.vertices[mesh.indices[i]].position;
                    const auto b = mesh.vertices[mesh.indices[i+1]].position;
                    const auto c = mesh.vertices[mesh.indices[i+2]].position;
                    const auto n = glm::cross(b-a, c-a);
                    for (std::size_t j = 0; j < 3; ++j) {
                        const auto index = mesh.indices[i+j];
                        auto v = mesh.vertices[index];
                        v.normal = n;
                        v.tangent = {}; // Authored tangents require authored normals.
                        corners.push_back(v);
                        for (const auto& [set, uv] : mesh.texCoords) cornerUvs[set].push_back(uv[index]);
                        for (const auto& [set, colors] : mesh.colors) cornerColors[set].push_back(colors[index]);
                    }
                }
                mesh.vertices = std::move(corners);
                mesh.texCoords = std::move(cornerUvs);
                mesh.colors = std::move(cornerColors);
                for (std::size_t i = 0; i < mesh.indices.size(); ++i) mesh.indices[i] = static_cast<std::uint32_t>(i);
            }
            for (auto& v : mesh.vertices) v.normal = glm::length(v.normal) > 1e-8f ? glm::normalize(v.normal) : glm::vec3(0,1,0);
            meshes[m].push_back(scene.primitives.size());
            scene.primitives.push_back(std::move(mesh));
        }
    }
    for (std::size_t i = 0; i < meshes.size(); ++i) scene.meshes.push_back({model.meshes[i].name, meshes[i]});
    if (!model.animations.empty()) scene.warnings.push_back("Animations are not supported; displaying the authored static pose.");
    if (model.scenes.empty()) invalid("no scene");
    const auto sceneIndex = model.defaultScene < 0 ? 0 : model.defaultScene;
    if (static_cast<std::size_t>(sceneIndex) >= model.scenes.size()) invalid("invalid default scene");
    std::vector<bool> visited(model.nodes.size(), false);
    std::function<void(int,int)> visit = [&](int index, int parent) {
        if (index < 0 || static_cast<std::size_t>(index) >= model.nodes.size() || visited[index]) invalid("invalid or repeated node in hierarchy");
        visited[index] = true;
        const auto& source = model.nodes[index];
        if (source.skin >= 0) invalid("skinning is not yet supported");
        if ((!source.translation.empty() && source.translation.size() != 3) ||
            (!source.scale.empty() && source.scale.size() != 3) ||
            (!source.rotation.empty() && source.rotation.size() != 4)) invalid("invalid node TRS dimensions");
        SceneNode node;
        node.name = source.name;
        node.parent = parent;
        if (!source.matrix.empty()) {
            if (!source.translation.empty() || !source.rotation.empty() || !source.scale.empty())
                invalid("node cannot define both matrix and TRS");
            if (source.matrix.size() != 16) invalid("invalid node matrix");
            glm::mat4 matrix(1);
            for (int i = 0; i < 16; ++i) glm::value_ptr(matrix)[i] = static_cast<float>(source.matrix[i]);
            node.local.authoredMatrix = matrix;
        } else {
            if (!source.translation.empty()) node.local.translation = glm::vec3(source.translation[0],source.translation[1],source.translation[2]);
            if (!source.scale.empty()) node.local.scale = glm::vec3(source.scale[0],source.scale[1],source.scale[2]);
            if (!source.rotation.empty()) {
                node.local.rotation = glm::quat(source.rotation[3],source.rotation[0],source.rotation[1],source.rotation[2]);
                if (glm::length(node.local.rotation) < 1e-8f) invalid("zero rotation quaternion");
            }
        }
        const auto local = node.local.matrix();
        for (int i = 0; i < 16; ++i)
            if (!std::isfinite(glm::value_ptr(local)[i])) invalid("non-finite node transform");
        if (local[0][3] != 0 || local[1][3] != 0 || local[2][3] != 0 || local[3][3] != 1)
            invalid("node transform must be affine");
        if (source.mesh >= 0) {
            if (static_cast<std::size_t>(source.mesh) >= meshes.size()) invalid("invalid mesh reference");
            node.mesh = source.mesh;
            node.primitives = meshes[source.mesh];
        }
        const auto mappedIndex = static_cast<int>(scene.nodes.size());
        scene.nodes.push_back(std::move(node));
        if (parent < 0) scene.roots.push_back(mappedIndex);
        else scene.nodes[parent].children.push_back(mappedIndex);
        for (auto child : source.children) visit(child, mappedIndex);
    };
    for (auto root : model.scenes[sceneIndex].nodes) visit(root, -1);
    scene.updateWorldTransforms();
    if (!scene.worldBounds().valid()) invalid("scene contains no renderable geometry");
    return scene;
}
