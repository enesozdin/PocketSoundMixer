#include "Lang.h"

namespace psm {

namespace detail {

Language g_language = Language::English;

const char* const kStrings[static_cast<int>(Language::Count)][static_cast<int>(S::Count)] = {
    {
#define PSM_STRING_EN(id, en, tr) en,
        PSM_STRINGS(PSM_STRING_EN)
#undef PSM_STRING_EN
    },
    {
#define PSM_STRING_TR(id, en, tr) tr,
        PSM_STRINGS(PSM_STRING_TR)
#undef PSM_STRING_TR
    },
};

} // namespace detail

void setLanguage(Language language)
{
    detail::g_language = language;
}

Language currentLanguage()
{
    return detail::g_language;
}

const char* languageCode(Language language)
{
    return language == Language::Turkish ? "tr" : "en";
}

Language languageFromCode(const std::string& code)
{
    return code == "tr" ? Language::Turkish : Language::English;
}

const char* languageName(Language language)
{
    return language == Language::Turkish ? "Türkçe" : "English";
}

} // namespace psm
