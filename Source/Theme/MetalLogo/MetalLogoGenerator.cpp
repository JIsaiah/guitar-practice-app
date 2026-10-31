#include "MetalLogoGenerator.h"

namespace
{
    using Point = juce::Point<float>;
    using Contour = std::vector<Point>;   // 1つの輪郭 (閉じた折れ線)。最後の点と最初の点がつながっている

    // 1文字分の形
    struct Glyph
    {
        juce::Path original;              // フォントから取り出したままの形 (延長の「外向き」判定に使う)
        std::vector<Contour> contours;    // 延長などで変形していく輪郭
    };

    float cross (Point a, Point b) noexcept { return a.x * b.y - a.y * b.x; }

    Point normalised (Point p) noexcept
    {
        const auto length = p.getDistanceFromOrigin();
        return length > 0.0f ? p / length : Point();
    }

    // 符号付き面積 (靴ひも公式)。符号が輪郭の回転方向を表す
    float signedArea (const Contour& c) noexcept
    {
        float sum = 0.0f;
        for (size_t i = 0; i < c.size(); ++i)
            sum += cross (c[i], c[(i + 1) % c.size()]);
        return sum * 0.5f;
    }

    juce::Path toPath (const std::vector<Contour>& contours)
    {
        juce::Path path;
        for (const auto& c : contours)
        {
            path.startNewSubPath (c.front());
            for (size_t i = 1; i < c.size(); ++i)
                path.lineTo (c[i]);
            path.closeSubPath();
        }
        path.setUsingNonZeroWinding (true);
        return path;
    }

    //==========================================================================
    // Step 1: 文字を並べ、文字間隔を広げて、1文字ずつの Path にする
    std::vector<Glyph> layOut (const juce::String& text, const juce::Font& baseFont, const MetalLogoParams& params)
    {
        const auto font = baseFont.withHeight (params.fontHeight);

        juce::GlyphArrangement arrangement;
        arrangement.addLineOfText (font, text, 0.0f, 0.0f);

        std::vector<Glyph> glyphs;
        for (int i = 0; i < arrangement.getNumGlyphs(); ++i)
        {
            Glyph glyph;
            arrangement.getGlyph (i).createPath (glyph.original);

            if (glyph.original.isEmpty())   // 空白など
                continue;

            glyph.original.applyTransform (juce::AffineTransform::translation ((float) i * params.fontHeight * params.tracking, 0.0f));
            glyph.original.setUsingNonZeroWinding (true);
            glyphs.push_back (std::move (glyph));
        }
        return glyphs;
    }

    //==========================================================================
    // Step 2: 曲線を折れ線にし、輪郭ごとの頂点列にする
    Contour simplify (const Contour& input, float minTurnDeg)
    {
        constexpr float duplicateDistance = 0.05f;
        const auto minSin = std::sin (juce::degreesToRadians (minTurnDeg));

        // 連続する重複頂点を除く (最後と最初の重複も)
        Contour points;
        for (const auto& p : input)
            if (points.empty() || points.back().getDistanceFrom (p) > duplicateDistance)
                points.push_back (p);

        while (points.size() > 1 && points.back().getDistanceFrom (points.front()) <= duplicateDistance)
            points.pop_back();

        // ほぼ直線上の頂点を除く (前後の辺のなす角が小さく、向きが同じもの)
        bool removed = true;
        while (removed && points.size() > 3)
        {
            removed = false;
            Contour kept;
            const auto n = points.size();

            for (size_t i = 0; i < n; ++i)
            {
                const auto prev = kept.empty() ? points[(i + n - 1) % n] : kept.back();
                const auto next = points[(i + 1) % n];
                const auto in = points[i] - prev, out = next - points[i];
                const auto lengths = in.getDistanceFromOrigin() * out.getDistanceFromOrigin();

                const auto remaining = n - (i - kept.size());   // この回でまだ消していない頂点の数
                if (remaining > 3 && lengths > 0.0f && in.getDotProduct (out) > 0.0f
                    && std::abs (cross (in, out)) < minSin * lengths)
                {
                    removed = true;
                    continue;
                }
                kept.push_back (points[i]);
            }
            points = std::move (kept);
        }

        return points;
    }

    std::vector<Contour> toContours (const juce::Path& path, const MetalLogoParams& params)
    {
        std::vector<Contour> contours;
        juce::PathFlatteningIterator it (path, {}, params.flattenTolerance);

        while (it.next())
        {
            // subPathIndex は「輪郭の中で何本目の線分か」。0 なら新しい輪郭の始まり
            if (it.subPathIndex == 0 || contours.empty())
                contours.push_back ({ { it.x1, it.y1 } });

            contours.back().push_back ({ it.x2, it.y2 });
        }

        std::vector<Contour> result;
        for (const auto& c : contours)
            if (auto simplified = simplify (c, params.simplifyAngleDeg); simplified.size() >= 3)
                result.push_back (std::move (simplified));

        return result;
    }

