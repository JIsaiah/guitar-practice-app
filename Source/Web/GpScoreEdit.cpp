#include "GpScoreEdit.h"
#include "../Text.h"
#include "ScoreFileData.h"

namespace GpScoreEdit
{
namespace
{
    const juce::String noId = "-1";   // 空の Voice・Beat (GP のファイルの書き方)

    juce::StringArray tokensOf (const juce::XmlElement* list)
    {
        if (list == nullptr)
            return {};

        auto tokens = juce::StringArray::fromTokens (list->getAllSubText(), " \t\r\n", "");
        tokens.removeEmptyStrings();
        return tokens;
    }

    void setTokens (juce::XmlElement& list, const juce::StringArray& tokens)
    {
        list.deleteAllTextElements();
        list.addTextElement (tokens.joinIntoString (" "));
    }

    juce::XmlElement* findById (juce::XmlElement* collection, const juce::String& id)
    {
        if (collection != nullptr)
            for (auto* item : collection->getChildIterator())
                if (item->getStringAttribute ("id") == id)
                    return item;

        return nullptr;
    }

    // n 番目の、"-1" でない id の場所 (tokens の中の番号)。無ければ -1
    int indexOfNth (const juce::StringArray& tokens, int n)
    {
        for (int i = 0; i < tokens.size(); ++i)
            if (tokens[i] != noId && n-- == 0)
                return i;

        return -1;
    }

    // collection の中のもの (例: Beat) を、owners の中のもの (例: Voice) の listName (例: <Beats>) が、いくつ使っているか
    int countReferences (juce::XmlElement* owners, const juce::String& listName, const juce::String& id)
    {
        int count = 0;
        if (owners != nullptr)
            for (auto* owner : owners->getChildIterator())
                for (const auto& token : tokensOf (owner->getChildByName (listName)))
                    if (token == id)
                        ++count;
        return count;
    }

    int nextFreeId (juce::XmlElement* collection)
    {
        int highest = -1;
        if (collection != nullptr)
            for (auto* item : collection->getChildIterator())
                highest = juce::jmax (highest, item->getStringAttribute ("id").getIntValue());
        return highest + 1;
    }

    // list の index 番目の id のもの (collection の中) を、ほかの場所と共有していないようにして返す。
    // 共有していれば写しを作り、list の id を写しの id に変える
    juce::XmlElement* makeUnique (juce::XmlElement& list, int index, juce::XmlElement* collection,
                                  juce::XmlElement* owners, const juce::String& listName)
    {
        auto tokens = tokensOf (&list);
        const auto id = tokens[index];
        auto* item = findById (collection, id);
        if (item == nullptr || countReferences (owners, listName, id) <= 1)
            return item;

        auto* copy = new juce::XmlElement (*item);
        const auto newId = juce::String (nextFreeId (collection));
        copy->setAttribute ("id", newId);
        collection->addChildElement (copy);

        tokens.set (index, newId);
        setTokens (list, tokens);
        return copy;
    }

    juce::XmlElement* findProperty (juce::XmlElement& note, const juce::String& name)
    {
        if (auto* properties = note.getChildByName ("Properties"))
            for (auto* property : properties->getChildWithTagNameIterator ("Property"))
                if (property->getStringAttribute ("name") == name)
                    return property;

        return nullptr;
    }

    // <Property name="name"><child>値</child></Property> の値。無ければ fallback
    int readProperty (juce::XmlElement& note, const juce::String& name, const juce::String& child, int fallback)
    {
        if (auto* property = findProperty (note, name))
            if (auto* value = property->getChildByName (child))
                return value->getAllSubText().trim().getIntValue();

        return fallback;
    }

    void writeValue (juce::XmlElement& element, const juce::String& text)
    {
        element.deleteAllTextElements();
        element.addTextElement (text);
    }

