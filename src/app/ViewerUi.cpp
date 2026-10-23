#include "ViewerUi.hpp"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <stdexcept>

ViewerUi::ViewerUi(SDL_Window* window) {
    static_assert(IMGUI_VERSION_NUM >= 19280, "Update vcpkg: the UI requires Dear ImGui 1.92.8 or newer");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
        ImGui::DestroyContext();
        throw std::runtime_error("Could not initialize the SDL UI backend");
    }
}
ViewerUi::~ViewerUi() {
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}
void ViewerUi::processEvent(const SDL_Event& event) { ImGui_ImplSDL3_ProcessEvent(&event); }
bool ViewerUi::captures(const SDL_Event& event) const {
    const auto& io = ImGui::GetIO();
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL: return io.WantCaptureMouse;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT: return io.WantCaptureKeyboard;
    default: return false;
    }
}
bool ViewerUi::build(RenderSettings& settings, bool& showGuides) {
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    bool frame = false;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Lighting")) {
            ImGui::Checkbox("World light", &settings.worldLightEnabled);
            ImGui::SliderFloat("Light intensity", &settings.worldLightIntensity, 0.0f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::ColorEdit3("Light color", &settings.worldLightColor.x);
            ImGui::SliderFloat3("Light direction", &settings.worldLightDirection.x, -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::Separator();
            ImGui::SliderFloat("Ambient intensity", &settings.ambientIntensity, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Shading")) {
            ImGui::MenuItem("Normal mapping", nullptr, &settings.normalMapping);
            ImGui::MenuItem("Base color textures", nullptr, &settings.baseColorTextures);
            ImGui::MenuItem("Metallic / roughness textures", nullptr, &settings.metallicRoughnessTextures);
            ImGui::MenuItem("Ambient occlusion", nullptr, &settings.occlusion);
            ImGui::MenuItem("Emissive", nullptr, &settings.emissive);
            ImGui::MenuItem("Vertex colors", nullptr, &settings.vertexColors);
            ImGui::Separator();
            ImGui::MenuItem("Diffuse lighting", nullptr, &settings.diffuse);
            ImGui::MenuItem("Specular lighting", nullptr, &settings.specular);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Grid and axes", "G", &showGuides);
            frame = ImGui::MenuItem("Frame model", "F");
            ImGui::Separator();
            if (ImGui::MenuItem("Reset lighting / shading")) settings = RenderSettings{};
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    ImGui::Render();
    return frame;
}
ImDrawData* ViewerUi::drawData() const { return ImGui::GetDrawData(); }