    //==========================================================================
    // Step 3: はらい・はねの延長。鋭い凸の角を、角の二等分線の外向きに引き伸ばす
    void extendTips (Glyph& glyph, const MetalLogoParams& params, juce::Random& random)
    {
        const auto cosLimit = std::cos (juce::degreesToRadians (params.tipAngleDeg));

        for (auto& c : glyph.contours)
        {
            const auto n = (int) c.size();
            const auto falloff = juce::jmin (params.tipFalloffVerts, (n - 1) / 2);
            std::vector<Point> offsets ((size_t) n);

            for (int i = 0; i < n; ++i)
            {
                const auto a = c[(size_t) ((i + n - 1) % n)], b = c[(size_t) i], d = c[(size_t) ((i + 1) % n)];
                const auto lengthIn = a.getDistanceFrom (b), lengthOut = b.getDistanceFrom (d);
                if (lengthIn <= 0.0f || lengthOut <= 0.0f)
                    continue;

                // 曲がる角度 (進行方向の変化) が tipAngleDeg 以上か
                const auto cosTurn = (b - a).getDotProduct (d - b) / (lengthIn * lengthOut);
                if (cosTurn > cosLimit)
                    continue;

                // 角の二等分線は角の内側を向くので、その逆が外向き
                const auto outward = normalised (-((a - b) / lengthIn + (d - b) / lengthOut));
                if (outward.isOrigin())
                    continue;

                // 凸の角だけ: 外向きに少し進んだ点が、元の文字の外にあること
                if (glyph.original.contains (b + outward * 1.5f))
                    continue;

                auto amount = params.fontHeight * params.tipExtend
                            * (1.0f + params.tipJitter * (random.nextFloat() * 2.0f - 1.0f));

                // 引き伸ばしすぎると輪郭が自己交差するので、隣の頂点までの距離 (短いほう) の数倍までにする
                amount = juce::jmin (amount, params.tipMaxStretch * juce::jmin (lengthIn, lengthOut));

                // 伸ばした先が、同じ文字の別の線に入るなら伸ばさない (線同士がくっついて穴が埋まるのを防ぐ)
                if (amount <= 0.0f || glyph.original.contains (b + outward * amount))
                    continue;

                // 角の頂点と、前後 tipFalloffVerts 個の頂点を、角からの距離 (輪郭に沿った長さ) に応じて弱めながら引っ張る。
                // 距離が radius を超えた頂点は動かさない。
                // 弱まり方がゆるやか (radius > amount) なので、隣り合う頂点の動きの差が辺の長さより小さく、輪郭が折り返さない
                const auto radius = amount * params.tipFalloffRadius;

                const auto pull = [&] (int index, float distance)
                {
                    const auto offset = outward * (amount * (1.0f - distance / radius));
                    auto& current = offsets[(size_t) index];

                    // 近くの角が同じ頂点を引っ張るときは、足し合わせず大きいほうだけ使う
                    if (offset.getDistanceFromOrigin() > current.getDistanceFromOrigin())
                        current = offset;
                };

                pull (i, 0.0f);

                for (int direction : { -1, 1 })
                {
                    float distance = 0.0f;
                    for (int k = 1; k <= falloff; ++k)
                    {
                        const auto j = ((i + direction * k) % n + n) % n;
                        const auto previous = ((i + direction * (k - 1)) % n + n) % n;
                        distance += c[(size_t) j].getDistanceFrom (c[(size_t) previous]);

                        if (distance >= radius)
                            break;

                        pull (j, distance);
                    }
                }
            }

            for (size_t i = 0; i < c.size(); ++i)
                c[i] += offsets[i];
        }
    }

