#pragma once
void pc_profiler_reset();
void pc_profiler_mode(const char* value);
void pc_profiler_position(const char* value);
void pc_profiler_scale(const char* value);
void pc_profiler_behind_menu(const char* value);
// Space beside the menu; zero for compact or behind-menu counters.
float pc_profiler_menu_inset(bool* left);
