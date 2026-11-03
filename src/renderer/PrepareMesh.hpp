#pragma once
#include "scene/Scene.hpp"
#include <stdexcept>

inline MeshPrimitive prepareMesh(const MeshPrimitive& source) {
    if (source.topology != PrimitiveTopology::Triangles && source.topology != PrimitiveTopology::TriangleStrip &&
        source.topology != PrimitiveTopology::TriangleFan)
        throw std::runtime_error("Rendering points and lines is not yet supported");
    if (source.hasNormals) return source;
    MeshPrimitive result = source;
    std::vector<std::uint32_t> triangles;
    if (source.topology == PrimitiveTopology::Triangles) triangles = source.indices;
    else {
        if (source.indices.size() < 3 || source.indices.size() - 2 > UINT32_MAX / 3)
            throw std::runtime_error("Invalid expanded triangle count");
        triangles.reserve((source.indices.size() - 2) * 3);
        for (std::size_t i = 2; i < source.indices.size(); ++i) {
            if (source.topology == PrimitiveTopology::TriangleFan)
                triangles.insert(triangles.end(), {source.indices[0], source.indices[i-1], source.indices[i]});
            else if (i % 2)
                triangles.insert(triangles.end(), {source.indices[i-1], source.indices[i-2], source.indices[i]});
            else
                triangles.insert(triangles.end(), {source.indices[i-2], source.indices[i-1], source.indices[i]});
        }
    }
    if (triangles.size() > UINT32_MAX || triangles.size() % 3) throw std::runtime_error("Invalid triangle count");
    result.vertices.clear();
    result.indices.clear();
    result.texCoords.clear();
    result.colors.clear();
    result.vertices.reserve(triangles.size());
    for (std::size_t i = 0; i < triangles.size(); i += 3) {
        const auto a = source.vertices.at(triangles[i]).position;
        const auto b = source.vertices.at(triangles[i+1]).position;
        const auto c = source.vertices.at(triangles[i+2]).position;
        const auto cross = glm::cross(b-a, c-a);
        const auto normal = glm::length(cross) > 1e-8f ? glm::normalize(cross) : glm::vec3(0,1,0);
        for (std::size_t j = 0; j < 3; ++j) {
            const auto index = triangles[i+j];
            auto vertex = source.vertices.at(index);
            vertex.normal = normal;
            vertex.tangent = {}; // Authored tangents require authored normals.
            result.indices.push_back(static_cast<std::uint32_t>(result.vertices.size()));
            result.vertices.push_back(vertex);
            for (const auto& [set, uv] : source.texCoords) result.texCoords[set].push_back(uv.at(index));
            for (const auto& [set, colors] : source.colors) result.colors[set].push_back(colors.at(index));
        }
    }
    result.topology = PrimitiveTopology::Triangles;
    result.hasNormals = true;
    return result;
}
