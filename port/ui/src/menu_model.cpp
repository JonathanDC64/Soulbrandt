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

}  // namespace ui::menu
