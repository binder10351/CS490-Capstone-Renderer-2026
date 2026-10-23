// Color images are uploaded as sRGB; data images remain linear.
layout(set = 1, binding = 2) uniform sampler2D baseColorMap;
// glTF packs roughness in G and metallic in B. AO may use a separate image/UV set.
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D occlusionMap;

layout(set = 1, binding = 5) uniform sampler2D emissiveMap;
