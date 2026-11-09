// Home screen: big coloured tiles for the learning programmes, made for children, plus the settings screen.
// Visual reference: the "English Learner – Giao diện máy" design canvas (376 x 960, portrait).
//
// The board has no touch panel and one usable button (Boot), so everything is driven by gestures:
//   click = next item     double click = previous item     hold = open / change
// Saying the wake word still starts a conversation from anywhere.
//
// Tiles: new words, review and dictionary run offline (main/offline); "Nói với Sophia" and "Phát âm" talk to the
// tutor on the server. The settings button sits at the bottom of the list. Settings lets the learner choose the
// layout (two columns of tiles, or one tile per row), volume, brightness and start a vocabulary sync.
//
// The home screen covers the chat screen while the device is idle. Opening an online tile (or the wake word)
// switches to the chat screen; after the conversation ends and the device has been idle for a few seconds the home
// screen comes back by itself. A hold during a conversation ends it (a click or double click starts / stops talking).
#pragma once

#include <lvgl.h>

#include "setup_screen.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace home {

enum class Layout { kGrid = 0, kList = 1 };

class HomeScreen {
public:
    static HomeScreen& GetInstance();

    // Builds the screens (hidden until the device is idle). Call once after Display::SetupUI().
    void Build();

    // True while the home or settings screen is what the learner sees.
    bool Visible() const { return visible_; }
    // True while a conversation started from the home screen (or by the wake word) is on screen.
    bool InChat() const { return chat_; }

    // Button gestures. Each returns true when the screen used the gesture.
    // Safe to call from the button task: they take the display lock.
    bool OnClick();
    bool OnDoubleClick();
    bool OnLongPress();

    // Called by the study screens inside their page switch: brings the home flags and pages in line with the device
    // state without a switch of its own.
    void RefreshNow();

    // The learner skipped Wi-Fi setup: show the home screen (all tiles, settings) without a network. Tiles that need
    // the server show a notice; Settings has a Wi-Fi row that goes back to the setup. Cleared by itself once the
    // device leaves the start-up / Wi-Fi setup states. Any task.
    void EnterOfflineHome();

    // First-run screens (see setup_screen.h). Called by the Wi-Fi and activation code; any task.
    void ShowWifiSetup(const std::string& ssid, const std::string& url);
    void ShowActivation(const std::string& code);      // the voice starts reading the code now

    // Text of the conversation, forwarded by the display (see ElDisplay in the board file).
    // role is "user" (what the child said) or "assistant" (what Sophia says, one sentence at a time).
    void SetChatText(const char* role, const char* content);
    // A short message at the bottom of the current page. Any task.
    void Notify(const char* text);
    // Firmware download progress on the splash page (0..100; -1 = not downloading). Any task.
    void SetUpgradeProgress(int percent);
    void ClearChatText();

    // Optional learning data. Zero or negative values hide the element (nothing is shown that is not real).
    void SetGoal(int done, int total);          // stars in the speech bubble
    void SetBadge(int tile, int count);         // number on a tile (0 = new words, 1 = review, ...)
    void SetStreak(int days);                   // flame pill in the status bar

private:
    HomeScreen() = default;
    friend void OnHomeTimer(lv_timer_t* timer);
    friend void OnHomeToastTimer(lv_timer_t* timer);
    friend void OnHomeRefresh(void* arg);
    friend void OnHomeAnim(lv_timer_t* timer);

    void Tick();                                // runs in the LVGL task every 100 ms
    void AnimTick();                            // Sophia's movements, every 150 ms
    void RequestRefresh();                      // like Tick, but soon and in the LVGL task (callable from any task)
    // What should be on screen follows the device state. When it differs from what is, ui::SwitchPage prepares the
    // new page off-screen and flips to it. The study screens switch pages themselves (see study.cc), so while one
    // is active Tick only keeps the state up to date.
    enum class Mode { kNone, kHome, kSettings, kInfo, kChat, kSplash, kWifi, kCode };
    Mode Target() const;
    void UpdateState();                         // flags from the device state (no page change)
    void Apply();                               // switches pages when needed
    void ShowTarget();                          // shows/hides the pages at once (inside SwitchPage)
    void BuildTiles();
    void BuildSettings();
    void BuildInfo();                           // the device information page (opened from Settings)
    void UpdateInfo();                          // reads version, id, network ... when the page opens
    void BuildChat();
    void UpdateChat(int state);                     // pill, ring and placeholder for the device state
    void Focus(int index);
    void Mark(lv_obj_t* o, lv_obj_t* rim, int base_y, bool on);
    void Open(int index);
    void ChatPress();                           // the talk button in a conversation (click, or a double click)
    void StartChatWait();                       // a conversation (re)starts: wait for Sophia's first words
    void Toast(const char* text);
    void UpdateClock();
    void UpdateSettings();
    void ChangeSetting(int row);
    void OpenSettings(bool open);
    bool Online() const;                        // the server can be reached (some tiles and the sync need it)
    void ConnectWifi();                         // back to the Wi-Fi setup

