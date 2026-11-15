// Written for Demon's Souls. The trophy set as the Trophies screens read it (trophy_set.hpp).
#include "trophy_set.hpp"

#include "portable.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <utility>

extern "C" int sceNpTrophy_snapshot(sceNpTrophySnapshot* out);

namespace ui::trophy_set {
namespace {

std::function<bool(sceNpTrophySnapshot&)>& test_source() {
    static std::function<bool(sceNpTrophySnapshot&)> source;
    return source;
}

}  // namespace

bool read(sceNpTrophySnapshot& out) {
    const bool found = test_source() ? test_source()(out) : sceNpTrophy_snapshot(&out) == 0;
    if (out.count > 128) out.count = 128;
    return found && out.count > 0;
}

void set_test_source(std::function<bool(sceNpTrophySnapshot&)> source) {
    test_source() = std::move(source);
}

std::string text_of(const char* s, size_t max) {
    return std::string(s, strnlen(s, max));
}

std::string unlock_date(unsigned long long unixTime) {
    static constexpr const char* kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::time_t t = static_cast<std::time_t>(unixTime);
    std::tm tm{};
    if (!ui::local_time(t, tm) || tm.tm_mon < 0 || tm.tm_mon > 11) return {};
    char out[48];
    std::snprintf(out, sizeof out, "%d %s %d, %02d:%02d", tm.tm_mday, kMonths[tm.tm_mon], tm.tm_year + 1900,
                  tm.tm_hour, tm.tm_min);
    return out;
}

std::set<int> test_hidden() {
    std::set<int> ids;
    const char* p = std::getenv("DES_TROPHY_HIDDEN");
    while (p && *p) {
        char* end = nullptr;
        const long id = std::strtol(p, &end, 10);
        if (end == p) {  // a separator
            p++;
            continue;
        }
        if (id >= 0 && id < 128) ids.insert(int(id));
        p = end;
    }
    return ids;
}

}  // namespace ui::trophy_set
