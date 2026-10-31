#include "MetalLogoCache.h"
#include "MetalLogoGenerator.h"

namespace
{
    // ウィンドウの大きさを何度も変えたときに、古い大きさの画像がたまり続けないように
    constexpr size_t maxImages = 128;
}

MetalLogoCache::MetalLogoCache (juce::Typeface::Ptr t)
    : typeface (std::move (t))
{
}

void MetalLogoCache::setColour (juce::Colour newColour)
{
    if (colour != newColour)
    {
        colour = newColour;
        images.clear();
    }
}

void MetalLogoCache::clear()
{
    paths.clear();
    images.clear();
}

const juce::Path& MetalLogoCache::getPath (const juce::String& text)
{
    const auto key = text + "#" + juce::String (params.hash());

    if (auto it = paths.find (key); it != paths.end())
        return it->second;

    const auto start = juce::Time::getMillisecondCounterHiRes();
    auto path = MetalLogoGenerator::generate (text, juce::Font (juce::FontOptions (typeface)), params,
                                              MetalLogoGenerator::seedFor (text));
    DBG ("MetalLogo: \"" << text << "\" " << juce::String (juce::Time::getMillisecondCounterHiRes() - start, 2) << " ms");
    juce::ignoreUnused (start);   // Release ビルドでは DBG が消えるので

    return paths.emplace (key, std::move (path)).first->second;
}

juce::Image MetalLogoCache::getImage (const juce::String& text, int width, int height, float scale)
{
    const auto pixelWidth = juce::roundToInt ((float) width * scale);
    const auto pixelHeight = juce::roundToInt ((float) height * scale);

    if (pixelWidth < 1 || pixelHeight < 1 || typeface == nullptr)
        return {};

    const auto key = text + "|" + juce::String (pixelWidth) + "x" + juce::String (pixelHeight)
                   + "#" + juce::String (params.hash());

    if (auto it = images.find (key); it != images.end())
        return it->second;

    const auto& path = getPath (text);
    if (path.isEmpty())
        return {};

    juce::Image image (juce::Image::ARGB, pixelWidth, pixelHeight, true);
    {
        juce::Graphics g (image);
        g.setColour (colour);
        g.fillPath (path, juce::RectanglePlacement (juce::RectanglePlacement::centred)
                              .getTransformToFit (MetalLogoGenerator::getLogoArea (path, params),
                                                  image.getBounds().toFloat()));
    }

    if (images.size() >= maxImages)
        images.clear();

    images.emplace (key, image);
    return image;
}
