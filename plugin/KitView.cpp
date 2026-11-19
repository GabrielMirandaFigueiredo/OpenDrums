#include "KitView.h"

#include "SceneGeometry.hpp"

#include <algorithm>

namespace
{
juce::String T(const char* utf8) { return juce::String::fromUTF8(utf8); }

const juce::Colour kAccent(0xffe8a33d);

juce::Rectangle<float> toJuce(const od::geo::Rect& r) { return {r.x, r.y, r.w, r.h}; }

// Draws the primitives produced by core/KitGeometry.
void drawPrims(juce::Graphics& g, const std::vector<od::geo::Prim>& prims)
{
    using Kind = od::geo::Prim::Kind;
    for (const auto& p : prims) {
        g.setColour(juce::Colour(p.color));
        switch (p.kind) {
        case Kind::Line: {
            juce::Path path;
            path.startNewSubPath(p.x1, p.y1);
            path.lineTo(p.x2, p.y2);
            g.strokePath(path, juce::PathStrokeType(p.width, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
            break;
        }
        case Kind::Ellipse:
            g.fillEllipse(p.x1 - p.x2, p.y1 - p.y2, p.x2 * 2.0f, p.y2 * 2.0f);
            break;
        case Kind::RoundRect:
            g.fillRoundedRectangle(p.x1, p.y1, p.x2, p.y2, p.width);
            break;
        case Kind::Poly: {
            if (p.pts.size() < 6)
                break;
            juce::Path path;
            path.startNewSubPath(p.pts[0], p.pts[1]);
            for (size_t i = 2; i + 1 < p.pts.size(); i += 2)
                path.lineTo(p.pts[i], p.pts[i + 1]);
            path.closeSubPath();
            g.fillPath(path);
            break;
        }
        }
    }
}

juce::AffineTransform toJuce(const od::geo::Xform& x)
{
    // same order as the SVG used by tools/preview_kit: scale, then rotate (both about the pivot), then move
    return juce::AffineTransform::scale(x.sx, x.sy, x.px, x.py).rotated(x.rot, x.px, x.py).translated(x.dx, x.dy);
}
} // namespace

KitView::KitView() = default;

juce::String KitView::displayNameForSlot(const juce::String& slot)
{
    static const std::map<juce::String, juce::String> names = {
        {"kick", T("Bumbo")},       {"snare", T("Caixa")},       {"tom1", T("Tom 1")},
        {"tom2", T("Tom 2")},       {"tom3", T("Tom 3")},        {"floor", T("Surdo")},
        {"hihat", T("Chimbal")},    {"crash1", T("Ataque 1")},   {"crash2", T("Ataque 2")},
        {"ride", T("Condução")},    {"splash", T("Splash")},     {"china", T("China")},
        {"cowbell", T("Cowbell")},  {"tambourine", T("Pandeiro")}};
    auto it = names.find(slot);
    return it != names.end() ? it->second : slot;
}

std::shared_ptr<juce::Drawable> KitView::drawableFor(const std::filesystem::path& file)
{
    if (file.empty())
        return nullptr;
    const juce::String key(file.string());
    auto it = cache_.find(key);
    if (it != cache_.end())
        return it->second;

    std::shared_ptr<juce::Drawable> drawable;
    const juce::File f(key);
    if (f.existsAsFile())
        drawable = std::shared_ptr<juce::Drawable>(juce::Drawable::createFromImageFile(f).release());
    cache_[key] = drawable; // cache misses too, so a missing picture is not retried on every repaint
    return drawable;
}

void KitView::setRig(std::shared_ptr<const od::Rig> rig)
{
    stopTimer();
    items_.clear();
    if (rig != nullptr) {
        for (const auto& slot : rig->slots) {
            if (slot.view.pieceRef.empty())
                continue; // slot without a loadable piece
            Item item;
            item.view = slot.view;
            item.drawable = drawableFor(slot.view.imagePath);
            items_.push_back(std::move(item));
        }
        std::stable_sort(items_.begin(), items_.end(),
                         [](const Item& a, const Item& b) { return a.view.y < b.view.y; });
    }
    hovered_ = -1;
    layoutItems();
    repaint();
}

void KitView::setSwappedSlots(const juce::StringArray& slots)
{
    swapped_ = slots;
    repaint();
}

void KitView::setRoom(int roomIndex)
{
    room_ = roomIndex;
    rebuildScene();
    repaint();
}

void KitView::rebuildScene()
{
    scene_.clear();
    if (room_ >= 0 && getWidth() > 0 && getHeight() > 0)
        scene_ = od::geo::buildScene(room_, (float) getWidth(), (float) getHeight());
}

void KitView::setSelectedSlot(const juce::String& slot)
{
    selected_ = slot;
    repaint();
}

void KitView::triggerHit(const juce::String& slot, const juce::String& role, int velocity)
{
    const std::string id = slot.toStdString();
    for (auto& item : items_) {
        if (item.view.slot != id)
            continue;
        item.anim.t = 0.0f;
        item.anim.amp = od::geo::ampForVelocity(velocity);
        item.anim.role = role.toStdString();
        if (!isTimerRunning()) {
            lastTickMs_ = juce::Time::getMillisecondCounterHiRes();
            startTimerHz(60);
        }
        repaint();
        return;
    }
}

void KitView::layoutItems()
{
    const float W = static_cast<float>(getWidth());
    const float H = static_cast<float>(getHeight());
    for (auto& item : items_) {
        float aspect = 1.0f;
        if (item.drawable != nullptr) {
            const auto b = item.drawable->getDrawableBounds();
            if (b.getWidth() > 0.0f)
                aspect = b.getHeight() / b.getWidth();
        }
        item.bounds = od::geo::computeBounds(item.view.x, item.view.y, item.view.diameter, item.view.scale,
                                             aspect, W, H);
    }
}

int KitView::itemAt(juce::Point<float> p) const
{
    for (int i = static_cast<int>(items_.size()) - 1; i >= 0; --i) // front to back
        if (toJuce(items_[static_cast<size_t>(i)].bounds).contains(p))
            return i;
    return -1;
}

// The swap badge sits on the top-right corner of every piece. At rest it is a small round icon; on the hovered or
// selected piece it grows into a pill that says what it does.
juce::Rectangle<float> KitView::badgeRect(size_t i, bool expanded) const
{
    const auto b = toJuce(items_[i].bounds);
    const float w = expanded ? 84.0f : 22.0f, h = 22.0f;
    float x = b.getRight() - 12.0f - (expanded ? w - 22.0f : 0.0f);
    x = juce::jlimit(2.0f, static_cast<float>(getWidth()) - w - 2.0f, x);
    const float y = juce::jlimit(2.0f, static_cast<float>(getHeight()) - h - 2.0f, b.getY() - 12.0f);
    return {x, y, w, h};
}

int KitView::badgeAt(juce::Point<float> p) const
{
    for (int i = static_cast<int>(items_.size()) - 1; i >= 0; --i) {
        const bool open = items_[static_cast<size_t>(i)].view.slot == selected_.toStdString() || i == hovered_ || i == hoveredBadge_;
        if (badgeRect(static_cast<size_t>(i), open).expanded(2.0f).contains(p))
            return i;
    }
    return -1;
}

void KitView::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    if (!scene_.empty()) {
        drawPrims(g, scene_);
    } else {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff23262d), area.getCentreX(), area.getCentreY(),
                                               juce::Colour(0xff111317), 0.0f, 0.0f, true));
        g.fillRect(area);
        // a soft "rug" under the kit
        g.setColour(juce::Colour(0x14ffffff));
        g.fillEllipse(area.withSizeKeepingCentre(area.getWidth() * 0.86f, area.getHeight() * 0.62f)
                          .translated(0.0f, area.getHeight() * 0.1f));
    }

    if (items_.empty()) {
        g.setColour(juce::Colours::grey);
        g.setFont(juce::FontOptions(16.0f));
        g.drawFittedText(T("Nenhum kit carregado"), getLocalBounds(), juce::Justification::centred, 2);
        return;
    }

    const float viewH = static_cast<float>(getHeight());
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        const bool isSelected = item.view.slot == selected_.toStdString();
        const bool isHovered = static_cast<int>(i) == hovered_;
        const auto geometry = od::geo::buildSlot(item.view.slot, item.view.family, item.bounds, viewH, item.anim);

        drawPrims(g, geometry.behind); // stand, legs, spurs, feet

        const auto bounds = toJuce(item.bounds);
        if (item.drawable != nullptr) {
            const auto fit = juce::RectanglePlacement(juce::RectanglePlacement::stretchToFit)
                                 .getTransformToFit(item.drawable->getDrawableBounds(), bounds);
            item.drawable->draw(g, 1.0f, fit.followedBy(toJuce(geometry.piece))); // squash / wobble / dip
        } else { // no picture: generic placeholder, so the slot is still clickable
            g.setColour(juce::Colour(0xff4a4f5a));
            g.fillEllipse(bounds);
        }

        drawPrims(g, geometry.front); // pedal + beater, hi-hat pedal, head glow

        if (isSelected || isHovered) {
            g.setColour(isSelected ? kAccent : juce::Colours::white.withAlpha(0.35f));
            g.drawRoundedRectangle(bounds.expanded(3.0f), 6.0f, isSelected ? 2.0f : 1.0f);
        }
    }

    // Second pass: captions and "swapped" markers, so a piece in front never hides a name.
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        const auto bounds = toJuce(item.bounds);
        const bool isSelected = item.view.slot == selected_.toStdString();

        g.setFont(juce::FontOptions(12.0f));
        g.setColour(isSelected ? kAccent : juce::Colours::lightgrey.withAlpha(0.8f));
        const auto caption = bounds.withY(bounds.getBottom() + 3.0f).withHeight(16.0f);
        g.drawText(displayNameForSlot(juce::String(item.view.slot)), caption.toNearestInt(),
                   juce::Justification::centred);

        if (swapped_.contains(juce::String(item.view.slot))) { // piece from another kit: dot on the top-left corner
            g.setColour(kAccent);
            g.fillEllipse(bounds.getX() - 4.0f, bounds.getY() - 4.0f, 9.0f, 9.0f);
        }
    }

    // Third pass: the swap badges, on top of everything.
    for (size_t i = 0; i < items_.size(); ++i) {
        const bool isSelected = items_[i].view.slot == selected_.toStdString();
        const bool open = isSelected || static_cast<int>(i) == hovered_ || static_cast<int>(i) == hoveredBadge_;
        const auto r = badgeRect(i, open);
        const bool direct = static_cast<int>(i) == hoveredBadge_;
        g.setColour(juce::Colour(0xff14161a).withAlpha(0.35f));
        g.fillRoundedRectangle(r.translated(0.0f, 1.5f), 11.0f);                          // drop shadow
        g.setColour(direct ? kAccent : (open ? juce::Colour(0xff2a2f3a) : juce::Colour(0xff2a2f3a).withAlpha(0.88f)));
        g.fillRoundedRectangle(r, 11.0f);
        g.setColour(open || direct ? kAccent : juce::Colour(0xffe8a33d).withAlpha(0.8f));
        g.drawRoundedRectangle(r, 11.0f, 1.5f);

        // two arrows, one each way
        const auto ic = juce::Rectangle<float>(r.getX() + 4.0f, r.getY() + 4.0f, 14.0f, 14.0f);
        g.setColour(direct ? juce::Colour(0xff14161a) : kAccent);
        g.drawArrow(juce::Line<float>(ic.getX(), ic.getY() + 4.0f, ic.getRight(), ic.getY() + 4.0f), 1.6f, 5.0f, 4.5f);
        g.drawArrow(juce::Line<float>(ic.getRight(), ic.getBottom() - 4.0f, ic.getX(), ic.getBottom() - 4.0f), 1.6f, 5.0f, 4.5f);
        if (open) {
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(T("Trocar"), juce::Rectangle<float>(r.getX() + 24.0f, r.getY(), r.getWidth() - 28.0f, r.getHeight()),
                       juce::Justification::centredLeft);
        }
    }
}