    // 音の高さ (<Pitch><Step>E</Step><Accidental>#</Accidental><Octave>4</Octave></Pitch>) を semitones だけ動かす
    void shiftPitch (juce::XmlElement& pitch, int semitones)
    {
        auto* step = pitch.getChildByName ("Step");
        auto* octave = pitch.getChildByName ("Octave");
        if (step == nullptr || octave == nullptr)
            return;

        static const juce::String letters = "CDEFGAB";
        static const int letterSemitones[] = { 0, 2, 4, 5, 7, 9, 11 };
        const auto letterIndex = letters.indexOfChar (step->getAllSubText().trim().toUpperCase()[0]);
        if (letterIndex < 0)
            return;

        int accidental = 0;
        if (auto* acc = pitch.getChildByName ("Accidental"))
            for (auto c : acc->getAllSubText().trim())
                accidental += c == '#' ? 1 : (c == 'b' ? -1 : 0);

        const auto value = octave->getAllSubText().trim().getIntValue() * 12 + letterSemitones[letterIndex] + accidental + semitones;
        static const char* const names[] = { "C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B" };
        static const bool sharp[] = { false, true, false, true, false, false, true, false, true, false, true, false };
        const auto pitchClass = ((value % 12) + 12) % 12;

        writeValue (*step, names[pitchClass]);
        writeValue (*octave, juce::String ((value - pitchClass) / 12));

        auto* acc = pitch.getChildByName ("Accidental");
        if (acc == nullptr)
        {
            int stepIndex = 0;
            for (auto* child : pitch.getChildIterator())
            {
                if (child == step)
                    break;
                ++stepIndex;
            }
            acc = new juce::XmlElement ("Accidental");
            pitch.insertChildElement (acc, stepIndex + 1);
        }
        writeValue (*acc, sharp[pitchClass] ? "#" : "");
    }

