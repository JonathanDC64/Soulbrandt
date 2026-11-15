// The F1 menu's rules on their own (menu_model.hpp), with no RmlUi and no settings: what waits for a restart is made up
// here. Closing the menu closes it when nothing waits; while something does, it asks about a restart, until the player
// answers Later to that very set of changes, and asks again once what waits is different.
//
// Prints a line per failure and "menu_logic_check: N checks, M failed"; exit 1 on a failure. Built on demand:
// cmake --build <build> --target menu_logic_check, then menu_logic_check.
#include "menu_model.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_fail = 0;

void check(bool ok, const char* what) {
    g_checks++;
    if (!ok) {
        std::printf("  FAIL %s\n", what);
        g_fail++;
    }
}

}  // namespace

int main() {
    using ui::menu::Model;
    using Waiting = std::vector<std::string>;
    const Waiting none, vulkan{"RSX_BACKEND=vulkan"}, d3d12{"RSX_BACKEND=d3d12"},
        two{"RSX_BACKEND=vulkan", "DES_LANGUAGE=ja"};
    constexpr auto now = Model::Close::Now, ask = Model::Close::AskRestart;

    Model m;
    check(m.close(none) == now, "nothing waits: the menu closes");
    check(m.close(vulkan) == ask, "a setting waits: it asks");
    check(m.close(vulkan) == ask, "not answered yet: it asks again");
    m.later(vulkan);
    check(m.close(vulkan) == now, "Later to it: the menu closes");
    check(m.close(d3d12) == ask, "the same setting at another value: it asks");
    check(m.close(two) == ask, "another setting as well: it asks");
    check(m.close(none) == now, "all changed back: nothing waits");
    check(m.close(vulkan) == now, "back to what Later answered: the menu closes");
    m.later(two);
    check(m.close(two) == now, "Later to two settings: the menu closes");
    check(m.close(vulkan) == ask, "one of them changed back: what waits is new, it asks");
    m.later(none);
    check(m.close(vulkan) == ask, "Later with nothing waiting forgets the earlier answer");

    std::printf("menu_logic_check: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
