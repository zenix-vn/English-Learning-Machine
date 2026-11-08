#include "study_view.h"

#include "board.h"
#include "display.h"
#include "fonts/fonts.h"
#include "text_util.h"

namespace offline {

namespace {

// ---- design tokens (see docs/offline-ui-preview.html)
constexpr int kW = 960, kH = 376, kMargin = 24;
constexpr int kCardX = kMargin, kCardY = 78, kCardW = 912, kCardH = 222, kCardRadius = 28;
constexpr uint32_t kBg = 0x0B1020, kCard = 0x121A30, kLine = 0x223055, kChip = 0x16203A, kTrack = 0x1B2440;
constexpr uint32_t kAccentBg = 0x3452E8, kRowText = 0xC9D3F0;
constexpr lv_opa_t kTint = 41;                                    // 16 % tint behind coloured text

const lv_font_t* F64() { return &font_en_64; }
const lv_font_t* F44() {
#if EL_HAVE_FONT_EN44
    return &font_en_44;
#else
    return &font_en_64;
#endif
}
const lv_font_t* F28() {
#if EL_HAVE_FONT_VI
    return &font_vi_28;
#else
    return LV_FONT_DEFAULT;     // ASCII only: run scripts/gen_fonts.sh for Vietnamese and IPA
#endif
}
const lv_font_t* F22() {
#if EL_HAVE_FONT_VI22
    return &font_vi_22;
#else
    return F28();
#endif
}
const lv_font_t* WordFont(int px) { return px == 64 ? F64() : px == 44 ? F44() : F28(); }

lv_color_t Hex(uint32_t rgb) { return lv_color_hex(rgb); }

void SetShown(lv_obj_t* o, bool shown) {
    if (shown) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// Transparent container: no border, padding or scrolling.
lv_obj_t* Plain(lv_obj_t* parent, int w, int h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// Filled rounded rectangle.
lv_obj_t* Box(lv_obj_t* parent, int w, int h, uint32_t color, int radius, lv_opa_t opa = LV_OPA_COVER) {
    lv_obj_t* o = Plain(parent, w, h);
    lv_obj_set_style_bg_color(o, Hex(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

// w == 0: size to content. h == 0: wrap onto as many lines as needed. Both set: clip with "..." after the last line.
lv_obj_t* Text(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int w = 0, int h = 0,
               int align = LV_TEXT_ALIGN_LEFT) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, Hex(color), 0);
    lv_obj_set_style_text_align(l, align, 0);
    if (w && h) {
        lv_obj_set_size(l, w, h);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    } else if (w) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
    return l;
}

// Pill that sizes itself to its content: horizontal flex row with vertically centred children.
lv_obj_t* Pill(lv_obj_t* parent, int h, uint32_t bg, lv_opa_t opa, int pad_left, int pad_right, int gap) {
    lv_obj_t* o = Box(parent, 0, h, bg, h / 2, opa);
    lv_obj_set_size(o, LV_SIZE_CONTENT, h);
    lv_obj_set_style_pad_left(o, pad_left, 0);
    lv_obj_set_style_pad_right(o, pad_right, 0);
    lv_obj_set_style_pad_column(o, gap, 0);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return o;
}

lv_obj_t* Dot(lv_obj_t* parent, int size, uint32_t color) { return Box(parent, size, size, color, LV_RADIUS_CIRCLE); }

}  // namespace

// Called by LVGL when the toast has been shown long enough. LVGL deletes a one-shot timer after this returns.
void OnToastTimer(lv_timer_t* timer) {
    auto* view = static_cast<StudyView*>(lv_timer_get_user_data(timer));
    view->toast_timer_ = nullptr;
    view->HideToast();
}

void StudyView::HideToast() {
    if (toast_) lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
}

bool StudyView::Build() {
    if (root_) return true;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());

    root_ = Box(lv_layer_top(), kW, kH, kBg, 0);
    lv_obj_set_pos(root_, 0, 0);

    // ---- header: tag pill on the left, queue counters on the right
    tag_ = Pill(root_, 34, ui::kExtra, kTint, 16, 16, 0);
    lv_obj_set_pos(tag_, kMargin, 14);
    tag_label_ = Text(tag_, F22(), ui::kExtra);

    counters_ = Plain(root_, 0, 34);
    lv_obj_set_size(counters_, LV_SIZE_CONTENT, 34);
    lv_obj_set_flex_flow(counters_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(counters_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(counters_, 10, 0);
    lv_obj_align(counters_, LV_ALIGN_TOP_RIGHT, -kMargin, 14);
    const uint32_t dots[3] = {ui::kLearn, ui::kDue, ui::kNew};
    for (int i = 0; i < 3; ++i) {
        counter_chip_[i] = Pill(counters_, 34, kChip, LV_OPA_COVER, 14, 14, 8);
        counter_dot_[i] = Dot(counter_chip_[i], 10, dots[i]);
        counter_label_[i] = Text(counter_chip_[i], F22(), ui::kText);
    }

    // ---- today's progress
    track_ = Box(root_, kCardW, 6, kTrack, 3);
    lv_obj_set_pos(track_, kMargin, 58);
    fill_ = Box(track_, 0, 6, ui::kAccent, 3);
    lv_obj_set_pos(fill_, 0, 0);

    // ---- card
    card_ = Box(root_, kCardW, kCardH, kCard, kCardRadius);
    lv_obj_set_pos(card_, kCardX, kCardY);
    lv_obj_set_style_border_width(card_, 1, 0);
    lv_obj_set_style_border_color(card_, Hex(kLine), 0);

    // front: headword centred, IPA with a small "sound" indicator below it
    front_ = Plain(card_, kCardW - 2, kCardH - 2);
    word_f_ = Text(front_, F64(), ui::kText, 880, 0, LV_TEXT_ALIGN_CENTER);
    ipa_row_ = Plain(front_, 0, 0);
    lv_obj_set_size(ipa_row_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ipa_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ipa_row_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ipa_row_, 14, 0);
    ipa_f_ = Text(ipa_row_, F28(), ui::kAccent);
    lv_obj_t* wave = Plain(ipa_row_, 0, 24);
    lv_obj_set_size(wave, LV_SIZE_CONTENT, 24);
    lv_obj_set_flex_flow(wave, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wave, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wave, 5, 0);
    for (int h : {10, 20, 14}) Box(wave, 5, h, ui::kAccent, 3);

    // back: headword + IPA on the left, meaning and example on the right
    back_ = Plain(card_, kCardW - 2, kCardH - 2);
    word_b_ = Text(back_, F64(), ui::kText, 300, 0, LV_TEXT_ALIGN_CENTER);
    ipa_b_ = Text(back_, F28(), ui::kAccent, 300, 0, LV_TEXT_ALIGN_CENTER);
    lv_obj_t* divider = Box(back_, 1, 164, kLine, 0);
    lv_obj_set_pos(divider, 340, 28);
    meaning_ = Text(back_, F28(), ui::kText, 512, 114);
    lv_obj_set_pos(meaning_, 372, 26);
    ex_bar_ = Box(back_, 3, 60, ui::kAccent, 2);
    lv_obj_set_pos(ex_bar_, 372, 152);
    ex_en_ = Text(back_, F22(), ui::kText, 496, 30);
    lv_obj_set_pos(ex_en_, 388, 152);
    ex_vi_ = Text(back_, F22(), ui::kMuted, 496, 30);
    lv_obj_set_pos(ex_vi_, 388, 182);

    // menu rows
    list_ = Plain(card_, kCardW - 2, kCardH - 2);
    for (int i = 0; i < 3; ++i) {
        row_[i] = Box(list_, 854, 52, kAccentBg, 16, LV_OPA_TRANSP);
        lv_obj_set_pos(row_[i], 28, 17 + 68 * i);
        row_label_[i] = Text(row_[i], F28(), kRowText);
        lv_obj_align(row_label_[i], LV_ALIGN_LEFT_MID, 22, 0);
        row_arrow_[i] = Text(row_[i], F28(), 0xFFFFFF);
        lv_label_set_text(row_arrow_[i], ">");
        lv_obj_align(row_arrow_[i], LV_ALIGN_RIGHT_MID, -22, 0);
    }

    // notice / busy
    notice_ = Plain(card_, kCardW - 2, kCardH - 2);
    badge_ = Box(notice_, 76, 76, ui::kGood, LV_RADIUS_CIRCLE, kTint);
    lv_obj_align(badge_, LV_ALIGN_TOP_MID, 0, 22);
    badge_label_ = Text(badge_, F44(), ui::kGood);
    lv_obj_center(badge_label_);
    title_ = Text(notice_, F28(), ui::kText, 880, 0, LV_TEXT_ALIGN_CENTER);
    body_ = Text(notice_, F22(), ui::kMuted, 686, 64, LV_TEXT_ALIGN_CENTER);
    bar_track_ = Box(notice_, 480, 8, kTrack, 4);
    lv_obj_align(bar_track_, LV_ALIGN_TOP_MID, 0, 150);
    bar_fill_ = Box(bar_track_, 0, 8, ui::kAccent, 4);
    lv_obj_set_pos(bar_fill_, 0, 0);

    // toast: sits inside the card, below the headword
    toast_ = Pill(card_, 34, ui::kGood, kTint, 18, 18, 0);
    lv_obj_align(toast_, LV_ALIGN_TOP_MID, 0, 176);
    toast_label_ = Text(toast_, F22(), ui::kGood);
    lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);

    // gesture legend along the bottom
    legend_ = Plain(root_, kW, 40);
    lv_obj_set_flex_flow(legend_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(legend_, 14, 0);
    lv_obj_set_pos(legend_, 0, 316);

    ShowGroup(nullptr);
    return true;
}

void StudyView::Destroy() {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if (toast_timer_) {
        lv_timer_delete(toast_timer_);
        toast_timer_ = nullptr;
    }
    lv_obj_delete(root_);
    *this = StudyView();
}

void StudyView::ShowGroup(lv_obj_t* group) {
    for (lv_obj_t* g : {front_, back_, list_, notice_}) SetShown(g, g == group);
}

void StudyView::SetHeader(const HeaderInfo& info) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    lv_label_set_text(tag_label_, info.tag);
    lv_obj_set_style_text_color(tag_label_, Hex(info.tag_color), 0);
    lv_obj_set_style_bg_color(tag_, Hex(info.tag_color), 0);
    SetShown(counters_, info.show_counters);
    const std::string labels[3] = {"Học lại " + std::to_string(info.learning), "Ôn " + std::to_string(info.due),
                                   "Mới " + std::to_string(info.fresh)};
    for (int i = 0; i < 3; ++i) lv_label_set_text(counter_label_[i], labels[i].c_str());
    const int pct = info.progress_pct < 0 ? 0 : info.progress_pct > 100 ? 100 : info.progress_pct;
    lv_obj_set_width(fill_, kCardW * pct / 100);
    lv_obj_set_style_bg_color(fill_, Hex(pct >= 100 ? ui::kGood : ui::kAccent), 0);
}

void StudyView::SetHints(const std::vector<Hint>& hints) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    lv_obj_clean(legend_);
    for (const Hint& h : hints) {
        const uint32_t glyph_color = h.color ? h.color : ui::kText;
        lv_obj_t* chip = Pill(legend_, 40, kChip, LV_OPA_COVER, 16, 18, 12);
        lv_obj_t* glyph = Plain(chip, 0, 12);
        lv_obj_set_size(glyph, LV_SIZE_CONTENT, 12);
        lv_obj_set_flex_flow(glyph, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(glyph, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(glyph, 5, 0);
        switch (h.glyph) {
            case Glyph::kClick: Dot(glyph, 12, glyph_color); break;
            case Glyph::kDouble: Dot(glyph, 12, glyph_color); Dot(glyph, 12, glyph_color); break;
            case Glyph::kHold: Box(glyph, 30, 12, glyph_color, 6); break;
        }
        lv_obj_t* label = Text(chip, F22(), h.color ? h.color : ui::kMuted);
        lv_label_set_text(label, h.label);
    }
}

void StudyView::ShowFront(const Card& card) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    ShowGroup(front_);
    const int px = WordFontSize(Utf8Length(card.word), false);
    const int word_y = px == 64 ? 34 : px == 44 ? 46 : 60;
    const int ipa_y = px == 64 ? 128 : px == 44 ? 116 : 106;
    lv_obj_set_style_text_font(word_f_, WordFont(px), 0);
    lv_label_set_text(word_f_, card.word.c_str());
    lv_obj_align(word_f_, LV_ALIGN_TOP_MID, 0, word_y);
    lv_label_set_text(ipa_f_, card.ipa.c_str());
    SetShown(ipa_row_, !card.ipa.empty());
    lv_obj_align(ipa_row_, LV_ALIGN_TOP_MID, 0, ipa_y);
}

void StudyView::ShowBack(const Card& card) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    ShowGroup(back_);
    const int px = WordFontSize(Utf8Length(card.word), true);
    const int word_y = px == 64 ? 36 : px == 44 ? 50 : 62;
    const int ipa_y = px == 64 ? 126 : px == 44 ? 116 : 106;
    lv_obj_set_style_text_font(word_b_, WordFont(px), 0);
    lv_label_set_text(word_b_, card.word.c_str());
    lv_obj_set_pos(word_b_, 28, word_y);
    lv_label_set_text(ipa_b_, card.ipa.c_str());
    lv_obj_set_pos(ipa_b_, 28, ipa_y);
    lv_label_set_text(meaning_, ShortMeaning(card.meaning, 105).c_str());

    std::string en, vi;
    SplitExample(card.example, en, vi);
    SetShown(ex_bar_, !en.empty());
    SetShown(ex_en_, !en.empty());
    SetShown(ex_vi_, !vi.empty());
    lv_label_set_text(ex_en_, en.c_str());
    lv_label_set_text(ex_vi_, vi.c_str());
    lv_obj_set_height(ex_bar_, vi.empty() ? 30 : 60);
}

void StudyView::ShowMenu(const char* const* items, int count, int selected) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    ShowGroup(list_);
    for (int i = 0; i < 3; ++i) {
        SetShown(row_[i], i < count);
        if (i >= count) continue;
        const bool on = i == selected;
        lv_label_set_text(row_label_[i], items[i]);
        lv_obj_set_style_text_color(row_label_[i], Hex(on ? 0xFFFFFF : kRowText), 0);
        lv_obj_set_style_bg_opa(row_[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        SetShown(row_arrow_[i], on);
    }
}

void StudyView::ShowNotice(Icon icon, const std::string& title, const std::string& body) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    ShowGroup(notice_);
    const uint32_t color = icon == Icon::kOk ? ui::kGood : ui::kLearn;
    SetShown(badge_, true);
    lv_obj_set_style_bg_color(badge_, Hex(color), 0);
    lv_obj_set_style_text_color(badge_label_, Hex(color), 0);
    lv_label_set_text(badge_label_, icon == Icon::kOk ? "OK" : "!");
    lv_obj_center(badge_label_);
    lv_label_set_text(title_, title.c_str());
    lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 114);
    lv_label_set_text(body_, body.c_str());
    lv_obj_align(body_, LV_ALIGN_TOP_MID, 0, 158);
    SetShown(bar_track_, false);
}

void StudyView::ShowBusy(const std::string& title, const std::string& status, int percent) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    ShowGroup(notice_);
    SetShown(badge_, false);
    lv_label_set_text(title_, title.c_str());
    lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_align(body_, LV_ALIGN_TOP_MID, 0, 96);
    lv_label_set_text(body_, status.c_str());
    SetShown(bar_track_, percent >= 0);
    if (percent >= 0) lv_obj_set_width(bar_fill_, 480 * (percent > 100 ? 100 : percent) / 100);
}

void StudyView::SetBusy(const std::string& status, int percent) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    lv_label_set_text(body_, status.c_str());
    SetShown(bar_track_, percent >= 0);
    if (percent >= 0) lv_obj_set_width(bar_fill_, 480 * (percent > 100 ? 100 : percent) / 100);
}

void StudyView::Toast(const std::string& text, uint32_t color) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    lv_obj_set_style_bg_color(toast_, Hex(color), 0);
    lv_obj_set_style_text_color(toast_label_, Hex(color), 0);
    lv_label_set_text(toast_label_, text.c_str());
    lv_obj_align(toast_, LV_ALIGN_TOP_MID, 0, 176);
    lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    if (toast_timer_) {
        lv_timer_reset(toast_timer_);
    } else {
        toast_timer_ = lv_timer_create(OnToastTimer, 1500, this);
        lv_timer_set_repeat_count(toast_timer_, 1);
    }
}

}  // namespace offline
