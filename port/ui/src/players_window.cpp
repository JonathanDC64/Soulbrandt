// The Players window (players_window.hpp). Everything in it comes from the runtime's ps3_net_players calls: the window
// asks for the list when it opens, and draws it again whenever an answer comes (the runtime counts them). A block or a
// report goes to the runtime, which sends it, asks for the list again, and hands its outcome to net_notices.cpp, so
// the toast comes even when the window has closed by then.
//
// Laid out like the Trophies window (trophies_window.cpp): one tab, the summary above one pane, one row per player.
// Up and Down move between rows, L1 and R1 (Page Up and Page Down) six rows at a time, and pressing a row (Cross, Enter
// or a click) opens what can be done with that player.
#include "players_window.hpp"

#include "pane.hpp"
#include "players_state.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace ui {

using namespace players;

struct PlayersWindow::State : players::State {
    std::vector<Component*> rows;  // the player rows, in order (for L1 / R1)
};

namespace {

// One player: how you met, their name and key, when, and whether you blocked or reported them.
class PlayerRow : public FluentComponent<PlayerRow> {
public:
    PlayerRow(Rml::Element* parent, std::shared_ptr<PlayersWindow::State> state, int index)
        : FluentComponent(append(parent, "player-row")), mState(std::move(state)), mIndex(index) {
        render();
        on_nav_command([this](Rml::Event&, NavCommand cmd) { return handle_nav_command(cmd); });
    }

    void update() override {
        if (mRendered != mState->generation) render();
        FluentComponent::update();
    }

private:
    bool handle_nav_command(NavCommand cmd) {
        if (cmd == NavCommand::Confirm) {
            if (mIndex < int(mState->players.size())) {
                play_sound(kSoundClick);
                open_actions(mState->players[mIndex]);
            }
            return true;
        }
        if (cmd == NavCommand::Next || cmd == NavCommand::Previous) {
            const auto& rows = mState->rows;
            const auto it = std::find(rows.begin(), rows.end(), this);
            if (it == rows.end()) return false;
            const int at = int(it - rows.begin());
            const int to = std::clamp(at + (cmd == NavCommand::Next ? kPageRows : -kPageRows), 0, int(rows.size()) - 1);
            if (to != at && rows[to]->focus()) play_sound(kSoundItemFocus);
            return true;
        }
        return false;
    }

    void render() {
        mRendered = mState->generation;
        if (mIndex >= int(mState->players.size())) return;
        const ps3_net_player& p = mState->players[mIndex];
        mRoot->SetClass("blocked", p.blocked != 0);
        std::string rml = std::string("<player-icon><icon class=\"") + met_icon(p) + "\"/></player-icon><player-text>";
        rml += "<player-name>" + escape(name_of(p));
        if (p.blocked) rml += "<player-tag class=\"blocked\">Blocked</player-tag>";
        if (p.reported) rml += "<player-tag>Reported</player-tag>";
        rml += "</player-name><player-detail>" + escape(how_met(p)) + "<player-key>key " +
               escape(key_text(p).substr(0, 9)) + "</player-key></player-detail>";
        rml += "</player-text><player-date>" + escape(ago(p.at)) + "</player-date>";
        mRoot->SetInnerRML(rml);
    }

    std::shared_ptr<PlayersWindow::State> mState;
    int mIndex;
    unsigned mRendered = 0;
};

}  // namespace

PlayersWindow::PlayersWindow()
    : Window(Props{.tabBar = true, .styleSheets = {"res/rml/players.rcss"}}), mState(std::make_shared<State>()) {
    mState->start();
    std::fprintf(stderr, "[ui] players window: %s, %zu rows so far\n",
                 mState->signedIn ? "asking the server" : "no sign-in", mState->players.size());
    add_tab("Players", [this](Rml::Element* content) { build_tab(content); });
}

void PlayersWindow::build_tab(Rml::Element* content) {
    mState->rows.clear();
    mSummary = append(content, "players-summary");
    render_summary();
    auto& pane = add_child<Pane>(content, Pane::Type::Uncontrolled);
    for (int i = 0; i < int(mState->players.size()); i++) {
        mState->rows.push_back(&pane.add_child<PlayerRow>(mState, i));
    }
}

void PlayersWindow::render_summary() {
    if (mSummary == nullptr) return;
    mSummary->SetInnerRML("<summary-icon><icon class=\"players\"/></summary-icon><summary-text><summary-title>Players "
                          "you met</summary-title><summary-line>" + escape(mState->summary()) +
                          "</summary-line></summary-text>");
}

void PlayersWindow::update() {
    const size_t before = mState->players.size();
    const unsigned generation = mState->generation;
    if (mState->update()) {
        if (mState->generation != generation) {
            std::fprintf(stderr, "[ui] players window: %zu rows (state %d)\n", mState->players.size(), mState->status);
        }
        if (mState->players.size() != before) {
            refresh_active_tab();  // (rows come and go: build the list again)
        } else {
            render_summary();  // (the rows follow the generation)
        }
    }
    Window::update();
}

}  // namespace ui
