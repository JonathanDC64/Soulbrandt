// Written for Demon's Souls. A page of the F1 menu drawn inside the menu's own document (menu_shell.hpp): Settings so
// far. The menu gives it an element to build in and the commands its keys give (menu_input.hpp); the page says what
// its prompts are, and which classes the menu's body takes while it shows (a look styles them: looks.hpp).
#pragma once

#include "menu_input.hpp"
#include "prompts.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ui {
class Document;
}

namespace ui::menu {

// What a page asks of the menu around it.
class PageHost {
public:
    virtual ~PageHost() = default;
    // A line over the game while Compare is held ("Before: Off"), kind "before" or "now"; "" takes it away. A "now"
    // line goes by itself after a moment.
    virtual void badge(const std::string& text, const std::string& kind) = 0;
    // Another document over the menu (Controls, the HUD editor); the menu shows again when it closes.
    virtual void push(std::unique_ptr<Document> document) = 0;
};

class Page {
public:
    virtual ~Page() = default;

    virtual std::string title() const = 0;  // its entry's ("Settings")
    virtual void update() = 0;              // once a frame while it shows

    // A command, as its key goes down (down) or up (the held ones, Compare and Hide): whether the page used it.
    virtual bool command(Command command, bool down) = 0;
    // Back: whether the page closed something of its own (a list, the preview); false: the menu goes back.
    virtual bool back() = 0;
    // What is held lets go (the window lost the focus, the menu hides).
    virtual void release() {}
    // The menu hides with the page open (a document over it, Controls): what it changed is kept (Settings saves).
    virtual void hidden() {}

    virtual std::vector<prompts::Prompt> prompts(const std::string& backLabel) const = 0;
    virtual std::string classes() const { return ""; }  // the menu body's, while it shows ("preview listing")
    virtual void closing() {}                            // the page goes (the menu closes, or back to its entries)
};

}  // namespace ui::menu
