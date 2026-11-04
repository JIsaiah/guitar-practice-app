#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../AppSettings.h"
#include "../Library/SessionLibrary.h"
#include "../Library/SetlistStore.h"
#include "../Theme/MetalLogo/FixedTextButton.h"
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

private:
    // ListBoxModel
    int getNumRows() override { return (int) entries.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;   // 星 (難易度) と右クリックのメニュー
    void selectedRowsChanged (int) override { updateButtons(); }

    // 一覧の列 (見出しと行で同じ位置を使う)
    struct Columns
    {
        juce::Rectangle<int> title, artist, bpm, difficulty, tuning, length, modified;
    };
    static Columns layoutColumns (int width, int height);
    static int starAt (const Columns& columns, int x);   // x の位置の星 (1〜5)。星の上でなければ 0

    void setDifficulty (int row, int stars);
    void showRowMenu (int row);
    void editDetails (int row);

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
    std::vector<SessionStore::Entry> entries;
    Tab currentTab = Tab::sessions;

    MetalLogoLabel appTitle;
    FixedTextButton sessionsTab, setlistsTab;
    juce::ListBox listBox { {}, this };
    FixedTextButton newButton, openButton, deleteButton, settingsButton;
    FixedTextToggle deathMetalToggle;
    juce::ComboBox themeBox;   // テーマ (Windows に合わせる／ライト／ダーク)

    SetlistPanel setlistPanel;
    SessionPickerPanel picker;
    juce::String pickerTargetId;   // 追加先のセットリスト (カードは読み直しで作り直されるので ID で持つ)

    juce::Rectangle<int> headerArea;   // 一覧の列の見出し (曲名・アーティスト…)
    std::unique_ptr<juce::AlertWindow> detailsWindow;   // 「詳細を編集…」のダイアログ
};
