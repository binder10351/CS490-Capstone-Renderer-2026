#pragma once
#include "Scene.hpp"
#include <filesystem>

class GltfLoader {
public:
    [[nodiscard]] Scene load(const std::filesystem::path& filePath);
};
