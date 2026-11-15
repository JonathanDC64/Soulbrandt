// Written for Demon's Souls. The mods folder as the Mods screens show it (mods_state.hpp).
#include "mods_state.hpp"

#include "input.hpp"
#include "prompts.hpp"
#include "report.hpp"
#include "settings.hpp"
#include "sound.hpp"
#include "ui.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace ui {

std::vector<std::string> mods_good_to_know() {
    // (the keyboard's: Page Up and Page Down move a mod on both screens; Q and E only on a look's page)
    const prompts::Device device = input::menu_device();
    const std::string buttons = device == prompts::Device::Keyboard
                                    ? "Page Up and Page Down"
                                    : std::string(prompts::glyph(device, prompts::Key::PrevTab).text) + " and " +
                                          std::string(prompts::glyph(device, prompts::Key::NextTab).text);
    return {
        "Mods are made by players. Back up your save before you try one.",
        "Mods that only change how the game looks or sounds keep online play. Any other mod keeps you offline while "
        "it's on.",
        "When two mods change the same thing, the one higher in the list wins. To move a mod, use the arrows on its "
        "card, or " + buttons + ".",
        "Mods made for the PS3 game or for RPCS3 work when they replace the game's files. Very large texture mods can "
        "run out of memory, as on a PS3.",
    };
}

bool ModsState::fixed() {
    return settings::pinned("DES_MODS") || mods::from_environment();
}

bool ModsState::in_use(const std::string& name) {
    const auto& used = mods::in_use();
    return std::any_of(used.begin(), used.end(), [&](const mods::Mod& m) { return m.name == name; });
}

void ModsState::load() {
    dir = mods::folder(report::data_dir());
    all = settings::pinned("DES_MODS") ? mods::scan(dir, settings::pinned_value("DES_MODS")) : mods::scan(dir);
    for (mods::Mod& m : all) mods::plan(m);  // (loose files: where the game's own files say they go)
    refresh_overlaps();
}

void ModsState::save() {
    if (mods::write_order(dir, mods::entries(all))) return;
    play_sound(kSoundWarning);
    push_toast({
        .type = "warning",
        .title = "Your change wasn't saved",
        .content = "The mods folder can't be written to: " + mods::utf8(dir / mods::kOrderFile),
        .duration = std::chrono::seconds(8),
    });
}

void ModsState::refresh_overlaps() {
    std::vector<mods::Mod> on;
    for (const mods::Mod& m : all)
        if (m.usable() && m.enabled) on.push_back(m);
    overlaps = on.size() > 1 ? mods::overlaps(on) : std::vector<mods::Overlap>{};
    generation++;
}

std::size_t ModsState::usable() const {
    return std::count_if(all.begin(), all.end(), [](const mods::Mod& m) { return m.usable(); });
}

std::size_t ModsState::on() const {
    return std::count_if(all.begin(), all.end(), [](const mods::Mod& m) { return m.usable() && m.enabled; });
}

std::size_t ModsState::neighbour(std::size_t i, int step) const {
    for (long long j = (long long)i + step; j >= 0 && j < (long long)all.size(); j += step)
        if (all[(std::size_t)j].usable()) return (std::size_t)j;
    return all.size();
}

bool ModsState::restart_needed() const {
    if (mods::from_environment()) return false;
    std::vector<std::string> now, used;
    for (const mods::Mod& m : all)
        if (m.usable() && m.enabled) now.push_back(m.name);
    for (const mods::Mod& m : mods::in_use()) used.push_back(m.name);
    return now != used;
}

bool ModsState::pending(std::size_t i) const {
    return !mods::from_environment() && all[i].usable() && all[i].enabled != in_use(all[i].name);
}

std::size_t ModsState::move(std::size_t i, int step) {
    if (i >= all.size() || !all[i].usable()) return all.size();
    const std::size_t to = neighbour(i, step);
    if (fixed() || to >= all.size()) return all.size();
    std::swap(all[i], all[to]);
    save();
    refresh_overlaps();
    return to;
}

bool ModsState::toggle(std::size_t i) {
    if (i >= all.size() || !all[i].usable() || fixed()) return false;
    all[i].enabled = !all[i].enabled;
    save();
    refresh_overlaps();
    return true;
}

std::string ModsState::fix(std::size_t i) {
    if (i >= all.size() || all[i].why != mods::Problem::NotInPlace) return "it has no files to put in place";
    const mods::Mod& m = all[i];
    const std::string err = mods::place(m);
    push_toast({
        .type = err.empty() ? "" : "warning",
        .title = err.empty() ? "Mod fixed" : "Mod not fixed",
        .content = err.empty() ? m.name + " is ready. Select it to turn it on."
                               : m.name + ": " + err + ". Some of its files may have moved already.",
        .duration = std::chrono::seconds(6),
    });
    lookAgain = true;
    return err;
}

std::string ModsState::summary_line() const {
    const std::size_t u = usable(), n = on();
    if (all.empty()) return "No mods yet";
    if (u == 0) return "None of your mods can be used yet";
    if (n == 0) return "No mods on (" + std::to_string(u) + " in the list)";
    return std::to_string(n) + " of " + std::to_string(u) + (u == 1 ? " mod on" : " mods on");
}

std::string ModsState::covered_by(std::size_t i) const {
    if (i >= all.size()) return "";
    for (const mods::Overlap& o : overlaps)
        if (o.covered == all[i].name) return o.by;
    return "";
}

}  // namespace ui
