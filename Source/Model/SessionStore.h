#pragma once

#include <optional>
#include <vector>
#include "Session.h"
#include "SessionTrust.h"

// セッションファイル (.gppractice) の一覧・読み書き
// 保存先: ドキュメント\suguhiku\Sessions (AppPaths.h。前は ドキュメント\GuitarPractice)
//
// ファイル名 (= id) は「曲名 - アーティスト名 (作った日時).gppractice」(作者の指示)。
//   - 何の曲のファイルか、見て分かるように。ほかの PC へ手でコピーして使えるように
//   - 作った日時を入れて、同じ曲名のセッションでも名前がぶつからないようにする (それでもぶつかれば「 2」などを付ける)
//   - 曲名・アーティスト名を変えて保存すると、ファイル名も変える。セットリストはファイル名ではなく
//     session_id でセッションを指すので、名前を変えてもリンクは切れない (SessionLibrary)
class SessionStore
{
public:
    struct Entry
    {
        juce::String id;
        juce::String sessionId;   // 古いファイルでまだ付いていなければ空 (assignMissingSessionIds() で付ける)
        juce::String title;
        juce::Time modified;
        double lengthSeconds = 0.0;   // 0 = 不明
        juce::String artist, tuning;
        juce::String format;   // 一覧の「形式」の列 (.gp、.pdf、URL、ローカルファイル。SessionFormat.h)
        juce::String folder;   // 空ならフォルダなし
        double bpm = 0.0;
        int difficulty = 0;
        juce::Time lastOpened;   // 最後に開いた日時。開いたことが無ければ 0
    };

    SessionStore();
    // テスト用: 保存先を指定する。trustRecord を省くと、保存先の隣の「(フォルダ名)-known.json」に記録する
    explicit SessionStore (const juce::File& directory, const juce::File& trustRecord = {});

    // セキュリティ (SessionTrust.h): この PC で保存したセッションか。人から受け取ったファイルや、アプリの外で書き換えたファイルは false。
    // 作者が「保存」したとき (save) だけ「この PC のもの」になる。一覧での星・詳細の変更、ID の付け足し、名前の付け直しなど、
    // アプリが自動で書き直すときは、書き直す前の状態を引き継ぐ (外から来たファイルを、書き直しただけで信じないように)
    bool isFromThisPc (const juce::String& id) const;
    void trustSession (const juce::String& id);   // 確認で「信頼して開く」を選んだ

    // 練習画面で開いたことを記録する (last_opened を今にする)。この PC のものかは変えない。
    // ファイル名を曲名の名前に直すことがあるので、新しいファイル名 (id) を返す (書けなければ元の id)
    juce::String markOpened (const juce::String& id);

    std::vector<Entry> list() const;  // 新しい順。ファイルは書き換えない
    std::optional<Session> load (const juce::String& id);   // session_id が無ければ付けて保存し直す
    // id・sessionId が空なら決める。曲名・アーティスト名に合わせてファイル名 (id) を変えることがある
    // (session.id が新しい名前になる。古い名前のファイルは消す)
    bool save (Session& session);
    bool remove (const juce::String& id);

    // 読み込み → change で書き換え → 保存。一覧の並び (更新日時の新しい順) は変えない。
    // 一覧で曲の詳細や難易度を変えたときに使う (星をクリックした行が、一番上に飛ばないように)
    std::optional<Session> update (const juce::String& id, const std::function<void (Session&)>& change);

    // 練習画面で開くために読み込み、最後に開いた日時 (last_opened) を今にして保存する (一覧の並びは変えない)。
    // 書き込めなくても、読み込めれば開く
    std::optional<Session> openForPractice (const juce::String& id);

    // session_id の無い古いファイルに ID を付けて保存し直す (setlist-mode.md 3.1 のマイグレーション)
    void assignMissingSessionIds();

    // 前の名前 (英数字 32 文字の ID) のファイルを、「曲名 - アーティスト名 (作った日時)」の名前に直す (ほかの名前は変えない)。
    // 作った日時の無いファイルは、ファイルの作られた日時を使って書き足す。更新日時 (一覧の並び) は変えない
    void renameToReadableNames();

    // セッションに合うファイル名 (拡張子なし)。currentId がすでに合う名前ならそれを返す。
    // ほかのファイルとぶつかるときは「 2」「 3」… を付ける
    juce::String readableIdFor (const Session& session, const juce::String& currentId) const;
    static juce::String baseNameFor (const juce::String& title, const juce::String& artist, juce::Time created);

    const juce::File& getDirectory() const noexcept { return directory; }

    static constexpr const char* fileExtension = ".gppractice";

private:
    juce::File fileFor (const juce::String& id) const;
    juce::String ensureSessionId (const juce::File& file, juce::var& parsed);
    bool saveImpl (Session& session, bool markTrusted);
    // アプリが自動で書き直す: 書く前に「この PC のもの」だったときだけ、書いたあとも「この PC のもの」にする
    bool rewriteKeepingTrust (const juce::File& from, const juce::File& to, const juce::String& text);

    juce::File directory;
    SessionTrust trust;
};
