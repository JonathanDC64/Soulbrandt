// The Mods window (mods.hpp, docs/143). Laid out like the Trophies window (Dusklight's windows, CC0, by way of
// FFCCEOT-Recomp): one tab, a summary over one list. Everything is in that list, so Up and Down (or the mouse) reach
// it all: the two buttons, a card per mod, the folders that need attention, then a few lines worth knowing. Pressing
// a mod's card (Cross, Enter or a click) turns it on or off; pressing a card whose files only need moving moves them.
// A card's arrows (or L1 and R1, Page Up and Page Down) move the mod up or down the list: the higher one wins when two
// change the same file. Both go to the mods folder's order file at once (mods.hpp). The mods folder is read when the
// window opens and on "Refresh the list". Alpha UI's; the mods folder is kept in ModsState (mods_state.hpp), as for the
// DeS UI page (mods_page.cpp).
#include "mods_window.hpp"

#include "app.hpp"
#include "button.hpp"
#include "mods.hpp"
#include "mods_state.hpp"
#include "pane.hpp"
#include "report.hpp"
#include "settings.hpp"
#include "ui.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace ui {
namespace {

Rml::String quoted(const std::string& name) {
    return "\"" + escape(name) + "\"";
}

}  // namespace

struct ModsWindow::State : ModsState {
    std::vector<Component*> cards;  // the card that shows all[i] (build_tab), for the focus after a move
};

namespace {

// Whether `el`, or a parent of it up to the card, is a `tag` element.
bool within(Rml::Element* el, Rml::Element* card, const char* tag) {
    for (; el != nullptr && el != card; el = el->GetParentNode())
        if (el->GetTagName() == tag) return true;
    return false;
}

// One mod: arrows to move it, a check box, its name and what it changes, and On or Off. Pressing it turns it on or
// off; an arrow, L1 or R1 moves it. A folder that needs attention instead shows a warning sign, what is wrong and
// what to do; pressing it fixes loose files.
class ModCard : public FluentComponent<ModCard> {
public:
    ModCard(Rml::Element* parent, std::shared_ptr<ModsWindow::State> state, std::size_t index)
        : FluentComponent(append(parent, "mod-card")), mState(std::move(state)), mIndex(index) {
        if (mIndex < mState->cards.size()) mState->cards[mIndex] = this;
        render();
        on_nav_command([this](Rml::Event& event, NavCommand cmd) {
            if (cmd == NavCommand::Confirm) {
                Rml::Element* target = event.GetTargetElement();
                if (within(target, mRoot, "move-up")) return move(-1);
                if (within(target, mRoot, "move-down")) return move(1);
                return press();
            }
            if (cmd == NavCommand::Previous) return move(-1);
            if (cmd == NavCommand::Next) return move(1);
            return false;
        });
    }

    void update() override {
        if (mRendered != mState->generation) render();
        FluentComponent::update();
    }

private:
    const mods::Mod& mod() const { return mState->all[mIndex]; }

    // Up (step -1) or down (+1) past the next usable mod; the focus goes with it.
    bool move(int step) {
        if (!mod().usable()) return false;
        const std::size_t to = mState->move(mIndex, step);   // (every card shows its mod again)
        if (to >= mState->all.size()) {   // (fixed, or the top or the bottom of the list already)
            play_sound(kSoundWarning);
            return true;
        }
        play_sound(kSoundItemChange);
        if (to < mState->cards.size() && mState->cards[to] != nullptr) mState->cards[to]->focus();
        return true;
    }

    bool press() {
        const mods::Mod& m = mState->all[mIndex];
        if (m.usable()) {
            if (!mState->toggle(mIndex)) play_sound(kSoundWarning);
            else play_sound(m.enabled ? kSoundItemEnable : kSoundItemDisable);
            return true;
        }
        if (m.why != mods::Problem::NotInPlace) {
            play_sound(kSoundWarning);
            return true;
        }
        play_sound(mState->fix(mIndex).empty() ? kSoundItemEnable : kSoundWarning);
        return true;
    }

    void render() {
        mRendered = mState->generation;
        const mods::Mod& m = mod();
        std::string rml;
        if (m.usable()) {
            const bool on = m.enabled;
            const bool pending = mState->pending(mIndex);
            const bool fixed = ModsState::fixed();
            const bool top = mState->neighbour(mIndex, -1) >= mState->all.size();
            const bool bottom = mState->neighbour(mIndex, 1) >= mState->all.size();
            mRoot->SetClass("on", on);
            mRoot->SetClass("off", !on);
            mRoot->SetClass("fixed", fixed);
            rml = "<mod-move><move-up class=\"" + std::string(fixed || top ? "none" : "") +
                  "\"><icon class=\"mods-up\"/></move-up><move-down class=\"" +
                  std::string(fixed || bottom ? "none" : "") +
                  "\"><icon class=\"mods-down\"/></move-down></mod-move>"
                  "<mod-check><icon class=\"" + std::string(on ? "check-box" : "check-box-blank") +
                  "\"/></mod-check><mod-text><mod-name>" + escape(m.name) + "</mod-name>";
            if (m.kind == mods::Kind::Gameplay) {
                rml += "<mod-line class=\"gameplay\"><icon class=\"warning\"/>Changes how the game plays. You play "
                       "offline while it's on.</mod-line>";
            } else {
                rml += "<mod-line>Changes how the game looks or sounds.</mod-line>";
            }
            if (const std::string by = mState->covered_by(mIndex); !by.empty()) {
                rml += "<mod-line class=\"note\">Some of its changes are replaced by " + quoted(by) +
                       ", which is higher in the list. Move this mod up to use its own.</mod-line>";
            }
            rml += "</mod-text><mod-state><state-word>" + std::string(on ? "On" : "Off") + "</state-word>";
            if (pending) rml += "<state-note>after a restart</state-note>";
            rml += "</mod-state>";
        } else {
            const bool fixable = m.why == mods::Problem::NotInPlace;
            mRoot->SetClass("problem", true);
            mRoot->SetClass("fixable", fixable);
            rml = "<mod-check><icon class=\"" + std::string(fixable ? "warning" : "error") +
                  "\"/></mod-check><mod-text><mod-name>" + escape(m.name) + "</mod-name><mod-line>" +
                  escape(m.problem) + "</mod-line><mod-line class=\"todo\">" + escape(mods::problem_advice(m.why)) +
                  "</mod-line></mod-text><mod-state><state-word>" +
                  (fixable ? Rml::String("Fix it") : escape(mods::problem_label(m.why))) + "</state-word></mod-state>";
        }
        mRoot->SetInnerRML(rml);
    }

