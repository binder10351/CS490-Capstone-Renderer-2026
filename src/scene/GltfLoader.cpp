#include "GltfLoader.hpp"

#include <stdexcept>

void GltfLoader::load(const std::filesystem::path& filePath) {
    // Keep tinygltf parsing here and return CPU-side scene data to the renderer.
    // This boundary keeps the loader independent of Vulkan and VMA.
    if (!std::filesystem::exists(filePath)) {
        throw std::runtime_error("glTF file does not exist: " + filePath.string());
    }
}
