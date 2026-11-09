// Drawing of the offline study and dictionary screens (LVGL only, no application logic).
// Portrait 376 x 960 panel, same child-friendly look as the home screen (home/ui_kit.h).
#pragma once

#include <lvgl.h>

#include <string>
#include <vector>

#include "home/ui_kit.h"
#include "pack.h"

namespace offline {

namespace tone {
constexpr uint32_t kNew = 0x2563EB, kLearn = 0xD97706, kDue = 0x7C3AED, kExtra = 0x6B7280, kDict = 0x0D9488;
constexpr uint32_t kGood = 0x16A34A, kAgain = 0xDC2626, kEasy = 0xD97706;
}  // namespace tone

using Glyph = ui::Glyph;                           // one press, two presses, press and hold
using Hint = ui::Hint;                             // {glyph, label, color (0 = neutral), strong}

struct HeaderInfo {
    const char* tag = "";                          // pill at the top left
    uint32_t tag_color = tone::kExtra;
    std::string right;                             // small text at the top right, for example "Còn 7 từ"
    int progress_pct = -1;                         // bar under the header, 0..100; -1 hides it
};

enum class Icon { kOk, kWarn };

struct ListRow {
    std::string title, subtitle;
    bool special = false;                          // "back" style row
};

class StudyView {
public:
    bool Build();
    void Destroy();
    bool built() const { return root_ != nullptr; }

    // Every call below takes the display lock itself.
    void SetHeader(const HeaderInfo& info);
    void SetHints(const std::vector<Hint>& hints);
    void ShowFront(const Card& card, const std::string& caption);
    void ShowBack(const Card& card);
    void ShowMenu(const char* const* items, int count, int selected);
    void ShowNotice(Icon icon, const std::string& title, const std::string& body);
    void ShowBusy(const std::string& title, const std::string& status, int percent);
    void SetBusy(const std::string& status, int percent);          // percent < 0: no bar
    void Toast(const std::string& text, uint32_t color);
    // Dictionary: 26 letters then an exit cell. `has[i]` says whether letter i has words.
    void ShowLetters(const bool* has, int selected);
    void ShowList(const std::vector<ListRow>& rows, int selected);

    static constexpr int kListRows = 9;

private:
    friend void OnStudyToastTimer(lv_timer_t*);
    void ShowGroup(lv_obj_t* group);
    void HideToast();

    lv_obj_t *root_ = nullptr, *tag_ = nullptr, *tag_label_ = nullptr, *right_ = nullptr, *track_ = nullptr,
             *fill_ = nullptr, *card_ = nullptr, *hints_ = nullptr, *toast_ = nullptr, *toast_label_ = nullptr;
    // front
    lv_obj_t *front_ = nullptr, *word_f_ = nullptr, *ipa_f_ = nullptr, *caption_ = nullptr;
    // back
    lv_obj_t *back_ = nullptr, *word_b_ = nullptr, *ipa_b_ = nullptr, *meaning_ = nullptr, *ex_en_ = nullptr,
             *ex_vi_ = nullptr, *ex_head_ = nullptr;
    // menu
    lv_obj_t *menu_ = nullptr, *menu_row_[3] = {}, *menu_label_[3] = {};
    // notice and busy
    lv_obj_t *notice_ = nullptr, *badge_ = nullptr, *badge_icon_ = nullptr, *title_ = nullptr, *body_ = nullptr,
             *bar_track_ = nullptr, *bar_fill_ = nullptr, *pct_label_ = nullptr;
    std::string busy_text_;            // what the sync screen shows now, so an update redraws only what changed
    int busy_pct_ = -2;                // -2: nothing shown yet, -1: no bar
    // dictionary
    lv_obj_t *letters_ = nullptr, *cell_[27] = {}, *cell_label_[27] = {};
    lv_obj_t *list_ = nullptr, *row_[kListRows] = {}, *row_title_[kListRows] = {}, *row_sub_[kListRows] = {};
    lv_timer_t* toast_timer_ = nullptr;
};

}  // namespace offline
