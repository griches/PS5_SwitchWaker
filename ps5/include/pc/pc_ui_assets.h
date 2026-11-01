#pragma once
#include <cstdint>
class JKRArchive;
void pc_ui_assets_init();
bool pc_ui_assets_active();
// Resource lookups are scoped to a validated archive instance, never global names.
void* pc_ui_assets_resource(JKRArchive* archive, const char* volume, uint32_t type, const char* path);
uint32_t pc_ui_assets_size(const void* resource);
uint32_t pc_ui_assets_copy(const void* resource, void* buffer, uint32_t capacity);
void pc_ui_assets_forget(const JKRArchive* archive);

// Shared US/PAL eye prompt, with the permanent R3 binding.
void* pc_ui_assets_camera_prompt(const void* original, uint32_t size);

// Generated arrow artwork, shared by telescope and Picto Box zoom prompts.
void* pc_ui_assets_zoom_arrow();
