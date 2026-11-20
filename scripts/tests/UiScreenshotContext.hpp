#pragma once
#include "Core/Presentation/ImGuiLayer.hpp"
#include "Core/Presentation/EditorUiState.hpp"
#include <glad/gl.h>
#include <imgui.h>

namespace SST::Testing {
// Use the application's initialization, including configured theme and platform fonts.
class UiScreenshotContext {
    Ref<EditorUiState> state = CreateRef<EditorUiState>();
    ImGuiLayer layer;
public:
    explicit UiScreenshotContext(ImGuiLayerRuntime runtime) : layer(runtime) {
        state->appearance = runtime.appearance;
        state->selectedFontPath = runtime.ui.fontPath;
        layer.OnAttach();
        layer.BindUiState(state);
        // Keep every test window in the captured framebuffer, after applying the app's style.
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    }
    ~UiScreenshotContext() { layer.OnDetach(); }
    void Render() {
        const auto &colour = state->appearance.clearColor;
        glClearColor(colour[0], colour[1], colour[2], colour[3]);
        glClear(GL_COLOR_BUFFER_BIT);
        layer.Render();
    }
};
}
