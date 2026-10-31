#pragma once

#include <juce_core/juce_core.h>

// ロゴ生成のパラメータ (death-metal-mode.md 4.3)
// 調整したい値はすべてここに置く。長さの多くは fontHeight に対する比率
struct MetalLogoParams
{
    // レイアウト (密度対策: 大きめに取る)
    float fontHeight      = 96.0f;   // ロゴ生成時の基準サイズ (px)。表示時に縮小する
    float tracking        = 0.35f;   // 文字間隔 (fontHeight に対する比率)
    float margin          = 0.05f;   // 上下左右の余白 (fontHeight 比)。外接矩形はトゲ込みで測るので、少しで足りる

    // 輪郭の分割
    float flattenTolerance = 0.5f;   // PathFlatteningIterator の許容誤差
    float simplifyAngleDeg = 2.0f;   // 曲がる角度がこれより小さい頂点は「ほぼ直線上」として間引く

    // はらい・はねの延長
    float tipAngleDeg      = 110.0f; // この角度以上に鋭く曲がる凸の角を「線の端」とみなす
    float tipExtend        = 0.25f;  // 延長量 (fontHeight 比)
    int   tipFalloffVerts  = 3;      // 前後何頂点まで減衰させながら引っ張るか
    float tipFalloffRadius = 1.5f;   // 減衰させる範囲 (延長量の何倍の距離まで)。1 より大きくして、輪郭が折り返さないようにする
    float tipJitter        = 0.3f;   // 延長量のランダム幅 (0〜1)
    float tipMaxStretch    = 3.0f;   // 延長量の上限 (隣の頂点までの距離 (短いほう) の何倍まで)。輪郭の破綻を防ぐ

    // トゲ
    float spikeBand        = 0.15f;  // グリフ上下の何割を「外周帯」とみなすか
    float spikeLenMin      = 0.12f;  // トゲ長さ (fontHeight 比)
    float spikeLenMax      = 0.35f;
    float spikeBaseWidth   = 0.04f;  // トゲ根元の幅 (fontHeight 比)
    float spikeMinGap      = 0.08f;  // トゲ同士の最小間隔 (fontHeight 比)
    float spikeProbability = 0.5f;   // 候補点にトゲを生やす確率
    int   collisionSamples = 5;      // 衝突判定のサンプル数
    float spikeStep        = 0.02f;  // 輪郭上で候補点を取る間隔 (fontHeight 比)
    float spikeMinFlatness = 0.5f;   // 輪郭の接線の水平成分がこれ未満 (= 縦に近い辺) の点には生やさない

    // すべての値に、名前と一緒に fn (名前, 値への参照) を呼ぶ。
    // 保存・読み込み・ハッシュで同じ一覧を使い、値を足したときの書き忘れを防ぐ
    template <typename Params, typename Fn>
    static void forEachField (Params& p, Fn&& fn)
    {
        fn ("fontHeight", p.fontHeight);             fn ("tracking", p.tracking);
        fn ("margin", p.margin);                     fn ("flattenTolerance", p.flattenTolerance);
        fn ("simplifyAngleDeg", p.simplifyAngleDeg); fn ("tipAngleDeg", p.tipAngleDeg);
        fn ("tipExtend", p.tipExtend);               fn ("tipFalloffVerts", p.tipFalloffVerts);
        fn ("tipFalloffRadius", p.tipFalloffRadius); fn ("tipJitter", p.tipJitter);
        fn ("tipMaxStretch", p.tipMaxStretch);       fn ("spikeBand", p.spikeBand);
        fn ("spikeLenMin", p.spikeLenMin);           fn ("spikeLenMax", p.spikeLenMax);
        fn ("spikeBaseWidth", p.spikeBaseWidth);     fn ("spikeMinGap", p.spikeMinGap);
        fn ("spikeProbability", p.spikeProbability); fn ("collisionSamples", p.collisionSamples);
        fn ("spikeStep", p.spikeStep);               fn ("spikeMinFlatness", p.spikeMinFlatness);
    }

    // キャッシュのキーに使うハッシュ。値が1つでも変われば別の値になる
    juce::int64 hash() const
    {
        juce::String s;
        forEachField (*this, [&s] (const char*, auto value) { s << juce::String ((double) value, 5) << ","; });
        return s.hashCode64();
    }

    // 設定ファイルへの保存用 (JSON)。無い値は初期値のまま
    juce::var toVar() const
    {
        auto* object = new juce::DynamicObject();
        forEachField (*this, [object] (const char* name, auto value) { object->setProperty (name, value); });
        return juce::var (object);
    }

    static MetalLogoParams fromVar (const juce::var& v)
    {
        MetalLogoParams p;
        forEachField (p, [&v] (const char* name, auto& value)
        {
            if (v.hasProperty (name))
                value = (std::remove_reference_t<decltype (value)>) (double) v[name];
        });
        return p;
    }
};