    // 音の高さの書かれ方は GP の版で違うので、あるものをすべて同じだけ動かす
    void shiftNotePitch (juce::XmlElement& note, int semitones)
    {
        if (semitones == 0)
            return;

        if (auto* midi = findProperty (note, "Midi"))
            if (auto* number = midi->getChildByName ("Number"))
                writeValue (*number, juce::String (number->getAllSubText().trim().getIntValue() + semitones));

        for (const auto* name : { "ConcertPitch", "TransposedPitch" })
            if (auto* property = findProperty (note, name))
                if (auto* pitch = property->getChildByName ("Pitch"))
                    shiftPitch (*pitch, semitones);

        // 古い書き方: 音名 (Tone の Step、0〜11) とオクターブ (Octave の Number)
        auto* tone = findProperty (note, "Tone");
        auto* octave = findProperty (note, "Octave");
        auto* toneStep = tone != nullptr ? tone->getChildByName ("Step") : nullptr;
        auto* octaveNumber = octave != nullptr ? octave->getChildByName ("Number") : nullptr;
        if (toneStep != nullptr && octaveNumber != nullptr)
        {
            const auto value = octaveNumber->getAllSubText().trim().getIntValue() * 12 + toneStep->getAllSubText().trim().getIntValue() + semitones;
            const auto pitchClass = ((value % 12) + 12) % 12;
            writeValue (*toneStep, juce::String (pitchClass));
            writeValue (*octaveNumber, juce::String ((value - pitchClass) / 12));
        }
    }
}

void addEdit (std::vector<NoteEdit>& edits, const NoteEdit& edit)
{
    const auto tieUnchanged = [] (const NoteEdit& e)
    {
        return e.tieOrigin == e.oldTieOrigin && e.tieDestination == e.oldTieDestination;
    };

    // 何も変えない編集は覚えない
    if ((edit.kind == Kind::setFret && edit.fret == edit.oldFret) || (edit.kind == Kind::tie && tieUnchanged (edit)))
        return;

    // 拍・小節の挿入は、まとめない (前後の編集とも)
    if (edits.empty() || isInsert (edit.kind) || isInsert (edits.back().kind) || ! edits.back().isSameNote (edit))
    {
        edits.push_back (edit);
        return;
    }

    auto& last = edits.back();
    const auto dropLast = [&edits] { edits.pop_back(); };

    // タイ: すぐ前も同じ音のタイなら、変えた後の値だけを置き換える (元に戻ったら外す)
    if (edit.kind == Kind::tie || last.kind == Kind::tie)
    {
        if (edit.kind == Kind::tie && last.kind == Kind::tie)
        {
            last.tieOrigin = edit.tieOrigin;
            last.tieDestination = edit.tieDestination;
            if (tieUnchanged (last))
                dropLast();
            return;
        }

        edits.push_back (edit);
        return;
    }

    switch (edit.kind)
    {
        case Kind::tie:
            break;

        case Kind::setFret:
            if (last.kind == Kind::setFret)
            {
                last.fret = edit.fret;   // ファイルに書かれているフレット (oldFret) は最初のまま
                if (last.fret == last.oldFret)
                    dropLast();
                return;
            }
            if (last.kind == Kind::add)
            {
                last.fret = edit.fret;   // 足した音のフレットを変えた → 初めからそのフレットで足す
                return;
            }
            break;

        case Kind::remove:
            if (last.kind == Kind::add)
            {
                dropLast();   // 足した音を消した (足すのを元に戻した)
                return;
            }
            if (last.kind == Kind::setFret)
            {
                // フレットを変えた音を消した → ファイルの音 (元のフレット) を消す
                last.kind = Kind::remove;
                last.fret = 0;
                return;
            }
            break;

        case Kind::add:
            if (last.kind == Kind::remove)
            {
                // 消した音を足し直した (消すのを元に戻した) → ファイルの音のフレットを変えるだけ
                last.kind = Kind::setFret;
                last.fret = edit.fret;
                if (last.fret == last.oldFret)
                    dropLast();
                return;
            }
            break;
    }

    edits.push_back (edit);
}

namespace
{
    // 足す音の手本: 同じ譜表の、弦とフレットが書かれている音 (GP が書く形をそのまま使うため)。
    // まず同じ拍、無ければ同じ譜表の小節を頭から探す
    juce::XmlElement* findTemplateNote (juce::XmlElement& gpif, juce::XmlElement* beat, int slot)
    {
        auto* notes = gpif.getChildByName ("Notes");
        const auto usable = [] (juce::XmlElement* note)
        {
            return note != nullptr && readProperty (*note, "String", "String", -1) >= 0 && findProperty (*note, "Fret") != nullptr;
        };

        if (beat != nullptr)
            for (const auto& id : tokensOf (beat->getChildByName ("Notes")))
                if (auto* note = findById (notes, id); usable (note))
                    return note;

        auto* bars = gpif.getChildByName ("Bars");
        auto* voices = gpif.getChildByName ("Voices");
        auto* beats = gpif.getChildByName ("Beats");
        if (auto* masterBars = gpif.getChildByName ("MasterBars"))
            for (auto* masterBar : masterBars->getChildIterator())
            {
                const auto barIds = tokensOf (masterBar->getChildByName ("Bars"));
                if (slot >= barIds.size())
                    continue;
                if (auto* bar = findById (bars, barIds[slot]))
                    for (const auto& voiceId : tokensOf (bar->getChildByName ("Voices")))
                        if (auto* voice = voiceId != noId ? findById (voices, voiceId) : nullptr)
                            for (const auto& beatId : tokensOf (voice->getChildByName ("Beats")))
                                if (auto* b = beatId != noId ? findById (beats, beatId) : nullptr)
                                    for (const auto& noteId : tokensOf (b->getChildByName ("Notes")))
                                        if (auto* note = findById (notes, noteId); usable (note))
                                            return note;
            }

        return nullptr;
    }

    void setProperty (juce::XmlElement& note, const juce::String& name, const juce::String& child, const juce::String& value)
    {
        auto* property = findProperty (note, name);
        if (property == nullptr)
        {
            auto* properties = note.getChildByName ("Properties");
            if (properties == nullptr)
                properties = note.createNewChildElement ("Properties");
            property = properties->createNewChildElement ("Property");
            property->setAttribute ("name", name);
        }

        auto* element = property->getChildByName (child);
        if (element == nullptr)
            element = property->createNewChildElement (child);
        writeValue (*element, value);
    }

