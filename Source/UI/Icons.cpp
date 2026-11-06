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

void Icons::draw (juce::Graphics& g, const char* fileName, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto* icon = findIcon (fileName);
    if (icon == nullptr || area.isEmpty() || icon->area.isEmpty())
        return;

    // 色を塗り替えた複製を描く (アイコンは黒か、紺 #1C274C で描かれている)
    auto copy = icon->drawable->createCopy();
    copy->replaceColour (juce::Colours::black, colour);
    copy->replaceColour (juce::Colour (0xff1c274c), colour);
    copy->replaceColour (juce::Colour (0xffd9d9d9), colour.withMultipliedAlpha (0.25f));   // 並び替えの向きのアイコンの、薄い側
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
