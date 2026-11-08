/* des_region.cpp: the disc the game was lifted from (des_region.h), and the
 * European disc's language. */
#include "des_region.h"
#include "ps3emu/ppu_lift.h"   /* vm_read32, vm_write32 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

extern "C" {
uint8_t g_des_region = static_cast<uint8_t>(DesRegion::US);
}

const char* des_region_title_id(DesRegion r)
{
    return r == DesRegion::EU ? "BLES00932" : "BLUS30443";
}

extern "C" const char* cellGame_get_title_id(void);   /* cellGame.c: PARAM.SFO's TITLE_ID */
extern "C" void ps3_setenv(const char* name, const char* value);   /* boot_main.cpp: through the getenv cache */

/* From ps3_title_options_init, once PARAM.SFO is read: say which disc this is,
 * and warn when the game folder's PARAM.SFO is the other disc's (the program is
 * the lifted EBOOT's either way). */
extern "C" void des_region_check_title(void)
{
    const DesRegion r = des_region();
    const char* want = des_region_title_id(r);
    const char* id = cellGame_get_title_id();
    fprintf(stderr, "[des] disc: %s (%s)\n", want, r == DesRegion::EU ? "Europe" : "US");
    if (id && *id && strcmp(id, want) != 0)
        fprintf(stderr, "[des] the game folder's PARAM.SFO is %s, but the game was set up from %s's EBOOT\n", id, want);
}

namespace {

/* The European disc's languages: DES_LANGUAGE's values, the PS3 system
 * language each one sets (PS3_SYS_LANG, ps3recomp's ps3emu/sys_lang.h), and
 * for Japanese, Chinese and Korean the game's language index (use_asian_text). */
struct Language {
    const char* code;   /* DES_LANGUAGE */
    const char* sys;    /* PS3_SYS_LANG */
    const char* name;
    int index;          /* the game's language index to set, or -1: the game picks it from PS3_SYS_LANG */
};
constexpr Language kLanguages[] = {
    {"en", "en-gb", "English", -1},
    {"fr", "fr", "French", -1},
    {"de", "de", "German", -1},
    {"es", "es", "Spanish", -1},
    {"it", "it", "Italian", -1},
    {"ja", "ja", "Japanese", 0},
    {"zh-tw", "zh-tw", "Chinese (Traditional)", 3},
    {"ko", "ko", "Korean", 2},
};

const Language* language_of(const std::string& code)
{
    for (const Language& l : kLanguages)
        if (code == l.code) return &l;
    return nullptr;
}

/* The disc's language for the PC's language tag ("fr-fr", "zh-hant-tw"): the
 * one named by its first part, except Chinese, which the disc has only in
 * Traditional characters (Taiwan, Hong Kong, Macau). */
const Language* language_of_pc(const std::string& tag)
{
    const std::string first = tag.substr(0, tag.find('-'));
    if (first != "zh") return language_of(first);
    for (size_t at = tag.find('-'); at != std::string::npos;) {
        const size_t end = tag.find('-', at + 1);
        const std::string part = tag.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
        if (part == "hant" || part == "tw" || part == "hk" || part == "mo") return language_of("zh-tw");
        at = end;
    }
    return nullptr;
}

/* The game's language index (US addresses; the European disc's are in
 * regions/BLES00932.inc). The European program keeps its language as one
 * index: 0 Japanese, 1 Asian English, 2 Korean, 3 Chinese, 4 US English, 5-9
 * English (UK), German, French, Spanish, Italian. It fills the word at first
 * use, while the word is still -1, from the PS3 system language, and only
 * ever with 5-9. The text files (msg/<language>/, and msg/ itself for
 * Japanese), the fonts (likewise) and the menus' language folder all follow
 * the word. Two switches on it pick
 * other things by region, through jump tables of case offsets that the lifted
 * code reads as it runs. */
constexpr uint32_t kLanguageIndex = 0x019D8C88u;   /* the word, -1 until the game's first use */
constexpr uint32_t kGameParamCases = 0x00162D0Cu;  /* the balance file: 0-3 the Asian one, 4-9 GameParamNA */
constexpr uint32_t kOnlineTagCases = 0x00013874u;  /* the server info's tags (game server, interval) */

/* Japanese, Chinese and Korean. The European disc carries the text and fonts
 * for them, but its program never picks them: on a PS3 set to one of them it
 * shows English. So the index is set here, before the game runs. The entries
 * for 0-3 in the two other switches are pointed at a Western one's case, so
 * the language changes only the text: the balance file stays GameParamNA (as
 * US English), and online uses UK English's tags, which a server with the
 * original servers' split sends to the European world, not the Japanese one.
 * False when the word is not the -1 this disc starts with. */
bool use_asian_text(int index)
{
    const uint32_t word = DES_AT(kLanguageIndex);
    if (vm_read32(word) != 0xFFFFFFFFu) return false;
    const uint32_t param = DES_AT(kGameParamCases), tags = DES_AT(kOnlineTagCases);
    vm_write32(word, static_cast<uint32_t>(index));
    for (uint32_t i = 0; i <= 3; i++) {
        vm_write32(param + 4 * i, vm_read32(param + 4 * 4));   /* US English's: GameParamNA */
        vm_write32(tags + 4 * i, vm_read32(tags + 4 * 5));     /* UK English's */
    }
    return true;
}

/* The OS's display language, as a lower-case language tag ("fr-fr",
 * "zh-hant-tw"), or "" when it gives none. Windows: the user's UI language.
 * Elsewhere: LC_ALL, then LC_MESSAGES, then LANG ("fr_FR.UTF-8"), as POSIX
 * orders them. */
std::string os_language()
{
    std::string name;
#ifdef _WIN32
    wchar_t buf[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), buf, LOCALE_NAME_MAX_LENGTH, 0) > 0)
        for (const wchar_t* p = buf; *p && *p < 128; p++) name += static_cast<char>(*p);
