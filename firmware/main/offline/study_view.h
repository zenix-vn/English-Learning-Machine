// Drawing of the offline study screens (LVGL only, no application logic).
// Layout numbers match docs/offline-ui-preview.html, which is the visual reference for the 960 x 376 panel.
#pragma once

#include <lvgl.h>

#include <string>
#include <vector>

#include "pack.h"

namespace offline {

namespace ui {
constexpr uint32_t kText = 0xF1F5FF, kMuted = 0x9AA7C7, kAccent = 0x6C8CFF;
constexpr uint32_t kNew = 0x4DA3FF, kLearn = 0xFFB020, kDue = 0xA78BFA, kExtra = 0x8794B8;
constexpr uint32_t kGood = 0x34D399, kAgain = 0xFB7185, kEasy = 0xFBBF24;
}  // namespace ui

enum class Glyph { kClick, kDouble, kHold };       // one press, two presses, press and hold
struct Hint {
    Glyph glyph;
    const char* label;
    uint32_t color;                                // 0 = neutral
};

struct HeaderInfo {
    const char* tag = "";                          // pill at the top left
    uint32_t tag_color = ui::kExtra;
    bool show_counters = true;
    uint32_t learning = 0, due = 0, fresh = 0;
    int progress_pct = 0;                          // today's progress bar, 0..100
};

enum class Icon { kOk, kWarn };

class StudyView {
public:
    bool Build();
    void Destroy();
    bool built() const { return root_ != nullptr; }

    // Every call below takes the display lock itself.
    void SetHeader(const HeaderInfo& info);
    void SetHints(const std::vector<Hint>& hints);
    void ShowFront(const Card& card);
    void ShowBack(const Card& card);
    void ShowMenu(const char* const* items, int count, int selected);
    void ShowNotice(Icon icon, const std::string& title, const std::string& body);
    void ShowBusy(const std::string& title, const std::string& status, int percent);
    void SetBusy(const std::string& status, int percent);          // percent < 0: no bar
    void Toast(const std::string& text, uint32_t color);

private:
    friend void OnToastTimer(lv_timer_t*);
    void ShowGroup(lv_obj_t* group);
    void HideToast();

    lv_obj_t *root_ = nullptr, *tag_ = nullptr, *tag_label_ = nullptr, *counters_ = nullptr, *track_ = nullptr,
             *fill_ = nullptr, *card_ = nullptr, *legend_ = nullptr, *toast_ = nullptr, *toast_label_ = nullptr;
    lv_obj_t *counter_dot_[3] = {}, *counter_label_[3] = {}, *counter_chip_[3] = {};
    // front
    lv_obj_t *front_ = nullptr, *word_f_ = nullptr, *ipa_row_ = nullptr, *ipa_f_ = nullptr;
    // back
    lv_obj_t *back_ = nullptr, *word_b_ = nullptr, *ipa_b_ = nullptr, *meaning_ = nullptr, *ex_bar_ = nullptr,
             *ex_en_ = nullptr, *ex_vi_ = nullptr;
    // menu
    lv_obj_t *list_ = nullptr, *row_[3] = {}, *row_label_[3] = {}, *row_arrow_[3] = {};
    // notice and busy
    lv_obj_t *notice_ = nullptr, *badge_ = nullptr, *badge_label_ = nullptr, *title_ = nullptr, *body_ = nullptr,
             *bar_track_ = nullptr, *bar_fill_ = nullptr;
    lv_timer_t* toast_timer_ = nullptr;
};

}  // namespace offline
