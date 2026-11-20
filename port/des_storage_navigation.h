/* Storage's visual rows over the game's native four-row command menu.
 *
 * row and expected are always native menu rows (0..3). Auto Sort is the one
 * row beyond the native menu's end: remapping it to native row 0 is ambiguous
 * with Use, so past_last carries that one extra bit. The visible row is
 * derived from this pair; native row changes, not pad edges, drive navigation.
 */
#pragma once

struct DesStorageNavigation {
    int row = 0;       // native row state; past_last makes row 0 mean Auto Sort
    int expected = -1; // acknowledgement for the last native row we published
    bool past_last = false;

    void reset() {
        row = 0;
        expected = -1;
        past_last = false;
    }

    bool active() const { return expected >= 0; }

    int visible() const { return past_last ? 4 : row; }

    int projected() const { return row; }

    bool send() const { return active() && visible() == 2; }

    int command() const {
        const int visual = visible();
        return visual > 2 ? visual - 1 : visual;
    }

    int dispatch() {
        const int native = command();
        row = native;
        expected = native;
        past_last = false;
        return native;
    }

    /* A direct UI selection is also a remap: visual Auto Sort aliases native
     * Use's row and is distinguished only by past_last. */
    bool select_visible(int visual) {
        if (visual < 0 || visual > 4) return false;
        row = visual == 4 ? 0 : visual;
        expected = row;
        past_last = visual == 4;
        return true;
    }

    /* Observe the row after the game's input update, then remap it for the
     * next draw/dispatch. Returns the direction of the accepted movement.
     * The two native wraps through row 0 distinguish Auto Sort from Use.
     */
    int observe(int native) {
        if (native < 0 || native > 3) return 0;

        if (!active()) {
            // A reused dialog always opens on Use, even if its row is stale.
            row = 0;
            expected = 0;
            past_last = false;
            return 0;
        }
        if (native == expected) return 0; // our projected write is not another input step

        if (row == 3 && native == 0) {
            // Reorganize -> Auto Sort: the native menu wrapped past its end.
            row = 0;
            expected = 0;
            past_last = true;
            return 1;
        }
        if (row == 0 && native == 3) {
            if (past_last) {
                // Auto Sort -> Reorganize when moving up.
                row = 3;
                expected = 3;
                past_last = false;
            } else {
                // Use -> Auto Sort when moving up through the native wrap.
                row = 0;
                expected = 0;
                past_last = true;
            }
            return -1;
        }
        if (row == 0 && past_last && native == 1) {
            // Auto Sort -> Use when moving down; cancel the native extra step.
            row = 0;
            expected = 0;
            past_last = false;
            return 1;
        }

        const int direction = native > row ? 1 : -1;
        row = native;
        expected = native;
        past_last = false;
        return direction;
    }
};