    // 拍の <Notes> (無ければ <Rhythm> のすぐ後ろに作る。休符の拍には <Notes> が無い)
    juce::XmlElement& notesListOf (juce::XmlElement& beat)
    {
        if (auto* list = beat.getChildByName ("Notes"))
            return *list;

        int index = 0, rhythmIndex = -1;
        for (auto* child : beat.getChildIterator())
        {
            if (child->hasTagName ("Rhythm"))
                rhythmIndex = index;
            ++index;
        }

        auto* list = new juce::XmlElement ("Notes");
        beat.insertChildElement (list, rhythmIndex + 1);
        return *list;
    }

    juce::Result addNote (juce::XmlElement& gpif, juce::XmlElement& beat, const NoteEdit& edit)
    {
        auto* notes = gpif.getChildByName ("Notes");
        auto& list = notesListOf (beat);
        auto tokens = tokensOf (&list);

        for (const auto& id : tokens)
            if (auto* note = findById (notes, id); note != nullptr && readProperty (*note, "String", "String", -1) == edit.string - 1)
                return juce::Result::fail ("その弦には、もう音があります"_jp);

        if (edit.string < 1 || edit.string > (int) edit.tuning.size())
            return juce::Result::fail ("弦の音の高さが分かりません"_jp);

        const auto pitchOf = [&edit] (int alphaTabString, int fret)
        {
            return juce::isPositiveAndBelow (alphaTabString - 1, (int) edit.tuning.size()) ? edit.tuning[(size_t) alphaTabString - 1] + fret : -1;
        };

        juce::XmlElement* note = nullptr;
        if (auto* templateNote = findTemplateNote (gpif, &beat, edit.slot))
        {
            // 手本の写しから、音の高さと弦・フレット以外 (タイ・ミュート・ベンドなどの奏法) を外す
            note = new juce::XmlElement (*templateNote);
            for (int i = note->getNumChildElements(); --i >= 0;)
                if (auto* child = note->getChildElement (i); ! child->hasTagName ("Properties") && ! child->hasTagName ("InstrumentArticulation"))
                    note->removeChildElement (child, true);

            if (auto* properties = note->getChildByName ("Properties"))
            {
                static const juce::StringArray keep { "String", "Fret", "Midi", "ConcertPitch", "TransposedPitch", "Tone", "Octave" };
                for (int i = properties->getNumChildElements(); --i >= 0;)
                    if (auto* property = properties->getChildElement (i); ! keep.contains (property->getStringAttribute ("name")))
                        properties->removeChildElement (property, true);
            }

            const auto templatePitch = pitchOf (readProperty (*templateNote, "String", "String", 0) + 1, readProperty (*templateNote, "Fret", "Fret", 0));
            const auto newPitch = pitchOf (edit.string, edit.fret);
            if (templatePitch < 0 || newPitch < 0)
            {
                delete note;
                return juce::Result::fail ("弦の音の高さが分かりません"_jp);
            }
            shiftNotePitch (*note, newPitch - templatePitch);
        }
        else
        {
            // 譜表に音が1つも無い: 弦・フレット・音の高さだけの音を作る
            note = new juce::XmlElement ("Note");
            setProperty (*note, "Midi", "Number", juce::String (pitchOf (edit.string, edit.fret)));
        }

        setProperty (*note, "String", "String", juce::String (edit.string - 1));
        setProperty (*note, "Fret", "Fret", juce::String (edit.fret));

        const auto id = juce::String (nextFreeId (notes));
        note->setAttribute ("id", id);
        notes->addChildElement (note);

        tokens.add (id);
        setTokens (list, tokens);
        return juce::Result::ok();
    }