#else
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* v = getenv(var);
        if (v && *v) {
            name = v;
            break;
        }
    }
#endif
    std::string tag;   /* letters and dashes, up to the encoding (".UTF-8") or a modifier ("@euro") */
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == '_') c = '-';
        if ((c < 'a' || c > 'z') && c != '-') break;
        tag += c;
    }
    return tag;
}

}  // namespace

/* From ps3_title_options_init, after settings.ini is read and the ELF is
 * loaded, before the guest runs: the European disc's text language.
 * DES_LANGUAGE picks one of the disc's languages; empty (the default) is the
 * PC's language when the disc has it, else English. A PS3_SYS_LANG given at
 * start wins, as on a PS3 (Japanese, Chinese and Korean then show English). The US disc
 * is English only, so it keeps the runtime's English (US). */
extern "C" void des_region_language(void)
{
    if (des_region() != DesRegion::EU) return;
    if (const char* given = getenv("PS3_SYS_LANG"); given && *given) {
        fprintf(stderr, "[des] language: PS3_SYS_LANG=%s\n", given);
        return;
    }
    const char* chosen = getenv("DES_LANGUAGE");
    const bool from_pc = !chosen || !*chosen;
    const Language* l = from_pc ? language_of_pc(os_language()) : language_of(chosen);
    if (!l) {
        if (!from_pc) fprintf(stderr, "[des] DES_LANGUAGE=%s is not one of the disc's languages\n", chosen);
        l = &kLanguages[0];
    }
    if (l->index >= 0 && !use_asian_text(l->index)) {
        fprintf(stderr, "[des] language: the game's language is already set, so not %s; English\n", l->name);
        l = &kLanguages[0];
    }
    ps3_setenv("PS3_SYS_LANG", l->sys);
    fprintf(stderr, "[des] language: %s%s\n", l->name, from_pc ? " (the PC's language, else English)" : "");
}
