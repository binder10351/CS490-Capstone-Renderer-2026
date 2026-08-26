#pragma once

#include <filesystem>

class GltfLoader {
public:
    void load(const std::filesystem::path& filePath);
};