    static constexpr int kTiles = 8;
    static constexpr int kItems = kTiles + 1;       // the tiles, then the settings button
    static constexpr int kSettingRows = 7;
    static constexpr int kInfoRows = 6;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* wifi_ = nullptr;
    lv_obj_t* clock_ = nullptr;
    lv_obj_t* streak_ = nullptr;
    lv_obj_t* streak_label_ = nullptr;
    lv_obj_t* greeting_ = nullptr;
    lv_obj_t* stars_ = nullptr;
    lv_obj_t* star_[10] = {};
    lv_obj_t* tiles_ = nullptr;                     // container rebuilt when the layout changes
    lv_obj_t* tile_[kTiles] = {};
    lv_obj_t* tile_rim_[kTiles] = {};               // darker edge under each tile, shown only under the selected one
    int tile_y_[kTiles] = {};                       // resting position of each tile
    int settings_y_ = 0;
    lv_obj_t* badge_[kTiles] = {};
    lv_obj_t* badge_label_[kTiles] = {};
    lv_obj_t* settings_btn_ = nullptr;
    lv_obj_t* settings_under_ = nullptr;
    lv_obj_t* hold_label_ = nullptr;                // "Mở <item>" in the legend, follows the focus
    lv_obj_t* set_hold_label_ = nullptr;            // the same on the settings page
    lv_obj_t* toast_ = nullptr;
    lv_obj_t* toast_label_ = nullptr;
    lv_timer_t* toast_timer_ = nullptr;

    lv_obj_t* set_root_ = nullptr;
    lv_obj_t* set_row_[kSettingRows] = {};
    lv_obj_t* set_rim_[kSettingRows] = {};          // paper edge under each row, shown only under the selected one
    lv_obj_t* set_disc_[kSettingRows] = {};         // coloured icon disc, faded when the row is not selected
    lv_obj_t* set_value_[kSettingRows] = {};
    lv_obj_t* set_icon_[kSettingRows] = {};
    lv_obj_t* set_bar_[kSettingRows] = {};          // fill of the volume / brightness bars

    lv_obj_t* info_root_ = nullptr;
    lv_obj_t* info_value_[kInfoRows] = {};
    bool info_open_ = false;

    lv_obj_t* chat_root_ = nullptr;
    lv_obj_t* chat_pill_ = nullptr;
    lv_obj_t* chat_dot_ = nullptr;
    lv_obj_t* chat_status_ = nullptr;
    lv_obj_t* chat_ring_ = nullptr;
    lv_obj_t* chat_mascot_ = nullptr;
    int anim_frame_ = -1;
    int anim_step_ = 0;
    int64_t next_blink_us_ = 0, blink_until_us_ = 0;
    lv_obj_t* chat_user_box_ = nullptr;
    lv_obj_t* chat_user_ = nullptr;
    lv_obj_t* chat_bot_box_ = nullptr;
    lv_obj_t* chat_bot_ = nullptr;
    std::string chat_user_text_, chat_bot_text_;
    int chat_state_ = -1;
    bool chat_shown_ = false;
    bool chat_translate_ = false;
    std::string chat_open_text_;                    // what the tile sent to the server; sent again after a reconnect
    bool chat_greeted_ = false;                     // Sophia has spoken (or listened) since the conversation began
    int64_t chat_wait_until_us_ = 0;                // until then, presses do not interrupt the opening
    SetupScreen setup_;
    Mode setup_target_ = Mode::kNone;               // the first-run page the device state asks for
    std::string activation_code_;
    std::string splash_text_;                   // the conversation is interpreter mode

    Layout layout_ = Layout::kGrid;
    int focus_ = 0;                                 // 0..6 on the home screen
    int set_focus_ = 0;                             // 0..4 on the settings screen
    int badge_count_[kTiles] = {};
    bool settings_open_ = false;
    std::atomic<bool> visible_{false};
    std::atomic<bool> chat_{false};
    int64_t idle_since_us_ = 0;
    int last_minute_ = -1;
    int last_wifi_ = -1;
    Mode shown_ = Mode::kNone;                      // what is on screen now
    std::atomic<bool> offline_home_{false};         // Wi-Fi setup skipped: home screen without a network
    bool offline_ = false;                         // a study screen is active
};

}  // namespace home