    std::shared_ptr<ModsWindow::State> mState;
    std::size_t mIndex;
    unsigned mRendered = 0;
};

// Buttons side by side; Left and Right move between them.
class ModsActions : public FluentComponent<ModsActions> {
public:
    explicit ModsActions(Rml::Element* parent) : FluentComponent(append(parent, "mods-actions")) {
        listen(Rml::EventId::Keydown, [this](Rml::Event& event) {
            const NavCommand cmd = map_nav_event(event);
            if (cmd != NavCommand::Left && cmd != NavCommand::Right) return;
            for (std::size_t i = 0; i < mChildren.size(); ++i) {
                if (!mChildren[i]->contains(event.GetTargetElement())) continue;
                const std::size_t to = cmd == NavCommand::Right ? i + 1 : i - 1;   // (wraps past 0: out of range)
                if (to < mChildren.size() && mChildren[to]->focus()) play_sound(kSoundItemFocus);
                event.StopPropagation();
                return;
            }
        });
    }

    Button& add(const Rml::String& text) { return add_child<Button>(text); }
};

}  // namespace

ModsWindow::ModsWindow()
    : Window(Props{.tabBar = true, .styleSheets = {"res/rml/mods.rcss"}}), mState(std::make_shared<State>()) {
    mState->load();
    add_tab("Mods", [this](Rml::Element* content) { build_tab(content); });
}

void ModsWindow::build_tab(Rml::Element* content) {
    mSummary = append(content, "mods-summary");
    render_summary();

    auto& list = add_child<Pane>(content, Pane::Type::Uncontrolled);
    const auto state = mState;
    state->cards.assign(state->all.size(), nullptr);

    auto& actions = list.add_child<ModsActions>();
    if (!state->dir.empty()) {
        actions.add("Open the mods folder").on_pressed([state] {
            play_sound(kSoundClick);
            std::error_code ec;
            std::filesystem::create_directories(state->dir, ec);
            report::open_local(state->dir);
        });
    }
    actions.add("Refresh the list").on_pressed([state] {
        play_sound(kSoundClick);
        state->lookAgain = true;
    });

    if (state->all.empty()) {
        list.add_rml(state->dir.empty()
                         ? Rml::String("<mods-empty><empty-title>No mods folder</empty-title>This run keeps no data "
                                       "folder, so it has no mods folder.</mods-empty>")
                         : "<mods-empty><empty-title>How to add a mod</empty-title>"
                           "<empty-step>1. Download a mod and unpack it if it is a .zip, .rar or .7z file.</empty-step>"
                           "<empty-step>2. Press \"Open the mods folder\" and put the mod there, in a folder of its "
                           "own.</empty-step>"
                           "<empty-step>3. Press \"Refresh the list\", turn the mod on, and restart the game."
                           "</empty-step></mods-empty>");
    }

    const std::size_t usable = state->usable();
    if (usable > 0) list.add_section("Your mods");
    for (std::size_t i = 0; i < state->all.size(); ++i)
        if (state->all[i].usable()) list.add_child<ModCard>(state, i);
    if (usable < state->all.size()) list.add_section("Needs attention");
    for (std::size_t i = 0; i < state->all.size(); ++i)
        if (!state->all[i].usable()) list.add_child<ModCard>(state, i);

    Rml::String footer = "<mods-footer><footer-title>Good to know</footer-title>";
    for (const std::string& line : mods_good_to_know()) footer += "<footer-line>" + escape(line) + "</footer-line>";
    list.add_rml(footer + "</mods-footer>");
}

void ModsWindow::render_summary() {
    if (mSummary == nullptr) return;
    mSummaryShown = mState->generation;
    const State& s = *mState;
    Rml::String rml = "<summary-text><summary-line>" + s.summary_line() + "</summary-line>";
    if (ModsState::fixed()) {
        rml += "<summary-note class=\"info\"><icon class=\"mods-info\"/>This run's mods were set when the game "
               "started, so they can't be changed here.</summary-note>";
    } else if (s.restart_needed()) {
        rml += "<summary-note class=\"warning\"><icon class=\"mods-restart\"/>Restart the game to use your changes."
               "</summary-note>";
    }
    if (mods::gameplay_in_use()) {
        rml += "<summary-note class=\"offline\"><icon class=\"mods-offline\"/>Playing offline: a mod that changes how "
               "the game plays is on (" + escape(mods::gameplay_names()) + ").</summary-note>";
    }
    rml += "</summary-text>";
    mSummary->SetInnerRML(rml);
}

void ModsWindow::update() {
    if (mState->lookAgain) {  // (read again here, not inside the press)
        mState->lookAgain = false;
        mState->load();
        refresh_active_tab();
    } else if (mSummaryShown != mState->generation) {
        render_summary();
    }
    Window::update();
}

}  // namespace ui
