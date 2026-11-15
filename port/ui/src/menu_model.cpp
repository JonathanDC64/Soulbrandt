// Written for Demon's Souls (menu_model.hpp).
#include "menu_model.hpp"

#include <utility>

namespace ui::menu {

Model::Close Model::close(const std::vector<std::string>& waiting) const {
    return waiting.empty() || waiting == mLater ? Close::Now : Close::AskRestart;
}

void Model::later(std::vector<std::string> waiting) {
    mLater = std::move(waiting);
}

}  // namespace ui::menu
