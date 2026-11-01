#include "pc/pc_shader_loading.h"
#include "pc/pc_menu_language.h"
#include "pc/pc_menu_widgets.h"
#include "pc/pc_ui_style.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

void pc_ps5_shader_loading(uint32_t done, uint32_t total, double secondsRemaining) {
    if (!ImGui::GetCurrentContext()) return;
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    if (size.x <= 0.f || size.y <= 0.f) return;
    auto* draw = ImGui::GetForegroundDrawList();
    const auto color = [](const ImVec4& value) { return ImGui::ColorConvertFloat4ToU32(value); };
    const float scale = std::min(size.x / 1920.f, size.y / 1080.f);
    const float center = size.x * .5f;
    const float width = 1080.f * scale;
    const float left = center - width * .5f;
    const float top = size.y * .5f - 325.f * scale;
    const auto centered = [&](const char* text, float y, float fontSize, ImU32 ink) {
        // Fit translated strings without clipping on narrower displays.
        float textWidth = ImGui::GetFont()->CalcTextSizeA(fontSize, 1e6f, 0.f, text).x;
        if (textWidth > width) {
            fontSize *= width / textWidth;
            textWidth = width;
        }
        draw->AddText(ImGui::GetFont(), fontSize, ImVec2(center - textWidth * .5f, y), ink, text);
    };
    draw->AddRectFilledMultiColor(ImVec2(0, 0), size,
        color(pc::ui::background), color(pc::ui::background),
        color(pc::ui::surface), color(pc::ui::surface));
    // A quiet horizon and broad wave strokes frame the content without new textures.
    const float horizon = size.y * .77f;
    const ImU32 wave = color(ImVec4(.18f, .40f, .46f, .14f));
    for (int i = 0; i < 3; ++i) {
        const float y = horizon + (48.f * i) * scale;
        draw->AddBezierCubic(ImVec2(-size.x * .1f, y),
            ImVec2(size.x * .3f, y - 140.f * scale),
            ImVec2(size.x * .7f, y + 160.f * scale),
            ImVec2(size.x * 1.1f, y - 35.f * scale), wave, 2.f * scale, 48);
    }
    // Reuse the options menu's vector mark; no images or game resources to load.
    pc_ps5_logo(draw, ImVec2(center - 90.f * scale, top), 120.f * scale);
    centered("ProsperoWake", top + 190.f * scale, 64.f * scale, color(pc::ui::text));
    centered(pc_menu_text("Preparing shaders"), top + 304.f * scale,
             40.f * scale, color(pc::ui::text));
    centered(pc_menu_text("Cached shaders make future launches faster."),
             top + 367.f * scale, 28.f * scale, color(pc::ui::muted));

    const float barY = top + 494.f * scale;
    const float height = 14.f * scale;
    // Give the progress area its own surface and generous television-safe spacing.
    draw->AddRectFilled(ImVec2(left - 36.f * scale, barY - 29.f * scale),
        ImVec2(left + width + 36.f * scale, barY + 132.f * scale),
        color(pc::ui::surface), 20.f * scale);
    draw->AddRect(ImVec2(left - 36.f * scale, barY - 29.f * scale),
        ImVec2(left + width + 36.f * scale, barY + 132.f * scale),
        color(pc::ui::border), 20.f * scale);
    draw->AddRectFilled(ImVec2(left, barY), ImVec2(left + width, barY + height),
                        color(pc::ui::border), height * .5f);
    const uint32_t completed = std::min(done, total);
    const float progress = total ? float(completed) / float(total) : 0.f;
    if (progress > 0.f)
        draw->AddRectFilled(ImVec2(left, barY), ImVec2(left + width * progress, barY + height),
                            color(pc::ui::accent), height * .5f);
    char count[96];
    if (total) {
        std::snprintf(count, sizeof(count), "%u / %u", unsigned(completed), unsigned(total));
        char percent[24];
        std::snprintf(percent, sizeof(percent), "%u%%", unsigned(progress * 100.f));
        const float fontSize = 44.f * scale;
        const float textWidth = ImGui::GetFont()->CalcTextSizeA(fontSize, 1e6f, 0.f, percent).x;
        draw->AddText(ImGui::GetFont(), fontSize, ImVec2(left + width - textWidth, barY + 43.f * scale),
                      color(pc::ui::accent), percent);
    } else {
        std::snprintf(count, sizeof(count), "%s", pc_menu_text("Starting…"));
    }
    draw->AddText(ImGui::GetFont(), 30.f * scale, ImVec2(left, barY + 51.f * scale),
                  color(pc::ui::muted), count);
    if (total && completed < total && std::isfinite(secondsRemaining) && secondsRemaining >= 0.) {
        // Estimates are approximate; avoid claiming a precise countdown.
        const unsigned seconds = static_cast<unsigned>(std::min(std::ceil(secondsRemaining), 359999.));
        char eta[128];
        std::snprintf(eta, sizeof(eta), pc_menu_text("About %u:%02u remaining"), seconds / 60, seconds % 60);
        centered(eta, top + 660.f * scale, 28.f * scale, color(pc::ui::muted));
    }
}
