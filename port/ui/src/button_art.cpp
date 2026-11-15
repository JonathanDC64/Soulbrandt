// Written for Demon's Souls. <menu-art>, the menus' pictures of buttons (button_art.hpp).
#include "button_art.hpp"

#include "button_pages.hpp"
#include "ui.hpp"
#include "ui_assets.hpp"

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/PropertyIdSet.h>
#include <RmlUi/Core/RenderManager.h>

#include <cmath>
#include <iterator>
#include <sstream>

namespace ui::button_art {
namespace {

namespace bp = button_pages;

// The device attribute's values (prompts::Device's order, as DES_GLYPHS names them) and what each draws.
constexpr std::string_view kDevices[] = {"keyboard", "playstation", "xbox", "nintendo", "steamdeck"};
constexpr bp::Kind kKinds[] = {bp::Kind::Keyboard, bp::Kind::PlayStation, bp::Kind::Xbox, bp::Kind::Nintendo,
                               bp::Kind::SteamDeck};
// The D-pad's ways, in Art::code's bit order.
constexpr std::string_view kWays[] = {"up", "right", "down", "left"};

// The letters on the pictures: the font the game's prompts are drawn again with (button_icons.cpp).
const unsigned char* letters() {
    static const Asset* const font = find_asset("res/fonts/AlegreyaSans-Bold.ttf");
    return font ? font->data : nullptr;
}

// The picture an element's attributes name.
bp::Single read(Rml::Element& e) {
    bp::Single s;
    const Rml::String device = e.GetAttribute<Rml::String>("device", "");
    for (size_t i = 0; i < std::size(kDevices); i++)
        if (device == kDevices[i]) s.kind = kKinds[i];
    if (s.kind == bp::Kind::Keyboard) {
        s.key.in = e.GetAttribute<int>("key", 0);
        s.key.label = e.GetAttribute<Rml::String>("cap", "");
        return s;
    }
    const Rml::String button = e.GetAttribute<Rml::String>("button", "");
    for (int i = 0; i < 16; i++)
        if (button == bp::kPadNames[i]) s.pad = i;
    std::istringstream ways(e.GetAttribute<Rml::String>("dpad", ""));
    for (std::string way; ways >> way;)
        for (size_t i = 0; i < std::size(kWays); i++)
            if (way == kWays[i]) s.dpad |= 1u << i;
    return s;
}

// <menu-art>: a replaced element, like <img>, whose picture is drawn for the pixels it covers each time that size
// changes (a callback texture: drawn when it is first shown).
class ArtElement final : public Rml::Element {
public:
    explicit ArtElement(const Rml::String& tag) : Rml::Element(tag) {}

    bool GetIntrinsicDimensions(Rml::Vector2f& dimensions, float& ratio) override {
        if (mStale) {
            mSingle = read(*this);
            mRatio = bp::single_ratio(mSingle, letters());
            mStale = false;
        }
        // A size for a sheet that gives none; the look's height wins, and its width follows from the ratio.
        const float h = 24.f * Rml::ElementUtilities::GetDensityIndependentPixelRatio(this);
        dimensions = {h * mRatio, h};
        ratio = mRatio;
        return true;
    }

protected:
    void OnAttributeChange(const Rml::ElementAttributes& changed) override {
        Rml::Element::OnAttributeChange(changed);
        mStale = true;
        mDrawnSize = {0, 0};
        DirtyLayout();
    }

    void OnPropertyChange(const Rml::PropertyIdSet& changed) override {
        Rml::Element::OnPropertyChange(changed);
        if (changed.Contains(Rml::PropertyId::ImageColor) || changed.Contains(Rml::PropertyId::Opacity)) mQuadStale = true;
    }

    void OnRender() override {
        Rml::RenderManager* render = GetRenderManager();
        if (!render) return;
        const Rml::Vector2f box = GetBox().GetSize(Rml::BoxArea::Content);
        const Rml::Vector2i size{static_cast<int>(std::lround(box.x)), static_cast<int>(std::lround(box.y))};
        if (size.x <= 0 || size.y <= 0) return;
        if (size != mDrawnSize) {
            mDrawnSize = size;
            const bp::Single single = mSingle;
            mTexture = render->MakeCallbackTexture([single, size](const Rml::CallbackTextureInterface& texture) {
                const bp::Image image = bp::draw_single(single, size.x, size.y, letters());
                return !image.rgba.empty() &&
                       texture.GenerateTexture(Rml::Span<const Rml::byte>(image.rgba.data(), image.rgba.size()), size);
            });
            mQuadStale = true;
        }
        if (mQuadStale) {
            Rml::Mesh mesh = mQuad.Release(Rml::Geometry::ReleaseMode::ClearMesh);
            const Rml::ComputedValues& computed = GetComputedValues();
            Rml::MeshUtilities::GenerateQuad(mesh, {0, 0}, Rml::Vector2f(size), computed.image_color().ToPremultiplied(computed.opacity()),
                                             {0, 0}, {1, 1});
            mQuad = render->MakeGeometry(std::move(mesh));
            mQuadStale = false;
        }
        // On whole pixels: the picture is drawn at its size, so it stays sharp.
        const Rml::Vector2f at = GetAbsoluteOffset(Rml::BoxArea::Content);
        mQuad.Render({std::round(at.x), std::round(at.y)}, mTexture);
    }

private:
    bp::Single mSingle;
    float mRatio = 1.f;
    bool mStale = true;                 // the attributes not read yet
    Rml::Vector2i mDrawnSize{0, 0};     // the size the texture is drawn for
    Rml::CallbackTexture mTexture;
    Rml::Geometry mQuad;
    bool mQuadStale = true;
};

Rml::ElementInstancerGeneric<ArtElement> sInstancer;

}  // namespace

std::string rml(prompts::Device device, const prompts::Art& art) {
    const auto d = static_cast<size_t>(device);
    if (d >= std::size(kDevices)) return {};
    const std::string at = "<menu-art device=\"" + std::string(kDevices[d]) + "\" ";
    switch (art.is) {
    case prompts::Art::Is::Button: return at + "button=\"" + std::string(art.name) + "\"/>";
    case prompts::Art::Is::Dpad: {
        std::string ways;
        for (size_t i = 0; i < std::size(kWays); i++)
            if ((art.code >> i) & 1) ways += (ways.empty() ? "" : " ") + std::string(kWays[i]);
        return at + "dpad=\"" + ways + "\"/>";
    }
    case prompts::Art::Is::Key:
        return at + "key=\"" + std::to_string(art.code) + "\" cap=\"" + escape(art.name) + "\"/>";
    case prompts::Art::Is::None: break;
    }
    return {};
}

void install() { Rml::Factory::RegisterElementInstancer("menu-art", &sInstancer); }

}  // namespace ui::button_art
