#include "Lang.h"

#include <cstring>

namespace mc {

int gLanguage = LANG_EN;

const char* LanguageName(int lang) { return lang == LANG_TR ? "T\xC3\xBCrk\xC3\xA7" "e (T\xC3\xBCrkiye)" : "English (US)"; }

const char* LanguageCode(int lang) { return lang == LANG_TR ? "tr" : "en"; }

int LanguageFromCode(const char* code) { return code && (std::strncmp(code, "tr", 2) == 0) ? LANG_TR : LANG_EN; }

} // namespace mc
