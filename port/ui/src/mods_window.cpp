// The Mods window (mods.hpp, docs/143). Laid out like the other windows (Dusklight's, CC0, by way of FFCCEOT-Recomp):
// one tab, rows on the left and the focused row's help on the right. The mods folder is read again each time the
// window opens, and on "Look again".
#include "mods_window.hpp"

#include "app.hpp"
#include "bool_button.hpp"
#include "mods.hpp"
#include "pane.hpp"
#include "report.hpp"
#include "settings.hpp"
#include "ui.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace ui {
namespace {

bool sLookAgain = false;  // "Look again" or "Put its files in place" was pressed: update() builds the tab again

// DES_MODS as this window changes it: the environment's when set there.
std::string mods_value() {
    return settings::pinned("DES_MODS") ? settings::pinned_value("DES_MODS") : settings::get("DES_MODS");
}

bool rows_fixed() {
    return settings::pinned("DES_MODS") || mods::from_environment();
}

bool mod_on(const std::string& name) {
    const std::vector<std::string> on = mods::split_list(mods_value());
    return std::find(on.begin(), on.end(), name) != on.end();
}

void set_mod_on(const std::string& name, bool value) {
    if (rows_fixed()) return;
    std::vector<std::string> on = mods::split_list(mods_value());
    on.erase(std::remove(on.begin(), on.end(), name), on.end());
    if (value) on.push_back(name);
    settings::set("DES_MODS", mods::join(on, "|"));
}

bool mod_in_use(const std::string& name) {
    const auto& used = mods::in_use();
    return std::any_of(used.begin(), used.end(), [&](const mods::Mod& m) { return m.name == name; });
}

// What heads an on/off row's help: the rows can't change the mods this run, or a change waits for the next start.
Rml::String start_note() {
    if (settings::pinned("DES_MODS")) {
        return "<status-badge class=\"warning\">Set to " + escape(settings::pinned_value("DES_MODS")) +
               " when the game started (DES_MODS), so these rows can't change it.</status-badge><br/>";
    }
    return "<status-badge class=\"info\">Takes effect the next time you start the game.</status-badge><br/>";
}

// (A <file-path> is a block: text after one starts on its own line, and one <br/> after it leaves a blank line.)
Rml::String path_rml(const std::string& path) {
    return "<file-path>" + escape(path) + "</file-path>";
}

// The mod's folder, after text (`afterList`: after a list of paths).
Rml::String folder_lines(const mods::Mod& m, bool afterList = false) {
    Rml::String h = Rml::String(afterList ? "<br/>" : "<br/><br/>") + "Folder:" + path_rml(mods::utf8(m.folder));
    if (!m.root.empty() && m.root != m.folder)
        h += "Its game files:" + path_rml(mods::utf8(m.root.lexically_relative(m.folder)));
    return h;
}

// A mod that can be used: whether it changes how the game plays, then its files.
Rml::String usable_help(const mods::Mod& m) {
    Rml::String h = m.kind == mods::Kind::Gameplay
                        ? "<status-badge class=\"warning\">Changes how the game plays: while it is on, the game stays "
                          "offline.</status-badge><br/>"
                        : "<status-badge class=\"info\">Changes only how the game looks or sounds: online play is not "
                          "affected.</status-badge><br/>";
    h += start_note();
    if (m.files) {
        std::vector<std::string> areas;
        for (const std::string& a : m.areas) areas.push_back(a == "(top)" ? "beside the game's folders" : a);
        h += std::to_string(m.files) + (m.files == 1 ? " file, in " : " files, in ") +
             escape(mods::join(areas, ", ")) + ".";
    }
    if (!m.gameplay.empty()) {
        h += "<br/><br/>Files that change how the game plays:";
        for (const std::string& f : m.gameplay) h += "<br/>" + escape(f);
        if (m.gameplay.size() == 5) h += "<br/>...";
    }
    h += "<br/><br/>This session: " + Rml::String(mod_in_use(m.name) ? "on" : "off") + ".";
    return h + folder_lines(m);
}

// A mod that can't be turned on: why, in plain words, then what the player can do.
Rml::String problem_help(const mods::Mod& m) {
    const bool fixable = m.why == mods::Problem::NotInPlace;
    Rml::String h = "<status-badge class=\"" + Rml::String(fixable ? "warning" : "error") + "\">" +
                    (fixable ? Rml::String("Not in place yet") : "Can't be turned on: " +
                                                                    Rml::String(mods::problem_label(m.why))) +
                    "</status-badge><br/>" + escape(m.problem) + "<br/><br/>What you can do: " +
                    escape(mods::problem_advice(m.why));
    bool list = false;
    if (!m.locked.empty()) {
        h += "<br/><br/>Locked files:";
        for (const std::string& f : m.locked) h += path_rml(f);
        if (m.locked.size() == 5) h += path_rml("...");
        list = true;
    }
    if (!m.moves.empty()) {   // (where each goes in the game's folders; each keeps its name)
        h += "<br/><br/>Where its files go:";
        for (size_t k = 0; k < m.moves.size() && k < 10; ++k) h += path_rml(m.moves[k].to);
        if (m.moves.size() > 10) h += path_rml("...");
        list = true;
    }
    return h + folder_lines(m, list);
}

// A row with a fixed value whose help is `helpRml` (settings_window.cpp's info_row).
void info_row(Pane& leftPane, Pane& rightPane, const Rml::String& label, const Rml::String& value,
    Rml::String helpRml) {
    leftPane.register_control(
        leftPane.add_select_button({
            .key = label,
            .getValue = [value] { return value; },
        }),
        rightPane, [helpRml = std::move(helpRml)](Pane& pane) {
            pane.clear();
            pane.add_rml(helpRml);
        });
}

}  // namespace