    //==========================================================================
    // Step 4: トゲ。文字の上端・下端付近の輪郭から、上下にトゲを生やす
    void addSpikes (const std::vector<Glyph>& glyphs, const juce::Path& wholeText,
                    const MetalLogoParams& params, juce::Random& random, juce::Path& spikes)
    {
        const auto h = params.fontHeight;
        const auto step = juce::jmax (0.5f, h * params.spikeStep);
        std::vector<Point> placed;   // 生やしたトゲの根元

        for (const auto& glyph : glyphs)
        {
            const auto shape = toPath (glyph.contours);
            const auto b = shape.getBounds();
            const auto topBand = b.getY() + b.getHeight() * params.spikeBand;
            const auto bottomBand = b.getBottom() - b.getHeight() * params.spikeBand;

            // 塗りの回転方向 = 一番大きい輪郭 (外側の輪郭) の回転方向。
            // 非ゼロ規則で塗るので、トゲの三角形もこれと同じ向きにしないと、本体と重なる部分が穴になる
            float fillDirection = 0.0f;
            for (const auto& c : glyph.contours)
                if (const auto area = signedArea (c); std::abs (area) > std::abs (fillDirection))
                    fillDirection = area;

            for (const auto& c : glyph.contours)
            {
                float carry = 0.0f;   // 前の辺から持ち越した、次の候補点までの距離

                for (size_t i = 0; i < c.size(); ++i)
                {
                    const auto from = c[i], to = c[(i + 1) % c.size()];
                    const auto length = from.getDistanceFrom (to);
                    const auto tangent = normalised (to - from);

                    auto s = carry;
                    for (; s < length; s += step)
                    {
                        const auto p = from + (to - from) * (s / length);

                        // 1. 上端帯か下端帯にある
                        const bool isTop = p.y < topBand, isBottom = p.y > bottomBand;
                        if (! (isTop || isBottom) || std::abs (tangent.x) < params.spikeMinFlatness)
                            continue;

                        // 2. 確率で当選
                        if (random.nextFloat() >= params.spikeProbability)
                            continue;

                        // 3. ほかのトゲから離れている
                        if (std::any_of (placed.begin(), placed.end(),
                                         [&] (Point q) { return q.getDistanceFrom (p) < h * params.spikeMinGap; }))
                            continue;

                        // 4. 向き
                        const auto dir = isTop ? Point (0.0f, -1.0f) : Point (0.0f, 1.0f);

                        // 5. 外向き: 少し進んだ点が文字の外
                        if (shape.contains (p + dir * 1.5f))
                            continue;

                        const auto spikeLength = h * (params.spikeLenMin + random.nextFloat() * (params.spikeLenMax - params.spikeLenMin));
                        const auto tip = p + dir * spikeLength;

                        // 7. 先端が外接矩形の外に出る (「口」の内側の穴に入るのを防ぐ)
                        if (isTop ? tip.y >= b.getY() : tip.y <= b.getBottom())
                            continue;

                        // 6. 衝突判定: 根元から先端までのどの点も、テキスト全体に含まれない
                        bool collides = false;
                        for (int k = 1; k <= params.collisionSamples && ! collides; ++k)
                            collides = wholeText.contains (p + dir * (spikeLength * (float) k / (float) params.collisionSamples));
                        if (collides)
                            continue;

                        // 三角形。根元は少し文字の内側に沈め、本体との間にすき間ができないようにする
                        const auto halfWidth = h * params.spikeBaseWidth * 0.5f;
                        auto base1 = p - tangent * halfWidth - dir * halfWidth;
                        auto base2 = p + tangent * halfWidth - dir * halfWidth;

                        if ((signedArea ({ base1, base2, tip }) > 0.0f) != (fillDirection > 0.0f))
                            std::swap (base1, base2);

                        spikes.addTriangle (base1, base2, tip);
                        placed.push_back (p);
                    }

                    carry = s - length;
                }
            }
        }
    }
}

//==============================================================================
juce::Path MetalLogoGenerator::generate (const juce::String& text,
                                         const juce::Font& baseFont,
                                         const MetalLogoParams& params,
                                         juce::uint32 seed)
{
    if (text.trim().isEmpty())
        return {};

    juce::Random random ((juce::int64) seed);

    // Step 1・2
    auto glyphs = layOut (text, baseFont, params);
    for (auto& glyph : glyphs)
        glyph.contours = toContours (glyph.original, params);

    // Step 3
    for (auto& glyph : glyphs)
        extendTips (glyph, params, random);

    // Step 4 (衝突判定は、延長後のテキスト全体に対して行う)
    juce::Path wholeText;
    for (const auto& glyph : glyphs)
        wholeText.addPath (toPath (glyph.contours));
    wholeText.setUsingNonZeroWinding (true);

    juce::Path spikes;
    addSpikes (glyphs, wholeText, params, random, spikes);

    // Step 5: 合成して、余白の分だけずらす
    auto logo = wholeText;
    logo.addPath (spikes);
    logo.setUsingNonZeroWinding (true);

    if (logo.isEmpty())
        return {};

    const auto bounds = logo.getBounds();
    const auto margin = params.fontHeight * params.margin;
    logo.applyTransform (juce::AffineTransform::translation (margin - bounds.getX(), margin - bounds.getY()));
    return logo;
}

juce::Rectangle<float> MetalLogoGenerator::getLogoArea (const juce::Path& logo, const MetalLogoParams& params)
{
    if (logo.isEmpty())
        return {};

    const auto margin = params.fontHeight * params.margin;
    return logo.getBounds().expanded (margin);
}
