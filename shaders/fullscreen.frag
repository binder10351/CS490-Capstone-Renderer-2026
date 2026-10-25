#version 450

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D hdrScene;
layout(push_constant) uniform PostProcess {
    float exposure; // EV stops
    float toneMapping;
    float fxaa;
    float encodeSrgb; // Zero for sRGB attachments, which encode automatically.
} settings;

vec3 displayColor(vec2 uv) {
    vec3 color = max(texture(hdrScene, uv).rgb, vec3(0.0)) * exp2(settings.exposure);
    if (settings.toneMapping > 0.5) {
        // ACES fitted curve (approximation), applied to linear scene radiance.
        color = (color * (2.51 * color + 0.03)) /
                (color * (2.43 * color + 0.59) + 0.14);
    }
    return clamp(color, 0.0, 1.0);
}

float luma(vec3 color) {
    // Perceptual luma for FXAA edge detection after exposure and tone mapping.
    return sqrt(dot(color, vec3(0.299, 0.587, 0.114)));
}

vec3 antialiasedColor() {
    vec3 center = displayColor(inUv);
    if (settings.fxaa < 0.5) return center;
    vec2 pixel = 1.0 / vec2(textureSize(hdrScene, 0));
    float nw = luma(displayColor(inUv + vec2(-1.0, -1.0) * pixel));
    float ne = luma(displayColor(inUv + vec2( 1.0, -1.0) * pixel));
    float sw = luma(displayColor(inUv + vec2(-1.0,  1.0) * pixel));
    float se = luma(displayColor(inUv + vec2( 1.0,  1.0) * pixel));
    float middle = luma(center);
    float low = min(middle, min(min(nw, ne), min(sw, se)));
    float high = max(middle, max(max(nw, ne), max(sw, se)));
    if (high - low < max(0.0312, high * 0.125)) return center;

    vec2 direction = vec2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    float reduction = max((nw + ne + sw + se) * (0.25 / 8.0), 1.0 / 128.0);
    direction = clamp(direction / (min(abs(direction.x), abs(direction.y)) + reduction),
                      vec2(-8.0), vec2(8.0)) * pixel;
    vec3 a = 0.5 * (displayColor(inUv + direction * (1.0 / 3.0 - 0.5)) +
                    displayColor(inUv + direction * (2.0 / 3.0 - 0.5)));
    vec3 b = a * 0.5 + 0.25 * (displayColor(inUv - direction * 0.5) +
                               displayColor(inUv + direction * 0.5));
    float bLuma = luma(b);
    return bLuma < low || bLuma > high ? a : b;
}

vec3 linearToSrgb(vec3 color) {
    return mix(1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
               12.92 * color, lessThanEqual(color, vec3(0.0031308)));
}

void main() {
    vec3 color = antialiasedColor();
    if (settings.encodeSrgb > 0.5) color = linearToSrgb(color);
    outColor = vec4(color, 1.0);
}