void KitView::mouseDown(const juce::MouseEvent& e)
{
    const int badge = badgeAt(e.position);
    if (badge >= 0) { // the swap badge: select the piece and open the menu, without playing it
        const juce::String slot(items_[static_cast<size_t>(badge)].view.slot);
        selected_ = slot;
        if (onSlotSelected) onSlotSelected(slot);
        if (onSlotMenuRequested) onSlotMenuRequested(slot);
        repaint();
        return;
    }
    const int i = itemAt(e.position);
    if (i < 0)
        return;
    const juce::String slot(items_[static_cast<size_t>(i)].view.slot);
    selected_ = slot;
    if (onSlotSelected)
        onSlotSelected(slot);

    if (e.mods.isPopupMenu()) {
        if (onSlotMenuRequested)
            onSlotMenuRequested(slot);
    } else if (onSlotHit) {
        onSlotHit(slot); // the engine reports the hit back, and triggerHit() starts the animation
    }
    repaint();
}

void KitView::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int i = itemAt(e.position);
    if (i >= 0 && onSlotMenuRequested)
        onSlotMenuRequested(juce::String(items_[static_cast<size_t>(i)].view.slot));
}

void KitView::mouseMove(const juce::MouseEvent& e)
{
    const int i = itemAt(e.position);
    const int badge = badgeAt(e.position);
    if (i != hovered_ || badge != hoveredBadge_) {
        hovered_ = i;
        hoveredBadge_ = badge;
        setMouseCursor(i >= 0 || badge >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void KitView::mouseExit(const juce::MouseEvent&)
{
    if (hovered_ != -1 || hoveredBadge_ != -1) {
        hovered_ = -1;
        hoveredBadge_ = -1;
        repaint();
    }
}

void KitView::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = juce::jlimit(0.0f, 0.1f, static_cast<float>((now - lastTickMs_) * 0.001));
    lastTickMs_ = now;

    bool any = false;
    for (auto& item : items_) {
        if (item.anim.t >= od::geo::animDuration(item.view.family))
            continue; // at rest
        item.anim.t += dt;
        if (item.anim.t >= od::geo::animDuration(item.view.family))
            item.anim.t = 1.0e9f; // settled: back to the resting pose (drawn one last time below)
        else
            any = true;
    }
    if (!any)
        stopTimer();
    repaint();
}