    juce::Result removeNote (juce::XmlElement& gpif, juce::XmlElement& beat, int tokenIndex)
    {
        auto* notes = gpif.getChildByName ("Notes");
        auto* list = beat.getChildByName ("Notes");
        auto tokens = tokensOf (list);
        const auto id = tokens[tokenIndex];

        tokens.remove (tokenIndex);
        if (tokens.isEmpty())
            beat.removeChildElement (list, true);   // 音が無くなった拍は休符 (<Notes> が無い)
        else
            setTokens (*list, tokens);

        // どこからも使われなくなった音は消す
        if (countReferences (gpif.getChildByName ("Beats"), "Notes", id) == 0)
            if (auto* note = findById (notes, id))
                notes->removeChildElement (note, true);

        return juce::Result::ok();
    }

    //==============================================================================
    // 拍・小節の挿入
    //   音の長さは <Rhythm> (例: <NoteValue>Quarter</NoteValue><AugmentationDot count="1" />) を、拍が ref で指す

    // 長さ (64分音符の数) と、GP の書き方 (音価・付点の数)。長い順
    struct RestLength { int length; const char* noteValue; int dots; };
    const RestLength restLengths[] = {
        { 64, "Whole", 0 }, { 48, "Half", 1 }, { 32, "Half", 0 }, { 24, "Quarter", 1 }, { 16, "Quarter", 0 },
        { 12, "Eighth", 1 }, { 8, "Eighth", 0 }, { 4, "16th", 0 }, { 2, "32nd", 0 }, { 1, "64th", 0 }
    };

    // 同じ長さの <Rhythm> (3連符などの無いもの) の id。無ければ作る
    juce::String rhythmIdFor (juce::XmlElement& gpif, const RestLength& length)
    {
        auto* rhythms = gpif.getChildByName ("Rhythms");
        if (rhythms == nullptr)
            rhythms = gpif.createNewChildElement ("Rhythms");

        for (auto* rhythm : rhythms->getChildIterator())
        {
            auto* value = rhythm->getChildByName ("NoteValue");
            auto* dot = rhythm->getChildByName ("AugmentationDot");
            const auto dots = dot != nullptr ? dot->getIntAttribute ("count", 0) : 0;
            if (value != nullptr && value->getAllSubText().trim() == length.noteValue && dots == length.dots
                && rhythm->getChildByName ("PrimaryTuplet") == nullptr && rhythm->getChildByName ("SecondaryTuplet") == nullptr)
                return rhythm->getStringAttribute ("id");
        }

        const auto id = juce::String (nextFreeId (rhythms));
        auto* rhythm = rhythms->createNewChildElement ("Rhythm");
        rhythm->setAttribute ("id", id);
        writeValue (*rhythm->createNewChildElement ("NoteValue"), length.noteValue);
        if (length.dots > 0)
            rhythm->createNewChildElement ("AugmentationDot")->setAttribute ("count", length.dots);
        return id;
    }

    // 拍子 (例: "3/4") の1小節を埋める休符の拍を作り、その id を返す (例: 3/4 → 付点2分休符1つ。7/8 → 2分 + 8分)
    juce::StringArray makeRestBeats (juce::XmlElement& gpif, const juce::String& time)
    {
        auto* beats = gpif.getChildByName ("Beats");
        const auto numerator = juce::jmax (1, time.upToFirstOccurrenceOf ("/", false, false).getIntValue());
        const auto denominator = juce::jmax (1, time.fromFirstOccurrenceOf ("/", false, false).getIntValue());
        auto left = juce::jmax (1, numerator * 64 / denominator);

        juce::StringArray ids;
        while (left > 0)
        {
            const RestLength* length = &restLengths[std::size (restLengths) - 1];
            for (const auto& l : restLengths)
                if (l.length <= left) { length = &l; break; }

            const auto id = juce::String (nextFreeId (beats));
            auto* beat = beats->createNewChildElement ("Beat");
            beat->setAttribute ("id", id);
            beat->createNewChildElement ("Rhythm")->setAttribute ("ref", rhythmIdFor (gpif, *length));
            ids.add (id);
            left -= length->length;
        }
        return ids;
    }

