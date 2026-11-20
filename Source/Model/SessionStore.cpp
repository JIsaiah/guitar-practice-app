#include "SessionStore.h"
#include "../Text.h"
#include "../AppPaths.h"
#include "../Library/SessionFormat.h"

SessionStore::SessionStore()
    : SessionStore (AppPaths::documentsFolder().getChildFile ("Sessions"),
                    // 「この PC で保存したセッション」の記録は、アプリの設定の場所に置く (セッションのフォルダとは別。SessionTrust.h)
                    juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("GuitarPractice").getChildFile ("known-sessions.json"))
{
}

SessionStore::SessionStore (const juce::File& dir, const juce::File& trustRecord)
    : directory (dir),
      trust (trustRecord != juce::File() ? trustRecord : dir.getSiblingFile (dir.getFileName() + "-known.json"))
{
    directory.createDirectory();

    // この仕組みが入る前の版から上げたとき: 今あるセッションは、すべて作者のものとみなして記録する
    if (trust.isFirstRun())
        trust.rememberAll (directory.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension));
}

bool SessionStore::isFromThisPc (const juce::String& id) const
{
    return trust.isKnown (fileFor (id));
}

void SessionStore::trustSession (const juce::String& id)
{
    trust.remember (fileFor (id));
}

juce::String SessionStore::markOpened (const juce::String& id)
{
    if (auto updated = update (id, [] (Session& s) { s.lastOpened = juce::Time::getCurrentTime(); }))
        return updated->id;
    return id;
}

bool SessionStore::rewriteKeepingTrust (const juce::File& from, const juce::File& to, const juce::String& text)
{
    const auto wasTrusted = trust.isKnown (from);
    if (! to.replaceWithText (text))
        return false;

    if (from != to)
        trust.forget (from);
    if (wasTrusted)
        trust.remember (to);
    else
        trust.forget (to);
    return true;
}

std::vector<SessionStore::Entry> SessionStore::list() const
{
    std::vector<Entry> entries;

    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
    {
        const auto parsed = juce::JSON::parse (file.loadFileAsString());
        if (! parsed.isObject())
            continue;

        Entry entry;
        entry.id = file.getFileNameWithoutExtension();
        entry.sessionId = parsed["session_id"].toString();
        entry.title = parsed["title"].toString();
        entry.modified = file.getLastModificationTime();
        entry.lengthSeconds = (double) parsed["source"]["length_sec"];
        entry.artist = parsed["artist"].toString();
        entry.tuning = parsed["tuning"].toString();
        entry.format = SessionFormat::describe (parsed["score"]["path"].toString(), parsed["source"]["type"].toString());
        entry.folder = parsed["folder"].toString();
        entry.bpm = (double) parsed["tempo"]["bpm"];
        entry.difficulty = juce::jlimit (0, Session::maxDifficulty, (int) parsed["difficulty"]);
        if (const auto opened = parsed["last_opened"].toString(); opened.isNotEmpty())
            entry.lastOpened = juce::Time::fromISO8601 (opened);
        entries.push_back (entry);
    }

    std::sort (entries.begin(), entries.end(),
               [] (const Entry& a, const Entry& b) { return a.modified > b.modified; });
    return entries;
}

std::optional<Session> SessionStore::load (const juce::String& id)
{
    const auto file = fileFor (id);
    if (! file.existsAsFile())
        return std::nullopt;

    auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject())
        return std::nullopt;

    ensureSessionId (file, parsed);

    auto session = Session::fromVar (parsed);
    session.id = id;
    return session;
}

bool SessionStore::save (Session& session)
{
    return saveImpl (session, true);   // 作者が保存した = この PC のもの
}

bool SessionStore::saveImpl (Session& session, bool markTrusted)
{
    if (session.sessionId.isEmpty())
        session.sessionId = juce::Uuid().toString();

    if (session.created.toMilliseconds() <= 0)
        session.created = juce::Time::getCurrentTime();

    if (! directory.createDirectory())
        return false;

    // ファイル名を曲名・アーティスト名に合わせる。新しい名前で書けてから、古い名前のファイルを消す
    const auto oldId = session.id;
    const auto newId = readableIdFor (session, oldId);
    if (! fileFor (newId).replaceWithText (juce::JSON::toString (session.toVar())))
        return false;

    if (oldId.isNotEmpty() && oldId != newId)
    {
        fileFor (oldId).deleteFile();
        trust.forget (fileFor (oldId));
    }

    if (markTrusted)
        trust.remember (fileFor (newId));
    else
        trust.forget (fileFor (newId));

    session.id = newId;
    return true;
}

juce::String SessionStore::baseNameFor (const juce::String& title, const juce::String& artist, juce::Time created)
{
    // Windows のファイル名に使えない文字 (\ / : * ? " < > |) と改行などは「_」にする。長すぎる名前は切る
    const auto clean = [] (const juce::String& text, int maxLength)
    {
        juce::String result;
        for (auto c : text.trim())
            result += juce::String::charToString (juce::String ("\\/:*?\"<>|").containsChar (c) || c < 32 ? juce::juce_wchar ('_') : c);
        return result.substring (0, maxLength).trim().trimCharactersAtEnd (".");
    };

    auto name = clean (title, 60);
    if (name.isEmpty())
        name = "無題"_jp;

    if (const auto by = clean (artist, 40); by.isNotEmpty())
        name << " - " << by;

    // 作った日時。ファイル名に「:」は使えないので、時と分は「-」でつなぐ (例: 2026-11-11 14-32)
    if (created.toMilliseconds() > 0)
        name << " (" << created.formatted ("%Y-%m-%d %H-%M") << ")";

    return name;
}

