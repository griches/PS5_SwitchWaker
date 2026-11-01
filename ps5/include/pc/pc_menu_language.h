#pragma once
// Menu-only language preference. "auto" follows PAL's live selection; US uses English.
void pc_menu_language_set(const char* code);
// English text is the catalog key and fallback. Resolve at draw time for live changes.
const char* pc_menu_text(const char* english);
