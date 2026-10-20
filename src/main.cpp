#include "app/Application.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        Application application(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{});
        return application.run();
    } catch (const std::exception& exception) {
        std::cerr << "Fatal error: " << exception.what() << '\n';
        return 1;
    }
}
