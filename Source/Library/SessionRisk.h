#pragma once

#include <juce_core/juce_core.h>
#include "../Model/Session.h"
#include "../Text.h"

// 人から受け取ったセッション (この PC で保存していないもの。SessionTrust.h) に書かれた、危ないもの (セキュリティ)
//
//   プラグイン: .vst3 の中身は DLL。読み込んだ時点で、その中のプログラムが動く (パソコンの乗っ取り)。
//              標準の置き場所 (管理者しか入れられない Program Files など) の外のものを危ないとする
//   ネットワークの場所: \\pc\共有\... のファイルは、「あるか」を見るだけで Windows がその PC につなぎ、
//              ログインの情報 (ユーザー名・パスワードから作った値) を送ってしまう
//   プラグインの状態: そのままプラグインに渡す。プラグインの読み込み処理の穴を突ける。中にファイルの場所を書けるプラグインもある
//
// 画面を使わないので、自動テストで確かめられる
namespace SessionRisk
{
    // ネットワークの場所か (\\pc\共有\...、\\?\UNC\...、\\pc@SSL@443\... (WebDAV)、//pc/...)
    inline bool isNetworkPath (const juce::String& path)
    {
        const auto p = path.trim();
        return p.startsWith ("\\\\") || p.startsWith ("//");
    }

    // 標準の置き場所の中か。".." の段がある場所は、中に見せかけて外を指せるので、中とみなさない
    inline bool isInFolders (const juce::String& path, const juce::Array<juce::File>& folders)
    {
        if (path.trim().isEmpty() || isNetworkPath (path))
            return false;

        for (const auto& part : juce::StringArray::fromTokens (path.replaceCharacter ('/', '\\'), "\\", {}))
            if (part == ".." || part == ".")
                return false;

        const auto file = juce::File::createFileWithoutCheckingPath (path);
        for (const auto& folder : folders)
            if (file.isAChildOf (folder))
                return true;
        return false;
    }

    struct Item
    {
        enum class Kind { plugin, networkFile, pluginState };
        Kind kind;
        juce::String detail;   // 場所 (pluginState は空)
    };

    // セッションに書かれた危ないもの (同じものは1回だけ)
    inline std::vector<Item> collect (const Session& s, const juce::Array<juce::File>& pluginFolders)
    {
        std::vector<Item> items;
        const auto add = [&items] (Item::Kind kind, const juce::String& detail)
        {
            for (const auto& i : items)
                if (i.kind == kind && i.detail.equalsIgnoreCase (detail))
                    return;
            items.push_back ({ kind, detail });
        };

        // プラグイン (今の音色と、プリセット 1〜3)
        juce::StringArray plugins { s.pluginPath };
        for (const auto& slot : s.toneSlots)
            plugins.add (slot.pluginPath);
        for (const auto& p : plugins)
            if (p.trim().isNotEmpty() && ! isInFolders (p, pluginFolders))
                add (Item::Kind::plugin, p);

        // ファイル (ネットワークの場所だけ)
        juce::StringArray files { s.sourceType == SourceType::local ? s.sourcePath : juce::String(),
                                  s.scorePath, s.pdfBackingPath, s.namModelPath, s.irPath };
        for (const auto& slot : s.toneSlots)
            files.addArray (juce::StringArray { slot.namModelPath, slot.irPath });
        for (const auto& f : files)
            if (isNetworkPath (f))
                add (Item::Kind::networkFile, f);

        // プラグインの状態
        bool hasState = s.pluginState.isNotEmpty();
        for (const auto& slot : s.toneSlots)
            hasState = hasState || slot.pluginState.isNotEmpty();
        if (hasState)
            add (Item::Kind::pluginState, {});

        return items;
    }

    // 危ないものを外す (「安全に開く」)。外したものは、作者が選び直せる
    inline void makeSafe (Session& s, const juce::Array<juce::File>& pluginFolders)
    {
        const auto cleanPlugin = [&pluginFolders] (juce::String& path, juce::String& state)
        {
            state = {};   // 状態は、標準の置き場所のプラグインでも外す (初めの状態で開く)
            if (path.trim().isNotEmpty() && ! isInFolders (path, pluginFolders))
                path = {};
        };
        const auto cleanFile = [] (juce::String& path)
        {
            if (isNetworkPath (path))
                path = {};
        };

        cleanPlugin (s.pluginPath, s.pluginState);
        for (auto& slot : s.toneSlots)
        {
            cleanPlugin (slot.pluginPath, slot.pluginState);
            cleanFile (slot.namModelPath);
            cleanFile (slot.irPath);
        }

        if (s.sourceType == SourceType::local && isNetworkPath (s.sourcePath))
        {
            s.sourcePath = {};
            s.sourceType = SourceType::none;
        }

        if (isNetworkPath (s.scorePath))
        {
            s.scorePath = {};
            s.pdfBackingPath = {};
        }
        cleanFile (s.pdfBackingPath);
        cleanFile (s.namModelPath);
        cleanFile (s.irPath);
    }

    // 確認のダイアログに出す文
    inline juce::String describe (const std::vector<Item>& items)
    {
        juce::StringArray lines;
        for (const auto& i : items)
        {
            if (i.kind == Item::Kind::plugin)
                lines.add ("・プラグイン (読み込むと、中のプログラムが動きます): "_jp + i.detail);
            else if (i.kind == Item::Kind::networkFile)
                lines.add ("・ネットワークの場所のファイル (開くと、このパソコンのログインの情報が送られます): "_jp + i.detail);
            else
                lines.add ("・プラグインの設定 (そのままプラグインに渡されます)"_jp);
        }
        return lines.joinIntoString ("\n");
    }
}
