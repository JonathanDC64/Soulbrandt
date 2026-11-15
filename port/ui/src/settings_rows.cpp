// Written for Demon's Souls. What a Settings row shows and does (settings_rows.hpp).
#include "settings_rows.hpp"

#include "settings.hpp"
#include "settings_values.hpp"

#include <algorithm>
#include <charconv>
#include <type_traits>

namespace ui::rows {
namespace {

using namespace catalog;

template <class... F>
struct Overload : F... {
    using F::operator()...;
};

// A choice row's list as it is now (its live one, made now), and the value in use (the blank choice for "").
std::vector<Choice> list_of(const RowDesc& row, const ChoiceRow& kind) {
    return kind.live ? kind.live(settings::in_use(row.key)) : kind.choices;
}

std::string current_of(const RowDesc& row, const ChoiceRow& kind) {
    const std::string v = settings::in_use(row.key);
    return v.empty() && !kind.blank.empty() ? kind.blank : v;
}

// The place of the value in use among the choices: past the list for the Custom choice, -1 for a value the row
// doesn't list and has no Custom choice for.
int index_of(const RowDesc& row, const ChoiceRow& kind, const std::vector<Choice>& list) {
    const std::string v = current_of(row, kind);
    const auto it = std::find_if(list.begin(), list.end(), [&](const Choice& c) { return c.value == v; });
    if (it != list.end()) return static_cast<int>(it - list.begin());
    return kind.custom ? static_cast<int>(list.size()) : -1;
}

bool toggle_on(const RowDesc& row, const ToggleRow& kind) {
    const std::string v = settings::in_use(row.key);
    const bool on = !v.empty() && v != "0";
    return kind.inverted ? !on : on;
}

// The game's own choice gets "(original)", unless its label says so already ("Original (4x)").
bool shows_original(const Choice& c) {
    return c.original && c.label.rfind("Original", 0) != 0;
}

bool own_lock(const RowDesc& row) {
    return row.locked && row.locked();
}

std::vector<Button> buttons_of(const RowDesc& row) {
    return row.buttons ? row.buttons() : std::vector<Button>{};
}

// A choice row set to its index-th choice (past the list: Custom). Whether it changed.
bool choose(const RowDesc& row, const ChoiceRow& kind, int index) {
    const std::vector<Choice> list = list_of(row, kind);
    const int count = static_cast<int>(list.size()) + (kind.custom ? 1 : 0);
    if (index < 0 || index >= count || index == index_of(row, kind, list)) return false;
    if (index < static_cast<int>(list.size())) {
        settings::set(row.key, list[index].value);
        if (kind.changed) kind.changed();
    } else {
        settings::set(row.key, kind.custom->value());
    }
    return true;
}

bool set_toggle(const RowDesc& row, const ToggleRow& kind, bool on) {
    if (toggle_on(row, kind) == on) return false;
    settings::set(row.key, on != kind.inverted ? "1" : "0");
    return true;
}

}  // namespace

Value value(const RowDesc& row) {
    if (own_lock(row)) {
        if (const std::string v = row.lockedValue(); !v.empty()) return {.text = v, .first = true, .last = true};
    }
    return std::visit(
        Overload{
            [&](const ChoiceRow& kind) -> Value {
                const std::vector<Choice> list = list_of(row, kind);
                const int i = index_of(row, kind, list);
                const int count = static_cast<int>(list.size()) + (kind.custom ? 1 : 0);
                Value v{.first = i <= 0, .last = i < 0 || i >= count - 1};
                if (i >= 0 && i < static_cast<int>(list.size())) {
                    v.text = list[i].label;
                    v.original = shows_original(list[i]);
                } else {
                    v.text = "Custom (" + settings::in_use(row.key) + ")";
                }
                return v;
            },
            [&](const ToggleRow& kind) -> Value {
                const bool on = toggle_on(row, kind);
                return {.text = on ? "On" : "Off", .first = !on, .last = on};
            },
            [&](const NumberRow& kind) -> Value {
                const int n = kind.get();
                return {.text = kind.format ? kind.format(n) : std::to_string(n) + kind.suffix,
                    .first = n <= kind.min,
                    .last = n >= kind.max,
                    .fill = kind.max > kind.min
                                ? std::clamp(static_cast<float>(n - kind.min) / static_cast<float>(kind.max - kind.min), 0.f, 1.f)
                                : 1.f};
            },
            [&](const TextRow&) -> Value {
                return {.text = settings::in_use(row.key), .first = true, .last = true};
            },
            [&](const InfoRow& kind) -> Value { return {.text = kind.value(), .first = true, .last = true}; },
            [&](const ActionRow&) -> Value { return {.first = true, .last = true}; },
        },
        row.kind);
}

bool modified(const RowDesc& row) {
    if (row.key.empty()) return false;
    if (const auto* n = std::get_if<NumberRow>(&row.kind); n && n->modified) return n->modified();
    return !settings::is_default(row.key);
}

bool locked(const RowDesc& row) {
    return (!row.key.empty() && settings::pinned(row.key)) || own_lock(row);
}

bool waits(const RowDesc& row) {
    if (row.key.empty()) return false;
    const std::vector<std::string> pending = settings::restart_pending();
    return std::find(pending.begin(), pending.end(), row.key) != pending.end();
}

bool steps(const RowDesc& row) {
    if (locked(row)) return false;
    return std::holds_alternative<ChoiceRow>(row.kind) || std::holds_alternative<ToggleRow>(row.kind) ||
           std::holds_alternative<NumberRow>(row.kind);
}

bool step(const RowDesc& row, int dir) {
    if (!steps(row) || dir == 0) return false;
    return std::visit(
        Overload{
            [&](const ChoiceRow& kind) {
                const int i = index_of(row, kind, list_of(row, kind));
                return choose(row, kind, i < 0 ? (dir > 0 ? 0 : -1) : i + (dir > 0 ? 1 : -1));
            },
            [&](const ToggleRow& kind) { return set_toggle(row, kind, dir > 0); },
            [&](const NumberRow& kind) {
                const int n = kind.get();
                const int next = std::clamp(n + (dir > 0 ? kind.step : -kind.step), kind.min, kind.max);
                if (next == n || !kind.set) return false;
                kind.set(next);
                return true;
            },
            [](const auto&) { return false; },
        },
        row.kind);
}

Enter enter(const RowDesc& row) {
    if (std::holds_alternative<ActionRow>(row.kind)) return Enter::Open;
    if (std::holds_alternative<InfoRow>(row.kind)) {
        const size_t n = buttons_of(row).size();
        return n == 0 ? Enter::None : n == 1 ? Enter::Press : Enter::List;
    }
    if (locked(row)) return Enter::None;
    if (std::holds_alternative<ChoiceRow>(row.kind) || std::holds_alternative<ToggleRow>(row.kind)) return Enter::List;
    return Enter::Edit;  // (a number or text)
}

std::string enter_label(const RowDesc& row) {
    switch (enter(row)) {
    case Enter::List: return std::holds_alternative<InfoRow>(row.kind) ? "Choose" : "List";
    case Enter::Edit: return "Type";
    case Enter::Open: return "Open";
    case Enter::Press: return buttons_of(row).front().text;
    case Enter::None: break;
    }
    return "";
}

std::vector<Item> items(const RowDesc& row) {
    std::vector<Item> out;
    if (const auto* kind = std::get_if<ChoiceRow>(&row.kind)) {
        const std::vector<Choice> list = list_of(row, *kind);
        const int i = index_of(row, *kind, list);
        for (size_t c = 0; c < list.size(); c++)
            out.push_back({.label = list[c].label, .original = shows_original(list[c]), .current = int(c) == i});
        if (kind->custom) out.push_back({.label = kind->custom->label, .current = i == int(list.size())});
    } else if (const auto* t = std::get_if<ToggleRow>(&row.kind)) {
        const bool on = toggle_on(row, *t);
        out.push_back({.label = "Off", .current = !on});
        out.push_back({.label = "On", .current = on});
    } else if (std::holds_alternative<InfoRow>(row.kind)) {
        for (const Button& b : buttons_of(row))
            out.push_back({.label = b.text, .disabled = b.disabled && b.disabled(), .button = true, .sound = b.sound});
    }
    return out;
}

bool pick(const RowDesc& row, int index) {
    if (std::holds_alternative<InfoRow>(row.kind)) {
        std::vector<Button> b = buttons_of(row);
        if (index < 0 || index >= static_cast<int>(b.size()) || (b[index].disabled && b[index].disabled())) return false;
        if (b[index].press) b[index].press();
        return true;
    }
    if (locked(row)) return false;
    if (const auto* kind = std::get_if<ChoiceRow>(&row.kind)) return choose(row, *kind, index);
    if (const auto* t = std::get_if<ToggleRow>(&row.kind)) return (index == 0 || index == 1) && set_toggle(row, *t, index == 1);
    return false;
}

std::string edit_text(const RowDesc& row) {
    if (const auto* n = std::get_if<NumberRow>(&row.kind)) return std::to_string(n->get());
    return settings::in_use(row.key);
}

bool commit_edit(const RowDesc& row, const std::string& typed) {
    if (locked(row)) return false;
    if (const auto* n = std::get_if<NumberRow>(&row.kind)) {
        int v = 0;
        const char* end = typed.data() + typed.size();
        const auto r = std::from_chars(typed.data(), end, v);
        if (r.ec != std::errc() || r.ptr != end) return false;
        v = std::clamp(v, n->min, n->max);
        if (v == n->get() || !n->set) return false;
        n->set(v);
        return true;
    }
    if (const auto* t = std::get_if<TextRow>(&row.kind)) {
        const std::string before = settings::in_use(row.key);
        if (t->set) t->set(typed);
        else settings::set(row.key, typed);
        return settings::in_use(row.key) != before;
    }
    return false;
}

int edit_limit(const RowDesc& row) {
    if (const auto* t = std::get_if<TextRow>(&row.kind)) return t->maxLength;
    if (const auto* n = std::get_if<NumberRow>(&row.kind))
        return static_cast<int>(std::max(std::to_string(n->min).size(), std::to_string(n->max).size()));
    return -1;
}

bool previewable(const RowDesc& row) {
    return (row.flags & kPreview) && !locked(row);
}

bool comparable(const RowDesc& row) {
    if (row.key.empty() || row.key == "DES_FULLSCREEN" || !steps(row)) return false;
    const settings::Def* d = settings::find(row.key);
    return d && d->apply == settings::Apply::Live;
}

std::string applies(const RowDesc& row) {
    if (row.key.empty()) return "";
    if (settings::pinned(row.key)) return "Set at start";
    const settings::Def* d = settings::find(row.key);
    return d && d->apply == settings::Apply::Restart ? "After a restart" : "At once";
}

std::string lock_note(const RowDesc& row) {
    if (row.key.empty() || !settings::pinned(row.key)) return "";
    const std::string v = settings::pinned_value(row.key);
    return "Set to " + (v.empty() ? std::string("(empty)") : v) + " when the game started (" + row.key +
           "), so it can't change here.";
}

}  // namespace ui::rows
