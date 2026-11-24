#pragma once

#include <map>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include "SafeJson.h"

// 「この PC で書いたセッションか」を覚える (セキュリティ。SessionRisk.h)
//
// アプリがセッションのファイルを保存するたびに、中身のハッシュ (SHA-256) をアプリの設定の場所
// (%APPDATA%\GuitarPractice\known-sessions.json) に記録する。開くときに、ファイルの中身のハッシュが記録と同じなら
// 「この PC のセッション」、違えば (人から受け取った・アプリの外で書き換えた)「外から来たセッション」。
// 記録はセッションのフォルダとは別の場所にあるので、渡されたセッションのファイルでは書き換えられない
class SessionTrust
{
public:
    explicit SessionTrust (juce::File recordFile) : file (std::move (recordFile)) { load(); }

    // 記録のファイルがまだ無い (この仕組みが入る前の版から上げたところ)
    bool isFirstRun() const { return ! file.existsAsFile(); }

    bool isKnown (const juce::File& sessionFile) const
    {
        const auto it = hashes.find (keyOf (sessionFile));
        return it != hashes.end() && sessionFile.existsAsFile() && it->second == hashOf (sessionFile);
    }

    // 今のファイルの中身を「この PC のもの」として記録する
    void remember (const juce::File& sessionFile)
    {
        if (sessionFile.existsAsFile())
            hashes[keyOf (sessionFile)] = hashOf (sessionFile);
        save();
    }

    void rememberAll (const juce::Array<juce::File>& sessionFiles)
    {
        for (const auto& f : sessionFiles)
            if (f.existsAsFile())
                hashes[keyOf (f)] = hashOf (f);
        save();
    }

    void forget (const juce::File& sessionFile)
    {
        if (hashes.erase (keyOf (sessionFile)) > 0)
            save();
    }

    static juce::String hashOf (const juce::File& f)
    {
        juce::MemoryBlock data;
        if (! f.loadFileAsData (data))
            return {};
        return juce::SHA256 (data.getData(), data.getSize()).toHexString();
    }

private:
    // ファイル名で記録する (Windows は大文字・小文字を区別しないので、小文字にそろえる)
    static juce::String keyOf (const juce::File& f) { return f.getFileName().toLowerCase(); }

    void load()
    {
        const auto parsed = SafeJson::parseFile (file);   // 大きすぎる・入れ子が深すぎるものは読まない。変数に持つ (一時の値のままだと、中身が先に消える)
        if (auto* object = parsed.getDynamicObject())
            for (const auto& p : object->getProperties())
                hashes[p.name.toString()] = p.value.toString();
    }

    void save() const
    {
        auto* object = new juce::DynamicObject();
        for (const auto& [name, hash] : hashes)
            object->setProperty (name, hash);
        file.getParentDirectory().createDirectory();
        file.replaceWithText (juce::JSON::toString (juce::var (object)));
    }

    juce::File file;
    std::map<juce::String, juce::String> hashes;
};
