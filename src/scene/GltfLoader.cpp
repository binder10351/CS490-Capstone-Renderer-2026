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
    scene.materials[0].baseColor = glm::vec4(1);
    scene.materials[0].metallic = 1;
    for (const auto& source : model.materials) {
        SceneMaterial material;
        const auto& pbr = source.pbrMetallicRoughness;
        material.baseColor = glm::vec4(pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2], pbr.baseColorFactor[3]);
        material.metallic = static_cast<float>(pbr.metallicFactor);
        material.roughness = static_cast<float>(pbr.roughnessFactor);
        material.emissive = glm::vec3(source.emissiveFactor[0], source.emissiveFactor[1], source.emissiveFactor[2]);
        if (source.normalTexture.index >= 0) {
            const auto index = static_cast<std::size_t>(source.normalTexture.index);
            if (index >= model.textures.size()) invalid("invalid normal texture index");
            const int imageIndex = model.textures[index].source;
            if (imageIndex < 0 || static_cast<std::size_t>(imageIndex) >= model.images.size()) invalid("invalid normal image index");
            const auto& image = model.images[imageIndex];
            if (image.width <= 0 || image.height <= 0 || image.bits != 8 ||
                image.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE || image.component < 3 || image.component > 4)
                invalid("normal maps must decode to RGB/RGBA 8-bit images");
            const auto pixels = static_cast<std::size_t>(image.width) * image.height;
            if (pixels > std::numeric_limits<std::size_t>::max() / 4 || image.image.size() / image.component != pixels)
                invalid("invalid normal image size");
            material.normalMap.width = static_cast<std::uint32_t>(image.width);
            material.normalMap.height = static_cast<std::uint32_t>(image.height);
            material.normalMap.rgba.resize(pixels * 4);
            for (std::size_t i = 0; i < pixels; ++i) {
                for (int c = 0; c < 3; ++c) material.normalMap.rgba[i*4+c] = image.image[i*image.component+c];
                material.normalMap.rgba[i*4+3] = 255;
            }
            material.normalScale = static_cast<float>(source.normalTexture.scale);
            material.normalTexCoord = source.normalTexture.texCoord;
            if (!std::isfinite(material.normalScale) || material.normalTexCoord < 0) invalid("invalid normal texture parameters");
            if (!source.normalTexture.extensions.empty()) invalid("normal texture extensions are not supported");
        }
        scene.materials.push_back(material);
    }
    std::vector<std::vector<std::size_t>> meshes(model.meshes.size());
    for (std::size_t m = 0; m < model.meshes.size(); ++m) {
        for (const auto& source : model.meshes[m].primitives) {
            if (source.mode != -1 && source.mode != TINYGLTF_MODE_TRIANGLES) invalid("only triangle primitives are supported");
            if (!source.targets.empty()) invalid("morph targets are not yet supported");
            const auto position = source.attributes.find("POSITION");
            if (position == source.attributes.end()) invalid("primitive has no POSITION");
            const auto positions = accessorData(model, position->second, TINYGLTF_TYPE_VEC3);
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
                const auto values = accessorData(model, normal->second, TINYGLTF_TYPE_VEC3);
                if (values.size() != positions.size()) invalid("normal count differs from positions");
                for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
                    mesh.vertices[i].normal = glm::vec3(values[i*3], values[i*3+1], values[i*3+2]);
            }
            if (source.material < -1 || source.material >= static_cast<int>(model.materials.size())) invalid("invalid material");
            mesh.material = static_cast<std::size_t>(source.material + 1);
            const auto& material = scene.materials[mesh.material];
            const bool hasNormalMap = !material.normalMap.rgba.empty();
            const auto uv = source.attributes.find("TEXCOORD_" + std::to_string(hasNormalMap ? material.normalTexCoord : 0));
            if (hasNormalMap && uv == source.attributes.end()) invalid("normal map references missing texture coordinates");
            if (uv != source.attributes.end()) {
                const auto values = accessorData(model, uv->second, TINYGLTF_TYPE_VEC2);
                if (values.size() / 2 != mesh.vertices.size()) invalid("UV count differs from positions");
                for (std::size_t i = 0; i < mesh.vertices.size(); ++i) mesh.vertices[i].uv = glm::vec2(values[i*2], values[i*2+1]);
            }
            const auto tangent = source.attributes.find("TANGENT");
            if (tangent != source.attributes.end()) {
                const auto values = accessorData(model, tangent->second, TINYGLTF_TYPE_VEC4);
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
                for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
                    auto& a = mesh.vertices[mesh.indices[i]];
                    auto& b = mesh.vertices[mesh.indices[i+1]];
                    auto& c = mesh.vertices[mesh.indices[i+2]];
                    const auto n = glm::cross(b.position - a.position, c.position - a.position);
                    a.normal += n; b.normal += n; c.normal += n;
                }
            }
            for (auto& v : mesh.vertices) v.normal = glm::length(v.normal) > 1e-8f ? glm::normalize(v.normal) : glm::vec3(0,1,0);
            if (source.material < -1 || source.material >= static_cast<int>(model.materials.size())) invalid("invalid material");
            mesh.material = static_cast<std::size_t>(source.material + 1);
            meshes[m].push_back(scene.primitives.size());
            scene.primitives.push_back(std::move(mesh));
        }
    }
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
            node.primitives = meshes[source.mesh];
        }
        const auto mappedIndex = static_cast<int>(scene.nodes.size());
        scene.nodes.push_back(std::move(node));
        for (auto child : source.children) visit(child, mappedIndex);
    };
    for (auto root : model.scenes[sceneIndex].nodes) visit(root, -1);
    scene.updateWorldTransforms();
    if (!scene.worldBounds().valid()) invalid("scene contains no renderable geometry");
    return scene;
}
