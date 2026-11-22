#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../AppSettings.h"
#include "AppDialog.h"
#include "../Library/SessionLibrary.h"
#include "../Library/SetlistStore.h"
#include "../Theme/MetalLogo/FixedTextButton.h"
#include "../Library/SessionFolders.h"
#include "../Library/SessionSort.h"
#include "../Library/SpatialNavigation.h"
#include "SessionFolderList.h"
#include "Icons.h"
#include "SessionPickerPanel.h"
#include "SetlistPanel.h"

// 起動直後の画面。保存済みのセッションを選んで開く (設計書 4.1 の 1)
// 上部のタブで「セッション」と「セットリスト」を切り替える (setlist-mode.md 4.1)
class SessionListScreen final : public juce::Component,
                                private juce::ListBoxModel,
                                private juce::ChangeListener
{
public:
    SessionListScreen (SessionLibrary& library, SetlistStore& setlists, ThemeManager& theme, AppSettings& settings);
    ~SessionListScreen() override;

    std::function<void()> onNewSession;
    std::function<void (const juce::String& fileId)> onOpenSession;            // セッションのタブから (ファイル名)
    std::function<void (const juce::String& sessionId)> onOpenSessionById;     // セットリストから「単体で開く」
    std::function<void (const juce::String& setlistId, int entryIndex)> onPlaySetlist;
    std::function<void()> onOpenSettings;

    enum class Tab { sessions = 0, setlists = 1 };
    void showTab (Tab tab);
    void openFirstSetlistForSnapshot();   // 開発用 (DevSnapshots): 1つ目のセットリストを開き、追加パネルを出す

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;     // 列の見出しのクリックで並び替え
    void mouseMove (const juce::MouseEvent& e) override;   // 並び替えられる見出しの上では指のカーソル

    // キーボード操作 (作者の指示): 矢印で選ぶ場所を動かし、Enter で押す。選んでいる場所はアクセントの色の枠で囲み、
    // 動かすと枠がなめらかに次の場所へ飛ぶ。マウスを押すと枠は消える (次に矢印を押すと、また出る)
    bool keyPressed (const juce::KeyPress& key) override;
    void showKeyboardNavigationForSnapshot();
   #if JUCE_DEBUG
    juce::String describeScreenForLog() const;
   #endif   // 開発用 (DevSnapshots): 選んでいる行に枠を出す
    // いちばん上のセッションの行に、キーボード操作の枠を出し、キーの入力を受け取る (アプリを起動したとき。Enter ですぐ開ける)。
    // 画面がまだ出ていなければ、出るまで少しずつ待つ (attempts 回まで、0.05 秒ごと)
    void showNavigationOnFirstSession (int attempts = 40);

private:
    // ListBoxModel
    // 最後に空の行を1つ足す: 一覧の右下に重ねた「＋」で、いちばん下のセッションが隠れないように (作者の指示)
    int getNumRows() override { return entries.empty() ? 0 : (int) entries.size() + 1; }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;   // 星 (難易度) と右クリックのメニュー
    void selectedRowsChanged (int) override { updateButtons(); }

    // 一覧の列 (見出しと行で同じ位置を使う)
    struct Columns
    {
        juce::Rectangle<int> title, artist, format, bpm, difficulty, tuning, length, lastPracticed;
    };
    static Columns layoutColumns (int width, int height);
    static int starAt (const Columns& columns, int x);   // x の位置の星 (1〜5)。星の上でなければ 0

    // 並び替え。見出しの位置 (この画面の座標) から列を求める。BPM と見出しの外なら無し
    std::optional<SessionSort::Key> sortKeyAt (juce::Point<int> position) const;
    void sortBy (SessionSort::Key key);

    void setDifficulty (int row, int stars);
    void showRowMenu (int row);
    void editDetails (int row);

    // フォルダ分け (左の列)
    void applyFolderFilter (bool keepSelection);   // entries を、選んでいるフォルダのものだけにする
    void updateFolderList();
    juce::StringArray getFolderNames() const;
    void askFolderName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onOk);
    void createFolder (std::function<void (juce::String)> then = nullptr);
    void renameFolder (const juce::String& oldName);
    void deleteFolder (const juce::String& name);
    void moveToFolder (const juce::String& sessionFileId, const juce::String& folder);
    void updateEntry (const Session& updated);   // 保存したセッションの内容を、一覧の行 (allEntries・entries) に反映する

    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // テーマが変わったとき
    void refresh();
    void updateButtons();
    void openSelected();
    void deleteSelected();

    // 曲の追加パネル (セットリストのタブ)。card が nullptr なら閉じる
    void showPicker (SetlistCard* card);

    SessionLibrary& library;
    SessionStore& store;
    SetlistStore& setlistStore;
    ThemeManager& theme;
    AppSettings& settings;
    std::vector<SessionStore::Entry> allEntries;   // すべてのセッション
    std::vector<SessionStore::Entry> entries;      // 一覧に出しているもの (選んでいるフォルダのもの)
    juce::String folderFilter = SessionFolders::allKey;
    Tab currentTab = Tab::sessions;
    SessionSort::Key sortKey = SessionSort::Key::lastPracticed;
    bool sortAscending = false;

    // アプリの名前とテーマの切り替えは、作者のレイアウトに無いので置かない (テーマは設定画面で変える)
    FixedTextButton sessionsTab, setlistsTab;
    juce::ListBox listBox { {}, this };
    SessionFolderList folderList;
    IconButton settingsButton { Icons::settings };   // タブの帯の右の歯車 (枠なし。作者のレイアウト)
    IconButton newButton { Icons::plus };   // 一覧の枠の右下の「＋」(新しいセッション)。開く・削除は、行のダブルクリックと右クリックのメニューで

    SetlistPanel setlistPanel;
    SessionPickerPanel picker;
    juce::String pickerTargetId;   // 追加先のセットリスト (カードは読み直しで作り直されるので ID で持つ)

    juce::Rectangle<int> headerArea;    // 一覧の列の見出し (曲名・アーティスト…)。一覧の枠の中の上
    juce::Rectangle<int> tabBar;        // 「セッション｜セットリスト」のつながった帯 (paint で面・枠・区切りの線を描く)

    // タブの帯の、選んでいるほうの塗り。0 = 左 (セッション)、1 = 右 (セットリスト)。
    // 押すと、選んだほうの塗りが真ん中からぼかしながら広がって現れ (フェードイン)、前のほうは同じように消える (作者の指示)
    // 動きは、画面の書き換えの合図 (frameVBlank) ごとに「押してからの経過時間」で決める (画面が 60 Hz でも 144 Hz でも同じ速さ)
    float tabHighlight = 0.0f, tabHighlightTarget = 0.0f;
    float tabHighlightFrom = 0.0f;        // 動き始めたときの値 (途中で押し直したら、その途中の値)
    double tabMoveStartMs = 0.0;
    static constexpr double tabMoveDurationMs = 250.0;
    void stepTabHighlight (double nowMs);
    void updateTabTextColours();   // 塗りが重なっているほうの文字を白に (動いている間は、重なり具合で混ぜる)
    juce::Rectangle<int> listPanel;     // 一覧の枠 (見出し・一覧・「＋」を囲む面)

    //==== キーボード操作 (SessionListNavigation.cpp) ====
    // 矢印で選べる場所。id は、一覧の読み直しやスクロールのあとも同じ場所を指す名前 ("row:<ファイル>" など)
    struct NavTarget
    {
        juce::String id;
        juce::Rectangle<int> bounds;          // この画面の座標 (スクロールで見えていない場所も、その位置で)
        const juce::Component* group = nullptr;   // スクロールする入れ物 (一覧・セットリストの欄)。無ければ nullptr
        bool visible = true;                  // 入れ物の中で見えているか
        std::function<void()> reveal;         // 選んだとき (見えるところまでスクロールする・行を選ぶ)
        std::function<void()> activate;       // Enter
    };
    std::vector<NavTarget> collectNavTargets();
    void moveNavigation (SpatialNavigation::Direction direction);
    void selectNavTarget (const NavTarget& target);
    void hideNavigation();
    std::optional<juce::Rectangle<float>> findNavTargetBounds();   // 今選んでいる場所 (無くなっていれば無し)
    void stepNavigationRing (double nowMs);   // 画面の書き換えの合図ごとに、経過時間から枠の場所を決める
    void paintNavigationRing (juce::Graphics& g);
    void updateKeyboardHighlights();   // キーボード操作の場所に合わせて、一覧・フォルダの塗りを変える
    void revealInViewport (juce::Viewport& viewport, juce::Rectangle<int> boundsInScreen);
    void keepKeyboardFocus();    // キーの入力を、中のボタンや一覧ではなくこの画面で受け取る

    bool navVisible = false;
    juce::String navId;
    juce::Rectangle<float> navRing;       // 今描いている枠
    juce::Rectangle<float> navClip;       // 枠を描いてよい範囲 (スクロールする入れ物の中の枠は、入れ物の外に描かない)
    juce::Rectangle<float> navMoveFrom;   // 飛び始めた場所
    double navMoveStartMs = 0.0, navShownAtMs = 0.0;
    static constexpr double navMoveDurationMs = 180.0;   // 飛ぶのにかかる時間
    float navAlpha = 0.0f;                // 出るときに、ふわっと濃くする

    // 画面の書き換えの合図 (タブの塗りとキーボードの枠で共用)。合図は「その書き換えの時刻」を渡してくれるので、それで動きを決める。
    // 合図がメッセージの列を通って届くまでの遅れ (毎回ばらつく) に左右されず、動きがなめらかになる
    std::unique_ptr<juce::VBlankAttachment> frameVBlank;
    void ensureFrameVBlank();          // 動きを始めるときに作る (一度作ったら、画面が消えるまで残す)
    void onFrame (double timestampSec);
    bool isAnimating() const;

   #if JUCE_DEBUG
    // 開発用 (後で消す): 動いている間の、合図の間隔・描くのにかかった時間を測り、動き1回ごとに timing.log へ書く
    struct FrameStats
    {
        bool active = false;
        int frames = 0;
        double lastFrameMs = 0.0, intervalSum = 0.0, intervalMax = 0.0, lagMax = 0.0, paintMax = 0.0;
    } frameStats;
    double paintStartMs = 0.0;
   #endif

    // 画面のどこかでマウスが押されたら、枠を消し、キーの入力をこの画面へ戻す
    struct NavMouseWatcher final : juce::MouseListener
    {
        explicit NavMouseWatcher (SessionListScreen& o) : owner (o) {}
        void mouseDown (const juce::MouseEvent&) override { owner.hideNavigation(); owner.keepKeyboardFocus(); }
        void mouseUp (const juce::MouseEvent&) override   { owner.keepKeyboardFocus(); }
        SessionListScreen& owner;
    };
    NavMouseWatcher navMouseWatcher { *this };
    std::unique_ptr<AppDialog> detailsWindow;   // 「詳細を編集…」のダイアログ
    std::unique_ptr<AppDialog> folderNameWindow;   // フォルダの名前を入れるダイアログ
};
