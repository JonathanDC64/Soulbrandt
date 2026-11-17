/* Storage's fifth command row over the native four-row menu. Native navigation
 * is the only source of steps: a PAD edge is not a second event. Observe after
 * the native update, before drawing or dispatching a command, and publish the
 * projected row back. Re-observing that projection is idempotent.
 */
#pragma once

struct DesStorageNavigation {
    int row = 0;
    int expected = -1;

    void reset() { row = 0; expected = -1; }
    bool active() const { return expected >= 0; }
    int projected() const { return row < 4 ? row : 3; }
    bool send() const { return active() && row == 2; }
    int command() const { return row > 2 ? row - 1 : row; }
    int dispatch() { expected = command(); return expected; }

    int observe(int native) {
        if (native < 0 || native > 3) return 0;
        if (!active()) {
            // Every opening starts on Use. The reused native dialog may still
            // contain the projection from its last opening (including Send).
            row = 0;
            expected = projected();
            return 0;
        }
        if (native == expected) return 0;
        const int step = expected == 3 && native == 0 ? 1 :
                         expected == 0 && native == 3 ? -1 : native > expected ? 1 : -1;
        if (step > 0 && row < 4) ++row;
        if (step < 0 && row > 0) --row;
        expected = projected();
        return step;
    }
};
