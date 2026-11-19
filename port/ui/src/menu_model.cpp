// Written for Demon's Souls (menu_model.hpp).
#include "menu_model.hpp"

#include <utility>

namespace ui::menu {

Model::Back Model::back() {
    if (mLevel == Level::Page && !mStandalone) {
        mLevel = Level::Entries;
        return Back::ToEntries;
    }
    return Back::Close;
}

Model::Close Model::close(const std::vector<std::string>& waiting) const {
    return waiting.empty() || waiting == mLater ? Close::Now : Close::AskRestart;
}

void Model::later(std::vector<std::string> waiting) {
    mLater = std::move(waiting);
}

std::string and_list(const std::vector<std::string>& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) out += i + 1 == items.size() ? " and " : ", ";
        out += items[i];
    }
    return out;
}

ModsRestart mods_restart(bool startScreen, bool coop) {
    return {.ask = !startScreen || coop, .play = !startScreen};
}

std::string mods_restart_body(const std::vector<std::string>& settings, Coop coop) {
    std::vector<std::string> named = {"Your mod changes"};
    named.insert(named.end(), settings.begin(), settings.end());
    std::string body = and_list(named) + " take effect after a restart.";
    if (coop == Coop::Host)
        body += " Your friends go back to their own worlds, and your invite code changes: send them the new one.";
    else if (coop == Coop::Guest)
        body += " Your game leaves the host's world, then joins it again.";
    return body;
}

}  // namespace ui::menu
