#include "pc/pc_menu_language.h"
#include "global.h"
#include "d/d_com_inf_game.h"
#include <cstring>

namespace {
// Matches the game's PAL language order.
enum Language { English, German, French, Spanish, Italian };
int overrideLanguage = -1;
const char* const codes[] = {"en", "de", "fr", "es", "it"};
struct Translation { const char* english; const char* german; const char* french; const char* spanish; const char* italian; };
const Translation translations[] = {
#include "pc_menu_translations.inc"
};
int language() {
    if (overrideLanguage >= 0) return overrideLanguage;
#if VERSION == VERSION_PAL
    const int selected = dComIfGs_getPalLanguage();
    if (REGION_PAL && selected >= English && selected <= Italian) return selected;
#endif
    return English;
}
}
void pc_menu_language_set(const char* code) {
    overrideLanguage = -1;
    if (code) for (int i = 0; i < 5; ++i)
        if (std::strcmp(code, codes[i]) == 0) { overrideLanguage = i; break; }
}
const char* pc_menu_text(const char* english) {
    if (!english) return "";
    const int selected = language();
    if (selected != English) for (const auto& entry : translations) {
        if (std::strcmp(english, entry.english) != 0) continue;
        const char* result = selected == German ? entry.german :
            selected == French ? entry.french : selected == Spanish ? entry.spanish : entry.italian;
        if (result && *result) return result;
        break;
    }
    return english;
}
