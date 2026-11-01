#pragma once
struct ImVec4;
struct ImVec2;
struct ImDrawList;
void pc_ps5_logo(ImDrawList* draw, ImVec2 position, float scale);
// Draw-only PS5 layout; menu state, actions and settings remain upstream-owned.
bool pc_ps5_gameplay_option_row(const char* label, const char* rawValue, bool category,
    bool selected, bool locked, const ImVec4& dim, const ImVec4& accent, bool adjustable = true);
// Vector section icons and a shared Xelu controller-prompt atlas.
bool pc_ps5_menu_tab(const char* label, int icon, bool selected, float width);
void pc_ps5_menu_footer(const char* confirm, const char* back, const char* tabs, const char* close);
void pc_ps5_menu_heading(const char* title);
void pc_ps5_menu_section(const char* title);
void pc_ps5_menu_style_begin(bool highContrast, float scale, float opacity);
void pc_ps5_menu_style_end();
void pc_ps5_menu_welcome(float alpha, bool highContrast, float textScale);
float pc_ps5_menu_text_height(const char* text, float size, float width);
void pc_ps5_menu_text(const char* text, const ImVec4& color);
