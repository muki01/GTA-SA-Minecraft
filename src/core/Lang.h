#pragma once
// The language of everything Minecraft says - block and item names, menus, messages: English (as Minecraft starts)
// or Turkish. The generated tables hold both (LangStr, ItemName, EffectName); Tr() picks between the mod's own texts.

namespace mc {

enum Language { LANG_EN = 0, LANG_TR, LANG_COUNT };
extern int gLanguage;

inline const char* Tr(const char* en, const char* tr) { return gLanguage == LANG_TR ? tr : en; }
const char* LanguageName(int lang);     // in its own language, as Minecraft's language list shows it
const char* LanguageCode(int lang);     // "en", "tr"
int LanguageFromCode(const char* code); // anything unknown: English

} // namespace mc
