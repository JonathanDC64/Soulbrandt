// Written for Demon's Souls (docs/146, "Notices"). See coop_notices.hpp.
#include "coop_notices.hpp"

#include "../../des_seamless_notice.h"
#include "settings.hpp"
#include "ui.hpp"

#include <chrono>
#include <cstdio>

namespace ui::coop_notices {
namespace {

constexpr auto kToastDuration = std::chrono::seconds(5);   // (a trophy's too: short notes, read while playing)
constexpr auto kBlockingDuration = std::chrono::seconds(10);  // co-op didn't start: more to read

}  // namespace

bool pending() {
    return des::seamless::notices_waiting();
}

void drain() {
    if (!pending()) return;
    const bool show = settings::get_int("coop_notices") != 0;
    for (const des::seamless::Notice& n : des::seamless::take_notices()) {
        if (!show && !n.always) {
            std::fprintf(stderr, "[ui] co-op notice not shown (Settings, Co-op notices is off): %s\n", n.text.c_str());
            continue;
        }
        std::fprintf(stderr, "[ui] co-op notice shown: %s\n", n.text.c_str());
        push_toast({
            .type = n.warn ? "warning" : "info",
            .title = "Seamless co-op",
            .content = "<toast-message-text>" + escape(n.text) + "</toast-message-text>",
            .duration = n.always ? kBlockingDuration : kToastDuration,
        });
    }
}

void discard() {
    if (pending()) des::seamless::take_notices();
}

}  // namespace ui::coop_notices