juce::String SessionStore::readableIdFor (const Session& session, const juce::String& currentId) const
{
    const auto base = baseNameFor (session.title, session.artist, session.created);

    // 今の名前が、すでにこの名前 (か、ぶつかったときに番号を付けた名前) なら変えない
    if (currentId == base)
        return currentId;
    if (currentId.startsWith (base + " ") && currentId.fromLastOccurrenceOf (" ", false, false).containsOnly ("0123456789"))
        return currentId;

    for (int n = 1;; ++n)
    {
        const auto candidate = n == 1 ? base : base + " " + juce::String (n);
        if (! fileFor (candidate).exists())
            return candidate;
    }
}

void SessionStore::renameToReadableNames()
{
    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
    {
        auto parsed = juce::JSON::parse (file.loadFileAsString());
        if (! parsed.isObject())
            continue;

        // 前の名前 (英数字 32 文字の ID) のファイルだけを直す。
        // 作者が自分で付けた名前 (アプリの外で変えた名前) は変えない (保存したときに曲名の名前になる)
        const auto id = file.getFileNameWithoutExtension();
        if (id.length() != 32 || ! id.containsOnly ("0123456789abcdefABCDEF"))
            continue;

        auto session = Session::fromVar (parsed);
        const auto modified = file.getLastModificationTime();

        // 作った日時の無い古いファイル: ファイルの作られた日時 (更新日時のほうが古ければそれ) を使う
        const bool addCreated = session.created.toMilliseconds() <= 0;
        if (addCreated)
        {
            const auto made = file.getCreationTime();
            session.created = made.toMilliseconds() > 0 && made < modified ? made : modified;
        }

        const auto newId = readableIdFor (session, id);
        if (newId == id && ! addCreated)
            continue;

        // 中身は読んだまま (知らない項目も消さない) で、作った日時だけを書き足す
        if (auto* object = parsed.getDynamicObject(); object != nullptr && addCreated)
            object->setProperty ("created", session.created.toISO8601 (true));

        const auto target = fileFor (newId);
        if (rewriteKeepingTrust (file, target, juce::JSON::toString (parsed)))
        {
            target.setLastModificationTime (modified);   // 一覧の並び (更新日時の新しい順) を変えない
            if (target != file)
                file.deleteFile();
        }
    }
}

bool SessionStore::remove (const juce::String& id)
{
    trust.forget (fileFor (id));
    return fileFor (id).deleteFile();
}

std::optional<Session> SessionStore::update (const juce::String& id, const std::function<void (Session&)>& change)
{
    auto session = load (id);
    if (! session.has_value())
        return std::nullopt;

    const auto modified = fileFor (id).getLastModificationTime();
    const auto wasTrusted = trust.isKnown (fileFor (id));   // 一覧での変更は、この PC のものかを変えない

    change (*session);
    if (! saveImpl (*session, wasTrusted))   // 曲名を変えたときは、ファイル名 (session->id) も変わる
        return std::nullopt;

    fileFor (session->id).setLastModificationTime (modified);   // 並び順を変えない
    return session;
}

std::optional<Session> SessionStore::openForPractice (const juce::String& id)
{
    if (auto opened = update (id, [] (Session& s) { s.lastOpened = juce::Time::getCurrentTime(); }))
        return opened;

    return load (id);
}

void SessionStore::assignMissingSessionIds()
{
    // ID ごとのファイル。同じ ID のファイルが2つ以上あると、セットリストがどちらを開くか決まらず、
    // 人から受け取ったセッションが、作者のセッションの代わりに開かれることがある (セキュリティ)
    std::map<juce::String, juce::Array<juce::File>> byId;

    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
    {
        auto parsed = juce::JSON::parse (file.loadFileAsString());
        if (parsed.isObject())
            byId[ensureSessionId (file, parsed).toLowerCase()].add (file);
    }

    // 同じ ID が重なっていたら、この PC のもの (無ければ最初の1つ) を残し、ほかには新しい ID を付ける
    for (auto& [id, files] : byId)
    {
        if (files.size() < 2)
            continue;

        int keep = 0;
        for (int i = 0; i < files.size(); ++i)
            if (trust.isKnown (files[i]))
            {
                keep = i;
                break;
            }

        for (int i = 0; i < files.size(); ++i)
        {
            if (i == keep)
                continue;

            auto parsed = juce::JSON::parse (files[i].loadFileAsString());
            if (auto* object = parsed.getDynamicObject())
            {
                object->setProperty ("session_id", juce::Uuid().toString());
                const auto modified = files[i].getLastModificationTime();
                if (rewriteKeepingTrust (files[i], files[i], juce::JSON::toString (parsed)))
                    files[i].setLastModificationTime (modified);
            }
        }
    }
}

juce::String SessionStore::ensureSessionId (const juce::File& file, juce::var& parsed)
{
    auto sessionId = parsed["session_id"].toString();
    if (sessionId.isNotEmpty())
        return sessionId;

    sessionId = juce::Uuid().toString();

    if (auto* object = parsed.getDynamicObject())
    {
        object->setProperty ("session_id", sessionId);
        if (! object->hasProperty ("version"))
            object->setProperty ("version", 1);   // バージョンの無い古いファイル。中身の形はそのままなので 1 とする

        // 一覧は更新日時の新しい順に並ぶ。ID を付けただけで順番が変わらないよう、更新日時を戻す
        const auto modified = file.getLastModificationTime();
        if (rewriteKeepingTrust (file, file, juce::JSON::toString (parsed)))
            file.setLastModificationTime (modified);
    }

    return sessionId;
}

juce::File SessionStore::fileFor (const juce::String& id) const
{
    return directory.getChildFile (id + fileExtension);
}