    // masterBar 番目の小節の後ろに、空の小節を入れる (すべての譜表。拍子・調は同じ。繰り返し記号などは写さない)
    juce::Result insertBar (juce::XmlElement& gpif, int masterBarIndex)
    {
        auto* masterBars = gpif.getChildByName ("MasterBars");
        auto* bars = gpif.getChildByName ("Bars");
        auto* voices = gpif.getChildByName ("Voices");
        auto* source = masterBars->getChildElement (masterBarIndex);
        if (source == nullptr || source->getChildByName ("Bars") == nullptr)
            return juce::Result::fail ("タブ譜の小節が見つかりません"_jp);

        auto* masterBar = new juce::XmlElement ("MasterBar");
        for (const auto* name : { "Key", "Time", "TripletFeel" })
            if (auto* child = source->getChildByName (name))
                masterBar->addChildElement (new juce::XmlElement (*child));

        auto* timeElement = source->getChildByName ("Time");
        const auto time = timeElement != nullptr ? timeElement->getAllSubText().trim() : juce::String ("4/4");

        juce::StringArray newBarIds;
        for (const auto& barId : tokensOf (source->getChildByName ("Bars")))
        {
            auto* sourceBar = findById (bars, barId);

            // 声部は、元の小節と同じ数の欄 (ふつう 4)。1つ目だけ休符の拍を入れ、ほかは空 ("-1")
            const auto voiceCount = juce::jmax (1, sourceBar != nullptr ? tokensOf (sourceBar->getChildByName ("Voices")).size() : 4);
            const auto voiceId = juce::String (nextFreeId (voices));
            auto* voice = voices->createNewChildElement ("Voice");
            voice->setAttribute ("id", voiceId);
            setTokens (*voice->createNewChildElement ("Beats"), makeRestBeats (gpif, time));

            juce::StringArray voiceIds { voiceId };
            while (voiceIds.size() < voiceCount)
                voiceIds.add (noId);

            const auto newBarId = juce::String (nextFreeId (bars));
            auto* bar = bars->createNewChildElement ("Bar");
            bar->setAttribute ("id", newBarId);
            if (sourceBar != nullptr)
                if (auto* clef = sourceBar->getChildByName ("Clef"))
                    bar->addChildElement (new juce::XmlElement (*clef));
            setTokens (*bar->createNewChildElement ("Voices"), voiceIds);
            newBarIds.add (newBarId);
        }

        setTokens (*masterBar->createNewChildElement ("Bars"), newBarIds);
        masterBars->insertChildElement (masterBar, masterBarIndex + 1);

        // テンポなどの変化 (<Automation> の <Bar> は小節の番号) は、入れた小節より後ろなら1つずらす
        std::function<void (juce::XmlElement&)> shiftAutomations = [&] (juce::XmlElement& element)
        {
            for (auto* child : element.getChildIterator())
            {
                if (child->hasTagName ("Automation"))
                {
                    if (auto* barNumber = child->getChildByName ("Bar"))
                        if (const auto n = barNumber->getAllSubText().trim().getIntValue(); n > masterBarIndex)
                            writeValue (*barNumber, juce::String (n + 1));
                }
                else
                {
                    shiftAutomations (*child);
                }
            }
        };
        shiftAutomations (gpif);
        return juce::Result::ok();
    }