ModsWindow::ModsWindow() {
    add_tab("Mods", [this](Rml::Element* content) { build_tab(content); });
}

void ModsWindow::build_tab(Rml::Element* content) {
    auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
    auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

    const std::filesystem::path dir = mods::folder(report::data_dir());
    auto found = std::make_shared<std::vector<mods::Mod>>(mods::scan(dir, mods_value()));
    for (mods::Mod& m : *found) mods::plan(m);  // (loose files: where the game's own files say they go)
    const auto usable = std::count_if(found->begin(), found->end(), [](const mods::Mod& m) { return m.usable(); });

    if (usable > 0 || found->empty() || mods::from_environment()) leftPane.add_section("Mods");
    if (mods::from_environment()) {
        info_row(leftPane, rightPane, "Set at start", "PS3_VFS_MODS",
            "The mods for this run were given when the game started (PS3_VFS_MODS), so the rows below can't change "
            "them.");
    }
    if (found->empty()) {
        info_row(leftPane, rightPane, "No mods yet", dir.empty() ? Rml::String("No mods folder") : Rml::String("Empty"),
            dir.empty() ? Rml::String("This run keeps no data folder, so it has no mods folder.")
                        : "Put each mod in its own folder in the mods folder, then press Look again.<br/><br/>"
                          "<file-path>" + escape(mods::utf8(dir)) + "</file-path>");
    }
    for (size_t i = 0; i < found->size(); ++i) {
        const mods::Mod& m = (*found)[i];
        if (!m.usable()) continue;
        auto& row = leftPane.add_child<BoolButton>(BoolButton::Props{
            .key = m.name,
            .icon = m.kind == mods::Kind::Gameplay ? "warning" : "",
            .getValue = [found, i] { return mod_on((*found)[i].name); },
            .setValue =
                [found, i](bool value) {
                    if (value != mod_on((*found)[i].name)) set_mod_on((*found)[i].name, value);
                },
            .isDisabled = [] { return rows_fixed(); },
            .isModified = [found, i] { return mod_on((*found)[i].name) != mod_in_use((*found)[i].name); },
        });
        leftPane.register_control(row, rightPane, [found, i](Pane& pane) {
            pane.clear();
            pane.add_rml(usable_help((*found)[i]));
        });
    }

    // The mods that are on (in settings.ini) that have the same file: the first by name gives it.
    std::vector<mods::Mod> on;
    for (const mods::Mod& m : *found)
        if (m.usable() && mod_on(m.name)) on.push_back(m);
    if (on.size() > 1) {
        const std::vector<std::string> same = mods::conflicts(on, 20);
        if (!same.empty()) {
            Rml::String list;
            for (const std::string& line : same) list += "<br/>" + escape(line);
            info_row(leftPane, rightPane, "Same files",
                std::to_string(same.size() > 20 ? 20 : same.size()) + (same.size() > 20 ? "+" : ""),
                "Mods that are on have some of the same files. Each file comes from the mod higher in the list (the "
                "first one named here). To change which one wins, rename their folders, for example \"1 Name\" and "
                "\"2 Name\".<br/>" + list);
        }
    }

    // The folders the game can't read as they are: each row says why, and its help (Enter, or a click) what to do.
    if (usable < static_cast<std::ptrdiff_t>(found->size())) leftPane.add_section("Can't be turned on");
    for (size_t i = 0; i < found->size(); ++i) {
        const mods::Mod& m = (*found)[i];
        if (m.usable()) continue;
        auto& row = leftPane.add_select_button({
            .key = m.name,
            .getValue = [why = m.why] { return Rml::String(mods::problem_label(why)); },
        });
        row.set_icon(m.why == mods::Problem::NotInPlace ? "warning" : "error");
        leftPane.register_control(row, rightPane, [found, i](Pane& pane) {
            pane.clear();
            pane.add_rml(problem_help((*found)[i]));
            if ((*found)[i].moves.empty()) return;
            pane.add_button("Put its files in place").on_pressed([found, i] {
                const std::string err = mods::place((*found)[i]);
                play_sound(err.empty() ? kSoundClick : kSoundWarning);
                push_toast({
                    .type = err.empty() ? "" : "warning",
                    .title = err.empty() ? "Mod files put in place" : "Mod files not all moved",
                    .content = err.empty() ? (*found)[i].name + ": its files are where the game reads them now. It "
                                                                "can be turned on."
                                           : (*found)[i].name + ": " + err + ".",
                    .duration = std::chrono::seconds(6),
                });
                sLookAgain = true;
            });
        });
    }

    leftPane.add_section("Mods folder");
    if (!dir.empty()) {
        leftPane.register_control(
            leftPane.add_group_button({.text = "Open the mods folder"}).on_pressed([dir] {
                play_sound(kSoundClick);
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                report::open_local(dir);
            }),
            rightPane, [dir](Pane& pane) {
                pane.clear();
                pane.add_rml("Opens the mods folder:<br/><file-path>" + escape(mods::utf8(dir)) + "</file-path>" +
                             "<br/><br/>Put each mod in its own folder there. A mod's folder holds the game's folders "
                             "it changes (menu, param, parts...), as its notes describe, or the folder its archive "
                             "unpacks to.");
            });
        leftPane.register_control(
            leftPane.add_group_button({.text = "Look again"}).on_pressed([] {
                play_sound(kSoundClick);
                sLookAgain = true;
            }),
            rightPane, [](Pane& pane) {
                pane.clear();
                pane.add_text("Reads the mods folder again, after you add, remove or rename a mod's folder.");
            });
    }

    leftPane.add_section("This session");
    const auto& used = mods::in_use();
    Rml::String list;
    for (const mods::Mod& m : used)
        list += "<br/>" + escape(m.name) + " (" + mods::kind_label(m.kind) + ", " + std::to_string(m.files) + " files)";
    info_row(leftPane, rightPane, "Mods in use", used.empty() ? Rml::String("None") : std::to_string(used.size()),
        used.empty() ? Rml::String("No mods are in use this session.")
                     : "In use this session, the first one first:<br/>" + list +
                           "<br/><br/>Changes to the rows above take effect the next time you start the game.");
    info_row(leftPane, rightPane, "Online play",
        mods::gameplay_in_use() ? Rml::String("Off: gameplay mods are on")
        : used.empty()          ? Rml::String("No mods")
                                : Rml::String("Not affected"),
        mods::gameplay_in_use()
            ? "These mods change how the game plays, so this session stays offline: " +
                  escape(mods::gameplay_names()) +
                  ".<br/><br/>Turn them off and restart the game to play online. Mods that only change how the game "
                  "looks or sounds can stay on."
            : Rml::String("Mods that only change how the game looks or sounds keep online play."));
    info_row(leftPane, rightPane, "About mods", "Read first",
        "Mods are made by players, not by this project, and nothing here checks them. Back up your save before you "
        "try one.<br/><br/>"
        "Mods made for the PS3 game or for RPCS3 work when they replace the game's files. Mods that change the game's "
        "program (an EBOOT.BIN, or RPCS3 patches) don't.<br/><br/>"
        "Mods that change only textures, models, text, sound or effects keep online play. Any other mod keeps the game "
        "offline while it is on, so that players with and without such mods don't meet.<br/><br/>"
        "Very large texture mods can run out of the game's memory and crash, as on a PS3.");
}

void ModsWindow::update() {
    if (sLookAgain) {  // (built again here, not inside the press)
        sLookAgain = false;
        refresh_active_tab();
    }
    Window::update();
}

}  // namespace ui
