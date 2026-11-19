#pragma once

#include <juce_core/juce_core.h>

// .gp (Guitar Pro 7 以降) のタブ譜の音を書き換えて保存する (作者の指示: タブ譜の編集)
//
// .gp は zip で、楽譜は Content/score.gpif (XML) に入っている。ページ (alphaTab) で変えた音を、
// この XML の同じ場所に書き込み、ほかのファイル (音声など) はそのまま写して、.gp を作り直す。
// alphaTab の書き出し (Gp7Exporter) を使わないのは、alphaTab が知らない情報 (音声のトラックなど) が消えるため。
//
// 音の場所は、alphaTab が楽譜を組み立てるときと同じ順で数える (alphaTab 1.8.4 の GpifParser と同じ):
//   MasterBars の masterBar 番目の <Bars> の slot 番目 (トラックの順に、各トラックの譜表の数だけ並ぶ) → Bar
//   Bar の <Voices> のうち "-1" でないものの voice 番目 → Voice
//   Voice の <Beats> のうち "-1" でないものの beat 番目 → Beat
//   Beat の <Notes> のうち、弦 (String) が同じもの → Note
// GP のファイルは、同じ中身の Bar・Voice・Beat・Note を、1つの id で何か所からも使い回すことがある。
// 書き換えるものが使い回されていたら、写しを作ってから書き換える (ほかの場所は変えない)
namespace GpScoreEdit
{
    // tie: タイ (前の音とつながっている) の付け外し。タイでつながった音の片方を消す・フレットを変えるときに、ページが送る
    // insertBeat: その拍の後ろに、同じ長さの休符の拍を入れる (作者の指示: 右クリックで拍を挿入)。
    // insertBar: その小節 (masterBar) の後ろに、同じ拍子の空の小節 (休符だけ) を、すべての譜表に入れる
    enum class Kind { setFret, add, remove, tie, insertBeat, insertBar };

    inline bool isInsert (Kind k) noexcept { return k == Kind::insertBeat || k == Kind::insertBar; }

    struct NoteEdit
    {
        Kind kind = Kind::setFret;
        int slot = 0;        // 譜表の番号 (トラックの順に、各トラックの譜表の数だけ数えたもの)
        int masterBar = 0;   // 楽譜の小節 (0 から)
        int voice = 0;
        int beat = 0;
        int string = 1;      // alphaTab の弦の番号 (1 = いちばん低い弦。ファイルの String は、これから 1 を引いたもの)
        int oldFret = 0;     // setFret・remove: ファイルに書かれているフレット (違っていたら書き換えない)
        int fret = 0;        // setFret・add: 新しいフレット
        std::vector<int> tuning;   // add: 各弦の開放の音の高さ (MIDI。[0] が弦 1)。足す音の高さを決めるのに使う
        // tie: この音がタイの始め (origin)・終わり (destination) か。old... は変える前 (元に戻ったかを見るのに使う)
        bool tieOrigin = false, tieDestination = false, oldTieOrigin = false, oldTieDestination = false;

        bool isSameNote (const NoteEdit& other) const noexcept
        {
            return slot == other.slot && masterBar == other.masterBar && voice == other.voice
                && beat == other.beat && string == other.string;
        }
    };

    // 編集を1つ足す。すぐ前の編集と同じ音なら、1つにまとめる (元に戻ったら、どちらも外す)。
    // ページの「元に戻す」(Ctrl+Z) は、逆の編集として届くので、ここでまとめると、戻しきったときに編集が無くなる
    void addEdit (std::vector<NoteEdit>& edits, const NoteEdit& edit);

    // score.gpif (XML の根) の中の音を書き換える (フレットを変える・音を足す・音を消す)
    juce::Result applyToGpif (juce::XmlElement& gpif, const NoteEdit& edit);

    // .gp のファイルを書き換える (編集を順に当てる)。初めて書き換えるときは、元のファイルを「名前.gp.bak」に写しておく
    juce::Result applyToGpFile (const juce::File& gpFile, const std::vector<NoteEdit>& edits);

    // メモリの中の .gp (zip) に編集を当てた .gp を作る (ファイルは書き換えない)。
    // 拍・小節を挿入したとき、ページに読み直させる楽譜を作るのに使う
    juce::Result applyToGpData (const juce::MemoryBlock& gpData, const std::vector<NoteEdit>& edits, juce::MemoryBlock& result);

    // 編集できるタブ譜か (Guitar Pro 7 以降の .gp)
    inline bool isEditable (const juce::File& file) { return file.hasFileExtension ("gp"); }
}
