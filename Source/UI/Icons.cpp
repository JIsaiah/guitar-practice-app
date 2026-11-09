#include "Icons.h"
#include <BinaryData.h>
#include <map>

namespace
{
    // 読み込んだ SVG (1つのファイルにつき1回だけ読む)
    struct LoadedIcon
    {
        std::unique_ptr<juce::Drawable> drawable;
        juce::Rectangle<float> area;   // 描く範囲 (SVG の viewBox。形の座標はこの範囲で書かれている)
    };

    const LoadedIcon* findIcon (const char* fileName)
    {
        static std::map<juce::String, LoadedIcon> cache;

        if (auto it = cache.find (fileName); it != cache.end())
            return it->second.drawable != nullptr ? &it->second : nullptr;

        auto& loaded = cache[fileName];

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const auto* name = BinaryData::namedResourceList[i];
            if (juce::String (BinaryData::getNamedResourceOriginalFilename (name)) != fileName)
                continue;

            int size = 0;
            const auto* data = BinaryData::getNamedResource (name, size);
            const auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (data, size));
            if (xml == nullptr)
                break;

            loaded.drawable = juce::Drawable::createFromSVG (*xml);

            // 範囲は形の外枠ではなく、SVG の viewBox で決める
            // (外枠で決めると、形の無い大きな四角 (fill: none) を含むアイコンが小さくなるため。
            //  width・height (800px) は使わない。形の座標は viewBox (0〜24 など) のままで読み込まれる)
            const auto viewBox = juce::StringArray::fromTokens (xml->getStringAttribute ("viewBox"), " ,", {});
            if (viewBox.size() == 4)
                loaded.area = { viewBox[0].getFloatValue(), viewBox[1].getFloatValue(),
                                viewBox[2].getFloatValue(), viewBox[3].getFloatValue() };
            else if (loaded.drawable != nullptr)
                loaded.area = loaded.drawable->getDrawableBounds();
            break;
        }

        return loaded.drawable != nullptr ? &loaded : nullptr;
    }
}

namespace
{
    // アイコンの色を、テーマの色に塗り替える (ライト・ダークの両方で見えるように)。
    // アイコンごとに元の色が違う (黒、紺 #1C274C、濃い灰 #292D32 …) ので、色の名前ではなく明るさで決める:
    //   暗い色 → テーマの文字の色、明るい色 (並び替えの向きのアイコンの薄い側など) → それを薄くしたもの
    juce::Colour tintFor (juce::Colour original, juce::Colour colour)
    {
        if (original.isTransparent())
            return original;
        return original.getPerceivedBrightness() < 0.5f ? colour.withMultipliedAlpha (original.getFloatAlpha())
                                                        : colour.withMultipliedAlpha (0.25f * original.getFloatAlpha());
    }

    void tint (juce::Drawable& drawable, juce::Colour colour)
    {
        if (auto* shape = dynamic_cast<juce::DrawableShape*> (&drawable))
        {
            if (auto fill = shape->getFill(); fill.isColour())
                shape->setFill (tintFor (fill.colour, colour));
            if (auto stroke = shape->getStrokeFill(); stroke.isColour())
                shape->setStrokeFill (tintFor (stroke.colour, colour));
        }

        for (auto* child : drawable.getChildren())
            if (auto* d = dynamic_cast<juce::Drawable*> (child))
                tint (*d, colour);
    }
}

void Icons::draw (juce::Graphics& g, const char* fileName, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto* icon = findIcon (fileName);
    if (icon == nullptr || area.isEmpty() || icon->area.isEmpty())
        return;

    // 色を塗り替えた複製を描く
    auto copy = icon->drawable->createCopy();
    tint (*copy, colour);
    copy->setTransform ({});   // 形の座標 (viewBox) から、そのまま area へ

    const auto transform = juce::RectanglePlacement (juce::RectanglePlacement::centred).getTransformToFit (icon->area, area);
    copy->draw (g, 1.0f, transform);
}

//==============================================================================
void IconButton::setIcon (const char* iconFile)
{
    icon = iconFile;
    repaint();
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto& lf = getLookAndFeel();
    const auto background = findColour (getToggleState() ? buttonOnColourId : buttonColourId);
    lf.drawButtonBackground (g, *this, background, highlighted, down);

    auto colour = findColour (getToggleState() ? textColourOnId : textColourOffId);
    if (! isEnabled())
        colour = colour.withMultipliedAlpha (0.5f);

    const auto label = getButtonText();
    const auto font = lf.getTextButtonFont (*this, getHeight());
    const auto iconSize = icon != nullptr ? juce::jmin ((float) getHeight() * iconScale, (float) getWidth() - 8.0f) : 0.0f;

    float textWidth = 0.0f;
    if (label.isNotEmpty())
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, label, 0.0f, 0.0f);
        textWidth = glyphs.getBoundingBox (0, -1, true).getWidth();
    }

    // アイコンと文字をひとかたまりにして、真ん中に置く
    const auto gap = icon != nullptr && label.isNotEmpty() ? 4.0f : 0.0f;
    const auto total = juce::jmin ((float) getWidth() - 6.0f, iconSize + gap + textWidth);
    auto area = getLocalBounds().toFloat().withSizeKeepingCentre (total, (float) getHeight());

    const auto iconArea = [&] { return (iconAfterText ? area.removeFromRight (iconSize) : area.removeFromLeft (iconSize))
                                           .withSizeKeepingCentre (iconSize, iconSize); };

    if (icon != nullptr)
    {
        Icons::draw (g, icon, iconArea(), colour);
        if (iconAfterText) area.removeFromRight (gap); else area.removeFromLeft (gap);
    }

    if (label.isNotEmpty())
    {
        g.setColour (colour);
        g.setFont (font);
        g.drawText (label, area, juce::Justification::centred, true);
    }
}