    // 拍の後ろに、同じ長さの休符の拍を入れる (beatList は makeUnique した声部の <Beats>、tokenIndex はその中の拍の場所)
    juce::Result insertBeat (juce::XmlElement& gpif, juce::XmlElement& beatList, int tokenIndex)
    {
        auto* beats = gpif.getChildByName ("Beats");
        auto tokens = tokensOf (&beatList);
        auto* source = findById (beats, tokens[tokenIndex]);
        if (source == nullptr)
            return juce::Result::fail ("タブ譜の拍が見つかりません"_jp);

        const auto id = juce::String (nextFreeId (beats));
        auto* beat = beats->createNewChildElement ("Beat");
        beat->setAttribute ("id", id);
        for (const auto* name : { "Dynamic", "Rhythm" })
            if (auto* child = source->getChildByName (name))
                beat->addChildElement (new juce::XmlElement (*child));

        tokens.insert (tokenIndex + 1, id);
        setTokens (beatList, tokens);
        return juce::Result::ok();
    }
}

juce::Result applyToGpif (juce::XmlElement& gpif, const NoteEdit& edit)
{
    const auto fail = [] (const juce::String& what) { return juce::Result::fail ("タブ譜の音が見つかりません ("_jp + what + ")"); };

    auto* masterBars = gpif.getChildByName ("MasterBars");
    auto* bars = gpif.getChildByName ("Bars");
    auto* voices = gpif.getChildByName ("Voices");
    auto* beats = gpif.getChildByName ("Beats");
    auto* notes = gpif.getChildByName ("Notes");
    if (masterBars == nullptr || bars == nullptr || voices == nullptr || beats == nullptr || notes == nullptr)
        return juce::Result::fail ("タブ譜のファイルの形が違います"_jp);

    if (edit.kind == Kind::insertBar)
        return insertBar (gpif, edit.masterBar);

    // 小節 → その譜表の Bar
    auto* masterBar = masterBars->getChildElement (edit.masterBar);
    auto* barList = masterBar != nullptr ? masterBar->getChildByName ("Bars") : nullptr;
    if (barList == nullptr || edit.slot < 0 || edit.slot >= tokensOf (barList).size())
        return fail ("小節");

    auto* bar = makeUnique (*barList, edit.slot, bars, masterBars, "Bars");
    auto* voiceList = bar != nullptr ? bar->getChildByName ("Voices") : nullptr;
    const auto voiceIndex = indexOfNth (tokensOf (voiceList), edit.voice);
    if (voiceIndex < 0)
        return fail ("声部");

    auto* voice = makeUnique (*voiceList, voiceIndex, voices, bars, "Voices");
    auto* beatList = voice != nullptr ? voice->getChildByName ("Beats") : nullptr;
    const auto beatIndex = indexOfNth (tokensOf (beatList), edit.beat);
    if (beatIndex < 0)
        return fail ("拍");

    if (edit.kind == Kind::insertBeat)
        return insertBeat (gpif, *beatList, beatIndex);

    auto* beat = makeUnique (*beatList, beatIndex, beats, voices, "Beats");
    if (beat == nullptr)
        return fail ("拍");

    if (edit.kind == Kind::add)
        return addNote (gpif, *beat, edit);

    auto* noteList = beat->getChildByName ("Notes");
    const auto noteTokens = tokensOf (noteList);

    // 弦が同じ音 (ファイルの String は 0 から)
    for (int i = 0; i < noteTokens.size(); ++i)
    {
        auto* candidate = findById (notes, noteTokens[i]);
        if (candidate == nullptr || readProperty (*candidate, "String", "String", -1) != edit.string - 1)
            continue;

        // ずれていないかの確かめ: ファイルのフレットが、ページで読んだときのものと同じ (タイの付け外しはフレットを見ない)
        if (edit.kind != Kind::tie && readProperty (*candidate, "Fret", "Fret", 0) != edit.oldFret)
            return juce::Result::fail ("タブ譜のファイルが、読み込んだときから変わっています"_jp);

        if (edit.kind == Kind::remove)
            return removeNote (gpif, *beat, i);

        auto* note = makeUnique (*noteList, i, notes, beats, "Notes");

        if (edit.kind == Kind::tie)
        {
            // <Tie origin="true" destination="false" /> (GP の書き方。どちらも false なら書かない)
            if (auto* tie = note->getChildByName ("Tie"))
                note->removeChildElement (tie, true);
            if (edit.tieOrigin || edit.tieDestination)
            {
                auto* tie = new juce::XmlElement ("Tie");
                tie->setAttribute ("origin", edit.tieOrigin ? "true" : "false");
                tie->setAttribute ("destination", edit.tieDestination ? "true" : "false");
                note->prependChildElement (tie);
            }
            return juce::Result::ok();
        }

        setProperty (*note, "Fret", "Fret", juce::String (edit.fret));
        shiftNotePitch (*note, edit.fret - edit.oldFret);
        return juce::Result::ok();
    }

    return fail ("弦");
}

namespace
{
    // zip (.gp) の score.gpif に編集を当て、zip を作り直して out に書く:
    // score.gpif だけ書き換えたもの、ほかはそのまま写す (並びも同じ)
    juce::Result rewriteZip (juce::ZipFile& zip, const std::vector<NoteEdit>& edits, juce::OutputStream& out)
    {
        const auto gpifIndex = zip.getIndexOfFileName ("Content/score.gpif");
        if (gpifIndex < 0)
            return juce::Result::fail ("Guitar Pro 7 以降の .gp ではないので、保存できません"_jp);

        auto gpifStream = ScoreFileData::openEntryLimited (zip, gpifIndex);   // 大きさを偽った zip でも、書かれた大きさまで
        if (gpifStream == nullptr)
            return juce::Result::fail ("タブ譜のファイルを読めませんでした"_jp);

        auto gpif = juce::XmlDocument::parse (gpifStream->readEntireStreamAsString());
        if (gpif == nullptr)
            return juce::Result::fail ("タブ譜のファイルを読めませんでした"_jp);

        for (const auto& edit : edits)
            if (auto result = applyToGpif (*gpif, edit); result.failed())
                return result;

        juce::ZipFile::Builder builder;
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* entry = zip.getEntry (i);
            if (i == gpifIndex)
            {
                const auto text = gpif->toString();
                builder.addEntry (std::make_unique<juce::MemoryInputStream> (text.toRawUTF8(), text.getNumBytesAsUTF8(), true),
                                  6, entry->filename, entry->fileTime);
            }
            else if (auto stream = ScoreFileData::openEntryLimited (zip, i); stream != nullptr)
            {
                // 大きいもの (音声) は、圧縮し直さずにそのまま入れる (音声はほとんど縮まないうえ、圧縮に時間がかかり、保存の間アプリが止まるため)
                builder.addEntry (std::move (stream), entry->uncompressedSize > 4 * 1024 * 1024 ? 0 : 6, entry->filename, entry->fileTime);
            }
        }

