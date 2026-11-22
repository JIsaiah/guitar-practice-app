#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// 画面の描き方 (GPU の Direct2D か、CPU のソフトウェアか)。
// 画面によっては (別の GPU につながったモニターなど) GPU のほうが遅く、アニメーションがガタつくことがあるので、
// 設定で選べるようにした (AppSettings::getGpuRendering)
namespace RenderingMode
{
    // 今あるウィンドウすべての描き方を変える (起動したとき・設定を変えたとき)。
    // JUCE 8 (Windows) の描き方の名前は「Software Renderer」と「Direct2D」なので、名前で選ぶ
    inline void apply (bool useGpu)
    {
        for (int i = 0; i < juce::ComponentPeer::getNumPeers(); ++i)
        {
            auto* peer = juce::ComponentPeer::getPeer (i);
            const auto engines = peer->getAvailableRenderingEngines();
            for (int e = 0; e < engines.size(); ++e)
                if (engines[e].containsIgnoreCase ("Direct2D") == useGpu)
                {
                    if (peer->getCurrentRenderingEngine() != e)
                        peer->setCurrentRenderingEngine (e);
                    break;
                }
        }
    }
}
