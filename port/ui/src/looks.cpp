// Written for Demon's Souls (looks.hpp).
#include "looks.hpp"

#include "host.hpp"
#include "settings.hpp"

#include <RmlUi/Core.h>

#include <string>

namespace ui::looks {

const Look& current() {
    const Look* look = find(settings::get("ui_look"));
    if (look && offered(*look)) return *look;
    return *find(kDefault);
}

void apply() {
    Rml::Context* context = host::context();
    if (!context) return;
    const Look& on = current();
    for (const Look& l : kLooks) context->ActivateTheme(std::string(l.id), l.id == on.id);
}

}  // namespace ui::looks
