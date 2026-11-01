#pragma once
#include <imgui.h>

// Shared PS5 menu/overlay palette. All decoration is drawn by ImGui.
namespace pc::ui {
inline const ImVec4 background{0.025f, 0.045f, 0.065f, 1.f};
inline const ImVec4 surface{0.045f, 0.075f, 0.10f, 1.f};
inline const ImVec4 selected{0.075f, 0.18f, 0.23f, 1.f};
inline const ImVec4 border{0.16f, 0.25f, 0.29f, 1.f};
inline const ImVec4 text{0.93f, 0.96f, 0.97f, 1.f};
inline const ImVec4 muted{0.64f, 0.73f, 0.77f, 1.f};
inline const ImVec4 accent{0.40f, 0.85f, 0.89f, 1.f};
inline const ImVec4 warm{1.f, 0.76f, 0.43f, 1.f};
}
