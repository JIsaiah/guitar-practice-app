#pragma once

#include <cmath>
#include <vector>

// キーボードの矢印で、画面の上の「次の場所」を選ぶ (一覧画面のキーボード操作。作者の指示)
// 今の場所 (from) から、押した向きにあるもののうち、いちばん近いものを選ぶ。
//   - 向きの先にあるか: 相手の真ん中が、今の場所の向こうの端より先にあるもの (下なら、今の下の端より下)。
//     真ん中どうしで比べると、横に長い行で「右」を押したときに、右上のタブへ飛んでしまうため
//   - 近さ: 向きの方向のすき間 + 横 (上下なら左右) のずれ × 3。横に重なっていれば、ずれは 0
//   - 同じくらいなら、真ん中の横のずれが小さいもの
// 画面の部品 (juce::Component) を使わないので、自動テストで確かめられる
namespace SpatialNavigation
{
    enum class Direction { up, down, left, right };

    struct Box
    {
        float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;

        float right() const noexcept   { return x + width; }
        float bottom() const noexcept  { return y + height; }
        float centreX() const noexcept { return x + width * 0.5f; }
        float centreY() const noexcept { return y + height * 0.5f; }
    };

    // 区間 [a1, a2] と [b1, b2] のすき間。重なっていれば 0
    inline float gapBetween (float a1, float a2, float b1, float b2) noexcept
    {
        return std::fmax (0.0f, std::fmax (b1 - a2, a1 - b2));
    }

    // boxes の中で、from から direction の向きにいちばん近いものの番号。無ければ -1
    inline int findNext (const std::vector<Box>& boxes, const Box& from, Direction direction)
    {
        const auto vertical = direction == Direction::up || direction == Direction::down;

        int best = -1;
        float bestScore = 0.0f;

        for (int i = 0; i < (int) boxes.size(); ++i)
        {
            const auto& box = boxes[(size_t) i];

            // 向きの先にあるか (相手の真ん中が、今の場所の向こうの端より先)
            const auto beyond = direction == Direction::down  ? box.centreY() - from.bottom()
                              : direction == Direction::up    ? from.y - box.centreY()
                              : direction == Direction::right ? box.centreX() - from.right()
                                                              : from.x - box.centreX();
            if (beyond <= 0.0f)
                continue;

            const auto along = vertical ? box.centreY() - from.centreY() : box.centreX() - from.centreX();

            // 向きの方向のすき間 (重なっていれば 0) と、横のずれ
            const auto primary = vertical ? gapBetween (from.y, from.bottom(), box.y, box.bottom())
                                          : gapBetween (from.x, from.right(), box.x, box.right());
            const auto secondary = vertical ? gapBetween (from.x, from.right(), box.x, box.right())
                                            : gapBetween (from.y, from.bottom(), box.y, box.bottom());
            const auto centreOffset = vertical ? std::fabs (box.centreX() - from.centreX())
                                               : std::fabs (box.centreY() - from.centreY());

            const auto score = primary + secondary * 3.0f + centreOffset * 0.05f + std::fabs (along) * 0.01f;
            if (best < 0 || score < bestScore)
            {
                best = i;
                bestScore = score;
            }
        }

        return best;
    }
}
