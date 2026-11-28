// Standalone checks for the production storage navigation state machine.
#include "../des_storage.h"
#include "../des_storage_navigation.h"

#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

#define CHECK(expression) do { ++checks; if (!(expression)) { \
    std::fprintf(stderr, "storage_navigation_check:%d: %s\n", __LINE__, #expression); ++failures; \
} } while (0)

void observe(DesStorageNavigation& nav, int native, int expected_row, int expected_visual, int step) {
    CHECK(nav.observe(native) == step);
    CHECK(nav.row == expected_row);
    CHECK(nav.visible() == expected_visual);
    CHECK(nav.expected == nav.projected());
}
}

int main() {
    DesStorageNavigation nav;

    // A reused command dialog is always remapped to Use on open.
    CHECK(nav.observe(3) == 0);
    CHECK(nav.row == 0 && nav.visible() == 0 && nav.projected() == 0);

    // Native D-pad sequence: Use, Drop, Send, Reorganize, Auto Sort, Use.
    observe(nav, 1, 1, 1, 1);
    observe(nav, 2, 2, 2, 1);
    observe(nav, 3, 3, 3, 1);
    observe(nav, 0, 0, 4, 1); // wrap beyond the native last row
    observe(nav, 0, 0, 4, 0); // repeated draw is idempotent
    observe(nav, 1, 0, 0, 1); // remap Auto Sort's down step back to Use
    observe(nav, 0, 0, 0, 0);

    // Up wraps Use to Auto Sort, then Auto Sort up reaches Reorganize.
    observe(nav, 3, 0, 4, -1);
    observe(nav, 0, 0, 4, 0);
    observe(nav, 3, 3, 3, -1);
    observe(nav, 2, 2, 2, -1);
    observe(nav, 1, 1, 1, -1);
    observe(nav, 0, 0, 0, -1);

    // Direct selection covers all five rows and acknowledges its own write.
    for (int row = 0; row < 5; ++row) {
        CHECK(nav.select_visible(row));
        CHECK(nav.visible() == row);
        CHECK(nav.projected() == (row == 4 ? 0 : row));
        CHECK(nav.observe(nav.projected()) == 0);
        CHECK(nav.visible() == row);
    }
    CHECK(!nav.select_visible(-1));
    CHECK(!nav.select_visible(5));

    // Dispatch translates visible commands back to their native command row.
    CHECK(nav.select_visible(3));
    CHECK(nav.dispatch() == 2); // Reorganize
    CHECK(nav.row == 2 && nav.expected == 2 && !nav.past_last);
    CHECK(nav.observe(nav.projected()) == 0); // dispatch's native write is acknowledged
    CHECK(nav.select_visible(4));
    CHECK(nav.dispatch() == 3); // Auto Sort
    CHECK(nav.row == 3 && nav.expected == 3 && !nav.past_last);
    CHECK(nav.observe(nav.projected()) == 0);

    nav.reset();
    CHECK(!nav.active() && nav.row == 0 && nav.expected == -1 && !nav.past_last);
    CHECK(nav.observe(3) == 0); // reopened dialog starts on Use despite a stale native row
    CHECK(nav.visible() == 0 && nav.projected() == 0);

    std::printf("storage_navigation_check: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
