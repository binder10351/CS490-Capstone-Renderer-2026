#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
constexpr float PI = 3.14159265358979323846f;
using std::sqrt;
// Compile the scalar GLSL functions directly, rather than testing a copy.
#include "../shaders/pbr_brdf.glsl"
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        for (float roughness : {0.04f, 0.1f, 0.2f, 0.5f, 1.0f}) {
            // Analytical GGX normal-incidence peak: 1 / (pi * roughness^4).
            const double expected = 1.0 / (double(PI) * std::pow(double(roughness), 4));
            const float peak = distributionGgx(1.0f, roughness);
            require(std::abs(peak / expected - 1.0) < 1e-5, "GGX peak must not be suppressed by a denominator clamp");
            require(std::abs(visibilityGgx(1.0f, 1.0f, roughness) - 0.25f) < 1e-6f,
                "Normal-incidence Smith visibility must be 1/4");
            for (float cosine : {0.0f, 0.001f, 0.1f, 0.5f, 0.99f, 1.0f}) {
                const float d = distributionGgx(cosine, roughness);
                const float v = visibilityGgx(cosine, 0.7f, roughness);
                require(std::isfinite(d) && d >= 0 && std::isfinite(v) && v >= 0, "BRDF must remain finite and nonnegative");
                require(std::abs(v - visibilityGgx(0.7f, cosine, roughness)) < 1e-6f,
                    "Smith visibility must be reciprocal");
            }
        }
        require(visibilityGgx(0, 0, 0.04f) == 0, "Zero visibility at the degenerate horizon");
        require(distributionGgx(1, 0.04f) > distributionGgx(1, 0.2f), "Smoother surfaces must have stronger peaks");
        require(distributionGgx(0.5f, 0.04f) < distributionGgx(0.5f, 0.2f), "Smoother surfaces must have narrower lobes");
        std::cout << "GGX peak, visibility, and numerical stability checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
