#include <juce_core/juce_core.h>
#include "../Source/Web/GpScoreEdit.h"

// タブ譜の編集の保存: .gp の score.gpif の音 (フレット) を書き換える
class GpScoreEditTests final : public juce::UnitTest
{
public:
    GpScoreEditTests() : juce::UnitTest ("GpScoreEdit", "Web") {}

    // 2小節・2つの譜表。小節 0 の Bar、Voice の中の Beat、Beat の中の Note を使い回している (GP のファイルの書き方)
    static juce::String sampleGpif()
    {
        return R"(<?xml version="1.0" encoding="UTF-8"?>
<GPIF>
<MasterBars>
 <MasterBar><Bars>0 1</Bars></MasterBar>
 <MasterBar><Bars>0 1</Bars></MasterBar>
</MasterBars>
<Bars>
 <Bar id="0"><Voices>0 -1 -1 -1</Voices></Bar>
 <Bar id="1"><Voices>-1 1 -1 -1</Voices></Bar>
</Bars>
<Voices>
 <Voice id="0"><Beats>0 -1 0</Beats></Voice>
 <Voice id="1"><Beats>1</Beats></Voice>
</Voices>
<Beats>
 <Beat id="0"><Notes>0 1</Notes></Beat>
 <Beat id="1"><Notes>0</Notes></Beat>
</Beats>
<Notes>
 <Note id="0"><Properties>
  <Property name="String"><String>5</String></Property>
  <Property name="Fret"><Fret>3</Fret></Property>
  <Property name="Midi"><Number>67</Number></Property>
  <Property name="ConcertPitch"><Pitch><Step>G</Step><Accidental></Accidental><Octave>4</Octave></Pitch></Property>
 </Properties></Note>
 <Note id="1"><Properties>
  <Property name="String"><String>0</String></Property>
  <Property name="Fret"><Fret>0</Fret></Property>
 </Properties></Note>
</Notes>
</GPIF>)";
    }

    static GpScoreEdit::NoteEdit fretEdit (int slot, int bar, int voice, int beat, int string, int oldFret, int fret)
    {
        GpScoreEdit::NoteEdit e;
        e.slot = slot; e.masterBar = bar; e.voice = voice; e.beat = beat; e.string = string; e.oldFret = oldFret; e.fret = fret;
        return e;
    }

    static GpScoreEdit::NoteEdit addEditOf (int beat, int string, int fret)
    {
        auto e = fretEdit (0, 0, 0, beat, string, 0, fret);
        e.kind = GpScoreEdit::Kind::add;
        e.tuning = { 38, 45, 50, 55, 59, 64 };   // ドロップ D (弦 1 = いちばん低い D)
        return e;
    }

    static GpScoreEdit::NoteEdit removeEditOf (int beat, int string, int oldFret)
    {
        auto e = fretEdit (0, 0, 0, beat, string, oldFret, 0);
        e.kind = GpScoreEdit::Kind::remove;
        return e;
    }

    // 1小節・音のある拍と休符の拍 (本物の GP のファイルと同じ形の音。ミュートとタイが付いている)
    static juce::String restGpif()
    {
        return R"(<?xml version="1.0" encoding="UTF-8"?>
<GPIF>
<MasterBars><MasterBar><Bars>0</Bars></MasterBar></MasterBars>
<Bars><Bar id="0"><Voices>0 -1 -1 -1</Voices></Bar></Bars>
<Voices><Voice id="0"><Beats>0 1</Beats></Voice></Voices>
<Beats>
 <Beat id="0"><Rhythm ref="0" /><Notes>0</Notes></Beat>
 <Beat id="1"><Dynamic>MF</Dynamic><Rhythm ref="0" /><Properties></Properties></Beat>
</Beats>
<Notes>
 <Note id="0"><Tie origin="true" destination="false" /><InstrumentArticulation>0</InstrumentArticulation><Properties>
  <Property name="ConcertPitch"><Pitch><Step>D</Step><Accidental></Accidental><Octave>4</Octave></Pitch></Property>
  <Property name="Fret"><Fret>0</Fret></Property>
  <Property name="Midi"><Number>50</Number></Property>
  <Property name="Muted"><Enable /></Property>
  <Property name="String"><String>2</String></Property>
  <Property name="TransposedPitch"><Pitch><Step>D</Step><Accidental></Accidental><Octave>5</Octave></Pitch></Property>
 </Properties></Note>
</Notes>
</GPIF>)";
    }

    static juce::XmlElement* beatById (juce::XmlElement& gpif, const juce::String& id)
    {
        for (auto* beat : gpif.getChildByName ("Beats")->getChildIterator())
            if (beat->getStringAttribute ("id") == id)
                return beat;
        return nullptr;
    }

    static bool hasProperty (juce::XmlElement& note, const juce::String& name)
    {
        for (auto* p : note.getChildByName ("Properties")->getChildIterator())
            if (p->getStringAttribute ("name") == name)
                return true;
        return false;
    }

    static juce::XmlElement* noteById (juce::XmlElement& gpif, const juce::String& id)
    {
        for (auto* note : gpif.getChildByName ("Notes")->getChildIterator())
            if (note->getStringAttribute ("id") == id)
                return note;
        return nullptr;
    }

    static juce::String property (juce::XmlElement& note, const juce::String& name, const juce::String& path)
    {
        for (auto* p : note.getChildByName ("Properties")->getChildIterator())
            if (p->getStringAttribute ("name") == name)
            {
                auto* e = p;
                for (const auto& part : juce::StringArray::fromTokens (path, "/", ""))
                    e = e != nullptr ? e->getChildByName (part) : nullptr;
                return e != nullptr ? e->getAllSubText().trim() : juce::String();
            }
        return {};
    }

    static juce::String listText (juce::XmlElement& gpif, const juce::String& collection, const juce::String& id, const juce::String& list)
    {
        for (auto* item : gpif.getChildByName (collection)->getChildIterator())
            if (item->getStringAttribute ("id") == id)
                return item->getChildByName (list)->getAllSubText().trim();
        return {};
    }

    void runTest() override
    {
        using GpScoreEdit::NoteEdit;

        beginTest ("A shared note is copied, and only this place changes");
        {
            auto gpif = juce::XmlDocument::parse (sampleGpif());
            // 2小節目・1つ目の譜表・声部 0・2つ目の拍 (間の -1 は数えない)・いちばん高い弦 (6) を 3 → 5
            expect (GpScoreEdit::applyToGpif (*gpif, fretEdit (0, 1, 0, 1, 6, 3, 5)).wasOk());

            // 使い回していたものは、すべて写しが作られる
            expectEquals (gpif->getChildByName ("MasterBars")->getChildElement (1)->getChildByName ("Bars")->getAllSubText().trim(), juce::String ("2 1"));
            expectEquals (listText (*gpif, "Bars", "2", "Voices"), juce::String ("2 -1 -1 -1"));
            expectEquals (listText (*gpif, "Voices", "2", "Beats"), juce::String ("0 -1 2"));
            expectEquals (listText (*gpif, "Beats", "2", "Notes"), juce::String ("2 1"));

            auto* edited = noteById (*gpif, "2");
            expect (edited != nullptr);
            expectEquals (property (*edited, "Fret", "Fret"), juce::String ("5"));
            expectEquals (property (*edited, "Midi", "Number"), juce::String ("69"));          // 音の高さも 2 つ上がる
            expectEquals (property (*edited, "ConcertPitch", "Pitch/Step"), juce::String ("A"));
            expectEquals (property (*edited, "ConcertPitch", "Pitch/Octave"), juce::String ("4"));

            // 元の音 (1小節目など、ほかの場所) は変わらない
            expectEquals (property (*noteById (*gpif, "0"), "Fret", "Fret"), juce::String ("3"));
            expectEquals (listText (*gpif, "Voices", "0", "Beats"), juce::String ("0 -1 0"));
        }

        beginTest ("The second staff and a sharp");
        {
            auto gpif = juce::XmlDocument::parse (sampleGpif());
            // 1小節目・2つ目の譜表 (声部は -1 の次の 1 番だが、alphaTab では 0 番目)
            expect (GpScoreEdit::applyToGpif (*gpif, fretEdit (1, 0, 0, 0, 6, 3, 4)).wasOk());
            auto* edited = noteById (*gpif, "2");   // Note 0 は Beat 0 と 1 で使い回し → 写し
            expect (edited != nullptr);
            expectEquals (property (*edited, "Midi", "Number"), juce::String ("68"));
            expectEquals (property (*edited, "ConcertPitch", "Pitch/Step"), juce::String ("G"));
            expectEquals (property (*edited, "ConcertPitch", "Pitch/Accidental"), juce::String ("#"));
        }

        beginTest ("A note that does not match the file is not changed");
        {
            auto gpif = juce::XmlDocument::parse (sampleGpif());
            expect (GpScoreEdit::applyToGpif (*gpif, fretEdit (0, 0, 0, 0, 6, 7, 5)).failed());   // ファイルのフレットは 3
            expect (GpScoreEdit::applyToGpif (*gpif, fretEdit (0, 0, 0, 0, 3, 0, 5)).failed());   // その弦の音は無い
            expect (GpScoreEdit::applyToGpif (*gpif, fretEdit (0, 9, 0, 0, 6, 3, 5)).failed());   // 小節が無い
        }

        beginTest ("Edits to the same note are merged, and dropped when back to the original");
        {
            std::vector<NoteEdit> edits;
            GpScoreEdit::addEdit (edits, fretEdit (0, 0, 0, 0, 6, 3, 5));
            GpScoreEdit::addEdit (edits, fretEdit (0, 0, 0, 0, 6, 5, 7));   // 同じ音をもう一度 (ページでは 5 になっていた)
            expectEquals ((int) edits.size(), 1);
            expectEquals (edits[0].oldFret, 3);
            expectEquals (edits[0].fret, 7);
            GpScoreEdit::addEdit (edits, fretEdit (0, 0, 0, 0, 6, 7, 3));   // 元に戻した
            expect (edits.empty());
        }

        beginTest ("Adding a note to a rest copies the form of a note nearby, without its techniques");
        {
            auto gpif = juce::XmlDocument::parse (restGpif());
            expect (GpScoreEdit::applyToGpif (*gpif, addEditOf (1, 6, 3)).wasOk());   // 休符の拍・いちばん高い弦 (MIDI 64) の 3 フレット = MIDI 67 (G)

            auto* beat = beatById (*gpif, "1");
            expectEquals (beat->getChildByName ("Notes")->getAllSubText().trim(), juce::String ("1"));
            expect (beat->getChildElement (2)->hasTagName ("Notes"));   // <Rhythm> のすぐ後ろ

            auto* added = noteById (*gpif, "1");
            expect (added != nullptr);
            expectEquals (property (*added, "String", "String"), juce::String ("5"));
            expectEquals (property (*added, "Fret", "Fret"), juce::String ("3"));
            expectEquals (property (*added, "Midi", "Number"), juce::String ("67"));
            expectEquals (property (*added, "ConcertPitch", "Pitch/Step"), juce::String ("G"));
            expectEquals (property (*added, "ConcertPitch", "Pitch/Octave"), juce::String ("5"));     // GP のオクターブは MIDI ÷ 12
            expectEquals (property (*added, "TransposedPitch", "Pitch/Octave"), juce::String ("6"));  // ギターの楽譜は1オクターブ上に書く
            expect (! hasProperty (*added, "Muted"));                    // 手本の奏法は写さない
            expect (added->getChildByName ("Tie") == nullptr);
            expect (added->getChildByName ("InstrumentArticulation") != nullptr);

            expect (GpScoreEdit::applyToGpif (*gpif, addEditOf (0, 3, 5)).failed());   // その弦には、もう音がある
        }

        beginTest ("Removing the last note of a beat makes it a rest");
        {
            auto gpif = juce::XmlDocument::parse (restGpif());
            expect (GpScoreEdit::applyToGpif (*gpif, removeEditOf (0, 3, 4)).failed());   // ファイルのフレットは 0
            expect (GpScoreEdit::applyToGpif (*gpif, removeEditOf (0, 3, 0)).wasOk());
            expect (beatById (*gpif, "0")->getChildByName ("Notes") == nullptr);
            expect (noteById (*gpif, "0") == nullptr);   // どこからも使われなくなった音は消す
        }

        beginTest ("A shared note is only removed from this place");
        {
            auto gpif = juce::XmlDocument::parse (sampleGpif());
            auto e = fretEdit (0, 0, 0, 0, 6, 3, 0);
            e.kind = GpScoreEdit::Kind::remove;
            expect (GpScoreEdit::applyToGpif (*gpif, e).wasOk());
            expect (noteById (*gpif, "0") != nullptr);   // ほかの拍で使っているので残る
            expectEquals (listText (*gpif, "Beats", "1", "Notes"), juce::String ("0"));
        }

        beginTest ("Tie: written on both ends, and merged when put back");
        {
            auto gpif = juce::XmlDocument::parse (restGpif());
            auto untie = fretEdit (0, 0, 0, 0, 3, 0, 0);
            untie.kind = GpScoreEdit::Kind::tie;
            untie.oldTieOrigin = true;   // 手本の音は、タイの始め
            expect (GpScoreEdit::applyToGpif (*gpif, untie).wasOk());
            expect (noteById (*gpif, "0")->getChildByName ("Tie") == nullptr);   // どちらでもなくなったら書かない

            auto retie = untie;
            retie.tieOrigin = true; retie.oldTieOrigin = false;
            expect (GpScoreEdit::applyToGpif (*gpif, retie).wasOk());
            auto* tie = noteById (*gpif, "0")->getChildByName ("Tie");
            expect (tie != nullptr && tie->getStringAttribute ("origin") == "true" && tie->getStringAttribute ("destination") == "false");

            std::vector<GpScoreEdit::NoteEdit> edits;
            GpScoreEdit::addEdit (edits, untie);
            GpScoreEdit::addEdit (edits, retie);   // 元に戻した (Ctrl+Z)
            expect (edits.empty());
        }

        beginTest ("Undo merges with the edit just before it");
        {
            std::vector<GpScoreEdit::NoteEdit> edits;
            GpScoreEdit::addEdit (edits, addEditOf (1, 6, 3));
            GpScoreEdit::addEdit (edits, fretEdit (0, 0, 0, 1, 6, 3, 7));   // 足した音のフレットを変えた
            expectEquals ((int) edits.size(), 1);
            expect (edits[0].kind == GpScoreEdit::Kind::add && edits[0].fret == 7);
            GpScoreEdit::addEdit (edits, removeEditOf (1, 6, 7));            // 足した音を消した (Ctrl+Z)
            expect (edits.empty());

            GpScoreEdit::addEdit (edits, fretEdit (0, 0, 0, 0, 3, 0, 5));
            GpScoreEdit::addEdit (edits, removeEditOf (0, 3, 5));            // フレットを変えた音を消した
            expectEquals ((int) edits.size(), 1);
            expect (edits[0].kind == GpScoreEdit::Kind::remove && edits[0].oldFret == 0);
            GpScoreEdit::addEdit (edits, addEditOf (0, 3, 5));               // 消すのを元に戻した (Ctrl+Z)
            expect (edits[0].kind == GpScoreEdit::Kind::setFret && edits[0].fret == 5);
            GpScoreEdit::addEdit (edits, fretEdit (0, 0, 0, 0, 3, 5, 0));   // フレットも元に戻した (Ctrl+Z)
            expect (edits.empty());

            GpScoreEdit::addEdit (edits, fretEdit (0, 0, 0, 0, 3, 0, 5));
            GpScoreEdit::addEdit (edits, fretEdit (0, 0, 0, 1, 6, 0, 2));    // 別の音
            GpScoreEdit::addEdit (edits, fretEdit (0, 0, 0, 0, 3, 5, 0));    // 前の前の音: まとめない (順に当てる)
            expectEquals ((int) edits.size(), 3);
        }

        beginTest (".gp file: score.gpif is rewritten, other files are kept, and a backup is made");
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GpScoreEditTests");
            dir.deleteRecursively();
            dir.createDirectory();
            const auto gp = dir.getChildFile ("song.gp");

            juce::MemoryBlock audio;
            for (int i = 0; i < 5000; ++i)
                audio.append (&i, 1);

            {
                juce::ZipFile::Builder builder;
                const auto text = sampleGpif();
                builder.addEntry (std::make_unique<juce::MemoryInputStream> (text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 6, "Content/score.gpif", {});
                builder.addEntry (std::make_unique<juce::MemoryInputStream> (audio, true), 6, "Content/Assets/audio.wav", {});
                juce::FileOutputStream out (gp);
                expect (builder.writeToStream (out, nullptr));
            }

            expect (GpScoreEdit::applyToGpFile (gp, { fretEdit (0, 1, 0, 1, 6, 3, 5) }).wasOk());
            expect (gp.withFileExtension ("gp.bak").existsAsFile());

            {
                juce::ZipFile zip (gp);
                expectEquals (zip.getNumEntries(), 2);
                std::unique_ptr<juce::InputStream> gpifStream (zip.createStreamForEntry (zip.getIndexOfFileName ("Content/score.gpif")));
                auto gpif = juce::XmlDocument::parse (gpifStream->readEntireStreamAsString());
                expect (gpif != nullptr && noteById (*gpif, "2") != nullptr);
                if (gpif != nullptr && noteById (*gpif, "2") != nullptr)
                    expectEquals (property (*noteById (*gpif, "2"), "Fret", "Fret"), juce::String ("5"));

                std::unique_ptr<juce::InputStream> audioStream (zip.createStreamForEntry (zip.getIndexOfFileName ("Content/Assets/audio.wav")));
                juce::MemoryBlock readBack;
                audioStream->readIntoMemoryBlock (readBack);
                expect (readBack == audio);
            }   // 閉じてから、もう一度書き換える

            // 2回目の保存: 控えは最初のまま (書き換えない)
            const auto backupSize = gp.withFileExtension ("gp.bak").getSize();
            expect (GpScoreEdit::applyToGpFile (gp, { fretEdit (0, 0, 0, 0, 6, 3, 2) }).wasOk());
            expectEquals (gp.withFileExtension ("gp.bak").getSize(), backupSize);

            dir.deleteRecursively();
        }

        // ---- 拍・小節の挿入 ----
        const auto insertOf = [] (GpScoreEdit::Kind kind, int bar, int beat)
        {
            auto e = fretEdit (0, bar, 0, beat, 1, 0, 0);
            e.kind = kind;
            return e;
        };

        beginTest ("Insert a beat: same rhythm, after the beat, in a copy of a shared voice");
        {
            auto gpif = juce::parseXML (restGpif());
            // restGpif: 拍 0 (音) と 拍 1 (休符)。拍 0 の後ろに入れる
            expect (GpScoreEdit::applyToGpif (*gpif, insertOf (GpScoreEdit::Kind::insertBeat, 0, 0)).wasOk());
            expectEquals (listText (*gpif, "Voices", "0", "Beats"), juce::String ("0 2 1"));
            auto* inserted = beatById (*gpif, "2");
            expect (inserted != nullptr && inserted->getChildByName ("Notes") == nullptr);   // 休符
            expectEquals (inserted->getChildByName ("Rhythm")->getStringAttribute ("ref"), juce::String ("0"));

            // 入れた拍 (拍 1 になった) に音を足せる
            expect (GpScoreEdit::applyToGpif (*gpif, addEditOf (1, 3, 0)).wasOk());
            expect (beatById (*gpif, "2")->getChildByName ("Notes") != nullptr);

            // 使い回されている声部 (sampleGpif の小節 0 と 1) は、写してから入れる (小節 1 は変わらない)
            auto shared = juce::parseXML (sampleGpif());
            expect (GpScoreEdit::applyToGpif (*shared, insertOf (GpScoreEdit::Kind::insertBeat, 0, 0)).wasOk());
            expectEquals (listText (*shared, "Voices", "0", "Beats"), juce::String ("0 -1 0"));
        }

        beginTest ("Insert a bar: every staff gets an empty bar with rests that fill the time signature");
        {
            auto gpif = juce::parseXML (R"(<?xml version="1.0" encoding="UTF-8"?>
<GPIF>
<MasterTrack><Automations>
 <Automation><Type>Tempo</Type><Bar>0</Bar><Value>120 2</Value></Automation>
 <Automation><Type>Tempo</Type><Bar>1</Bar><Value>140 2</Value></Automation>
</Automations></MasterTrack>
<MasterBars>
 <MasterBar><Key><AccidentalCount>0</AccidentalCount></Key><Time>7/8</Time><Repeat start="true" end="false" count="0" /><Bars>0 1</Bars></MasterBar>
 <MasterBar><Time>7/8</Time><Bars>0 1</Bars></MasterBar>
</MasterBars>
<Bars>
 <Bar id="0"><Clef>G2</Clef><Voices>0 -1 -1 -1</Voices></Bar>
 <Bar id="1"><Clef>F4</Clef><Voices>0 -1</Voices></Bar>
</Bars>
<Voices><Voice id="0"><Beats>0</Beats></Voice></Voices>
<Beats><Beat id="0"><Rhythm ref="0" /></Beat></Beats>
<Rhythms><Rhythm id="0"><NoteValue>Half</NoteValue><AugmentationDot count="1" /></Rhythm></Rhythms>
<Notes />
</GPIF>)");
            expect (GpScoreEdit::applyToGpif (*gpif, insertOf (GpScoreEdit::Kind::insertBar, 0, 0)).wasOk());

            auto* masterBars = gpif->getChildByName ("MasterBars");
            expectEquals (masterBars->getNumChildElements(), 3);
            auto* inserted = masterBars->getChildElement (1);
            expectEquals (inserted->getChildByName ("Time")->getAllSubText(), juce::String ("7/8"));
            expect (inserted->getChildByName ("Key") != nullptr);
            expect (inserted->getChildByName ("Repeat") == nullptr);   // 繰り返し記号は写さない
            expectEquals (inserted->getChildByName ("Bars")->getAllSubText().trim(), juce::String ("2 3"));

            // 譜表ごとの小節: 音部記号と声部の欄の数は元と同じ。1つ目の声部に休符 (7/8 = 付点2分 + 8分)
            expectEquals (listText (*gpif, "Bars", "2", "Voices"), juce::String ("1 -1 -1 -1"));
            expectEquals (listText (*gpif, "Bars", "3", "Voices"), juce::String ("2 -1"));
            expectEquals (listText (*gpif, "Bars", "3", "Clef"), juce::String ("F4"));
            const auto restIds = juce::StringArray::fromTokens (listText (*gpif, "Voices", "1", "Beats"), " ", "");
            expectEquals (restIds.size(), 2);
            const auto rhythmOf = [&] (const juce::String& beatId)
            {
                const auto ref = beatById (*gpif, beatId)->getChildByName ("Rhythm")->getStringAttribute ("ref");
                for (auto* r : gpif->getChildByName ("Rhythms")->getChildIterator())
                    if (r->getStringAttribute ("id") == ref)
                        return r->getChildByName ("NoteValue")->getAllSubText()
                             + (r->getChildByName ("AugmentationDot") != nullptr ? "." : "");
                return juce::String();
            };
            expectEquals (rhythmOf (restIds[0]), juce::String ("Half."));   // 元からある Rhythm を使う
            expectEquals (rhythmOf (restIds[1]), juce::String ("Eighth"));  // 無いものは作る

            // テンポの変化: 入れた小節より後ろのものだけ、1つずれる
            auto* automations = gpif->getChildByName ("MasterTrack")->getChildByName ("Automations");
            expectEquals (automations->getChildElement (0)->getChildByName ("Bar")->getAllSubText(), juce::String ("0"));
            expectEquals (automations->getChildElement (1)->getChildByName ("Bar")->getAllSubText(), juce::String ("2"));
        }

        beginTest ("Inserts are never merged with other edits");
        {
            std::vector<NoteEdit> edits;
            GpScoreEdit::addEdit (edits, insertOf (GpScoreEdit::Kind::insertBeat, 0, 0));
            GpScoreEdit::addEdit (edits, removeEditOf (0, 1, 0));   // 同じ場所でも、まとめない
            GpScoreEdit::addEdit (edits, insertOf (GpScoreEdit::Kind::insertBar, 0, 0));
            expectEquals ((int) edits.size(), 3);
        }

        beginTest ("Edits applied in memory: the .gp data is rebuilt without touching a file");
        {
            juce::MemoryBlock gpData;
            {
                juce::ZipFile::Builder builder;
                const auto text = restGpif();
                builder.addEntry (std::make_unique<juce::MemoryInputStream> (text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 6, "Content/score.gpif", {});
                juce::MemoryOutputStream out (gpData, false);
                expect (builder.writeToStream (out, nullptr));
            }

            juce::MemoryBlock result;
            expect (GpScoreEdit::applyToGpData (gpData, { insertOf (GpScoreEdit::Kind::insertBeat, 0, 0) }, result).wasOk());
            juce::ZipFile zip (std::make_unique<juce::MemoryInputStream> (result, false));
            std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (zip.getIndexOfFileName ("Content/score.gpif")));
            expect (stream != nullptr);
            if (stream != nullptr)
            {
                auto gpif = juce::XmlDocument::parse (stream->readEntireStreamAsString());
                expectEquals (listText (*gpif, "Voices", "0", "Beats"), juce::String ("0 2 1"));
            }
        }
    }
};

static GpScoreEditTests gpScoreEditTests;