        if (! builder.writeToStream (out, nullptr))
            return juce::Result::fail ("タブ譜のファイルを書けませんでした"_jp);
        return juce::Result::ok();
    }
}

juce::Result applyToGpData (const juce::MemoryBlock& gpData, const std::vector<NoteEdit>& edits, juce::MemoryBlock& result)
{
    juce::ZipFile zip (std::make_unique<juce::MemoryInputStream> (gpData, false));
    result.reset();
    juce::MemoryOutputStream out (result, false);
    const auto written = rewriteZip (zip, edits, out);
    out.flush();   // 書いた大きさに result を合わせる
    return written;
}

juce::Result applyToGpFile (const juce::File& gpFile, const std::vector<NoteEdit>& edits)
{
    if (edits.empty())
        return juce::Result::ok();

    if (! gpFile.existsAsFile())
        return juce::Result::fail ("タブ譜のファイルが見つかりません"_jp);

    juce::TemporaryFile temp (gpFile);

    {
        juce::ZipFile zip (gpFile);
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk())
            return juce::Result::fail ("タブ譜のファイルを書けませんでした"_jp);
        if (auto result = rewriteZip (zip, edits, out); result.failed())
            return result;
        out.flush();
    }   // ここで元のファイルを閉じる (置き換えられるように)

    // 初めて書き換えるときは、元のファイルを残しておく
    const juce::File backup (gpFile.getFullPathName() + ".bak");
    if (! backup.existsAsFile() && ! gpFile.copyFileTo (backup))
        return juce::Result::fail ("元のファイルの控え (.bak) を作れませんでした"_jp);

    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("タブ譜のファイルを書き換えられませんでした (ほかのアプリで開いていませんか)"_jp);

    return juce::Result::ok();
}
}
