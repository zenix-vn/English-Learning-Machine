#include "study_view.h"

#include "board.h"
#include "display.h"
#include "text_util.h"

namespace offline {

namespace {

using namespace ui;

constexpr int kCardX = kPad, kCardY = 96, kCardW = kW - 2 * kPad, kCardH = 750, kCardRadius = 28;
constexpr int kInner = kCardW - 32;                              // text width inside the card
constexpr int kHintsY = kHintBarY;                               // the shared legend (ui::HintBar)
// Selected row: ink with white text, one quiet tone instead of an outline or a bright colour.
constexpr uint32_t kRowTint = 0xF3F5F9, kRowFocus = kInk, kOnFocus = kWhite, kSubOnFocus = 0xC9CCDD;

// Light tint of a colour, for pills.
constexpr lv_opa_t kTint = 40;

void Center(lv_obj_t* o, int y) { lv_obj_align(o, LV_ALIGN_TOP_MID, 0, y); }

// Label that clips with "..." at width x height.
lv_obj_t* Clipped(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const std::string& text, int w, int h) {
    lv_obj_t* l = Label(parent, font, color, text);
    lv_obj_set_size(l, w, h);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

// Transparent container the size of the card.
lv_obj_t* Group(lv_obj_t* card) {
    lv_obj_t* g = Box(card, kCardW, kCardH, kWhite, 0, LV_OPA_TRANSP);
    lv_obj_set_pos(g, 0, 0);
    SetShown(g, false);
    return g;
}

}  // namespace

void OnStudyToastTimer(lv_timer_t* timer) {
    auto* view = static_cast<StudyView*>(lv_timer_get_user_data(timer));
    view->toast_timer_ = nullptr;
    view->HideToast();
}

void StudyView::HideToast() { SetShown(toast_, false); }

bool StudyView::Build() {
    if (root_) return true;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    const Fonts& f = GetFonts();

    root_ = Box(lv_layer_top(), kW, kH, kBg, 0);
    lv_obj_set_pos(root_, 0, 0);

    // ---- header: tag pill, remaining count, progress bar
    tag_ = Box(root_, LV_SIZE_CONTENT, 40, tone::kExtra, 20, kTint);
    lv_obj_set_style_pad_hor(tag_, 18, 0);
    lv_obj_set_pos(tag_, kPad, 14);
    tag_label_ = Label(tag_, f.xb20, tone::kExtra, "");
    lv_obj_center(tag_label_);
    right_ = Label(root_, f.sb19, kMuted, "");
    lv_obj_align(right_, LV_ALIGN_TOP_RIGHT, -kPad, 22);
    track_ = Box(root_, kCardW, 10, kRim, 5);
    lv_obj_set_pos(track_, kPad, 70);
    fill_ = Box(track_, 0, 10, tone::kNew, 5);

    // ---- card
    card_ = RaisedCard(root_, kCardX, kCardY, kCardW, kCardH, kWhite, kRim, 6, kCardRadius);

    // front: big word, pronunciation, caption
    front_ = Group(card_);
    word_f_ = Label(front_, f.xb56, kInk, "");
    ipa_f_ = Label(front_, f.ipa, kMuted, "");
    caption_ = Paragraph(front_, f.sb22, kFaint, "", kInner, true);
    Center(caption_, 640);

    // back: word, pronunciation, meaning, example
    back_ = Group(card_);
    word_b_ = Label(back_, f.xb44, kInk, "");
    lv_obj_set_pos(word_b_, 20, 14);
    ipa_b_ = Label(back_, f.ipa, kMuted, "");
    lv_obj_set_pos(ipa_b_, 22, 84);
    lv_obj_t* rule = Box(back_, kInner, 3, 0xEFE6D6, 2);
    lv_obj_set_pos(rule, 16, 132);
    lv_obj_t* head1 = Label(back_, f.xb20, kFaint, "NGHĨA");
    lv_obj_set_pos(head1, 20, 144);
    meaning_ = Clipped(back_, f.xb32, kInk, "", kInner, 230);
    lv_obj_set_pos(meaning_, 20, 182);
    ex_head_ = Label(back_, f.xb20, kFaint, "VÍ DỤ");
    lv_obj_set_pos(ex_head_, 20, 440);
    ex_en_ = Clipped(back_, f.sb22, kInk, "", kInner, 132);
    lv_obj_set_pos(ex_en_, 20, 478);
    ex_vi_ = Clipped(back_, f.sb19, kMuted, "", kInner, 112);
    lv_obj_set_pos(ex_vi_, 20, 626);

    // menu: three big rows
    menu_ = Group(card_);
    for (int i = 0; i < 3; ++i) {
        menu_row_[i] = Box(menu_, kInner, 120, kRowTint, 24);
        lv_obj_set_pos(menu_row_[i], 16, 40 + i * 150);
        menu_label_[i] = Label(menu_row_[i], f.xb26, kInk, "");
        lv_obj_center(menu_label_[i]);
    }

    // notice and busy share one group
    notice_ = Group(card_);
    badge_ = Box(notice_, 128, 128, tone::kGood, LV_RADIUS_CIRCLE);
    Center(badge_, 120);
    badge_icon_ = Label(badge_, f.icon64, kWhite, Utf8(icon::kCheck));
    lv_obj_center(badge_icon_);
    title_ = Paragraph(notice_, f.xb32, kInk, "", kInner, true);
    Center(title_, 290);
    body_ = Paragraph(notice_, f.sb22, kMuted, "", kInner, true);
    Center(body_, 420);
    pct_label_ = Label(notice_, f.xb44, tone::kNew, "0%");        // the percentage, redrawn only when it changes
    Center(pct_label_, 520);
    SetShown(pct_label_, false);
    bar_track_ = Box(notice_, kInner - 40, 16, kRim, 8);
    Center(bar_track_, 610);
    bar_fill_ = Box(bar_track_, 0, 16, tone::kNew, 8);
    SetShown(bar_track_, false);

    // dictionary: letter grid (4 x 7) and word list
    letters_ = Group(card_);
    const int cw = 70, ch = 88, gap = 10;
    const int gx = (kCardW - (4 * cw + 3 * gap)) / 2;
    for (int i = 0; i < 27; ++i) {
        const int x = gx + (i % 4) * (cw + gap), y = 14 + (i / 4) * (ch + 8);
        cell_[i] = Box(letters_, cw, ch, kRowTint, 20);
        lv_obj_set_pos(cell_[i], x, y);
        if (i < 26) {
            const char letter[2] = {static_cast<char>('A' + i), 0};
            cell_label_[i] = Label(cell_[i], f.xb32, kInk, letter);
        } else {
            cell_label_[i] = Label(cell_[i], f.icon40, tone::kAgain, Utf8(icon::kBack));
        }
        lv_obj_center(cell_label_[i]);
    }
    list_ = Group(card_);
    for (int i = 0; i < kListRows; ++i) {
        row_[i] = Box(list_, kInner, 76, kRowTint, 20);
        lv_obj_set_pos(row_[i], 16, 12 + i * 80);
        row_title_[i] = Label(row_[i], f.xb26, kInk, "");
        lv_obj_set_pos(row_title_[i], 16, 0);
        row_sub_[i] = Clipped(row_[i], f.sb17, kMuted, "", kInner - 32, 26);
        lv_obj_set_pos(row_sub_[i], 16, 42);
    }

    // ---- hints at the bottom
    hints_ = HintBar(root_);

    // ---- toast
    toast_ = Box(root_, LV_SIZE_CONTENT, 52, kInk, 26);
    lv_obj_set_style_pad_hor(toast_, 22, 0);
    toast_label_ = Label(toast_, f.xb22, kWhite, "");
    lv_obj_center(toast_label_);
    lv_obj_align(toast_, LV_ALIGN_TOP_MID, 0, kHintsY - 76);
    SetShown(toast_, false);

    return true;
}

void StudyView::Destroy() {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if (toast_timer_) { lv_timer_delete(toast_timer_); toast_timer_ = nullptr; }
    lv_obj_delete(root_);
    root_ = tag_ = tag_label_ = right_ = track_ = fill_ = card_ = hints_ = toast_ = toast_label_ = nullptr;
    front_ = word_f_ = ipa_f_ = caption_ = back_ = word_b_ = ipa_b_ = meaning_ = ex_en_ = ex_vi_ = ex_head_ = nullptr;
    menu_ = notice_ = badge_ = badge_icon_ = title_ = body_ = bar_track_ = bar_fill_ = pct_label_ = letters_ = list_ = nullptr;
    for (auto& o : menu_row_) o = nullptr;
    for (auto& o : menu_label_) o = nullptr;
    for (auto& o : cell_) o = nullptr;
    for (auto& o : cell_label_) o = nullptr;
    for (auto& o : row_) o = nullptr;
    for (auto& o : row_title_) o = nullptr;
    for (auto& o : row_sub_) o = nullptr;
}

void StudyView::ShowGroup(lv_obj_t* group) {
    lv_obj_t* all[] = {front_, back_, menu_, notice_, letters_, list_};
    for (lv_obj_t* g : all) SetShown(g, g == group);
}

void StudyView::SetHeader(const HeaderInfo& info) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    SetShown(tag_, info.tag[0] != 0);
    lv_label_set_text(tag_label_, info.tag);
    lv_obj_set_style_text_color(tag_label_, Hex(info.tag_color), 0);
    lv_obj_set_style_bg_color(tag_, Hex(info.tag_color), 0);
    lv_label_set_text(right_, info.right.c_str());
    lv_obj_align(right_, LV_ALIGN_TOP_RIGHT, -kPad, 22);
    SetShown(track_, info.progress_pct >= 0);
    if (info.progress_pct >= 0) {
        lv_obj_set_width(fill_, kCardW * (info.progress_pct > 100 ? 100 : info.progress_pct) / 100);
        lv_obj_set_style_bg_color(fill_, Hex(info.tag_color), 0);
    }
}

void StudyView::SetHints(const std::vector<Hint>& hints) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    FillHintBar(hints_, hints.data(), static_cast<int>(hints.size()));
}

void StudyView::ShowFront(const Card& card, const std::string& caption) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    const Fonts& f = GetFonts();
    const size_t n = card.word.size();
    lv_obj_set_style_text_font(word_f_, n <= 8 ? f.xb56 : n <= 12 ? f.xb44 : f.xb32, 0);
    lv_label_set_text(word_f_, card.word.c_str());
    Center(word_f_, 250);
    lv_label_set_text(ipa_f_, card.ipa.c_str());
    Center(ipa_f_, 350);
    lv_label_set_text(caption_, caption.c_str());
    Center(caption_, 640);
    ShowGroup(front_);
}

void StudyView::ShowBack(const Card& card) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    const Fonts& f = GetFonts();
    const size_t n = card.word.size();
    lv_obj_set_style_text_font(word_b_, n <= 10 ? f.xb44 : f.xb32, 0);
    lv_label_set_text(word_b_, card.word.c_str());
    lv_label_set_text(ipa_b_, card.ipa.c_str());
    lv_label_set_text(meaning_, ShortMeaning(card.meaning, 110).c_str());
    std::string en, vi;
    SplitExample(card.example, en, vi);
    SetShown(ex_head_, !en.empty());
    lv_label_set_text(ex_en_, en.c_str());
    lv_label_set_text(ex_vi_, vi.c_str());
    ShowGroup(back_);
}

void StudyView::ShowMenu(const char* const* items, int count, int selected) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    for (int i = 0; i < 3; ++i) {
        SetShown(menu_row_[i], i < count);
        if (i >= count) continue;
        lv_label_set_text(menu_label_[i], items[i]);
        lv_obj_set_style_bg_color(menu_row_[i], Hex(i == selected ? kRowFocus : kRowTint), 0);
        lv_obj_set_style_text_color(menu_label_[i], Hex(i == selected ? kOnFocus : kInk), 0);
    }
    ShowGroup(menu_);
}

void StudyView::ShowNotice(Icon icon, const std::string& title, const std::string& body) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    const bool ok = icon == Icon::kOk;
    lv_obj_set_style_bg_color(badge_, Hex(ok ? tone::kGood : tone::kLearn), 0);
    lv_label_set_text(badge_icon_, Utf8(ok ? icon::kCheck : icon::kClose).c_str());
    lv_label_set_text(title_, title.c_str());
    lv_label_set_text(body_, body.c_str());
    SetShown(bar_track_, false);
    SetShown(pct_label_, false);
    busy_text_.clear();
    busy_pct_ = -2;
    ShowGroup(notice_);
}

void StudyView::ShowBusy(const std::string& title, const std::string& status, int percent) {
    if (!root_) return;
    {
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        lv_obj_set_style_bg_color(badge_, Hex(tone::kNew), 0);
        lv_label_set_text(badge_icon_, Utf8(icon::kSync).c_str());
        lv_label_set_text(title_, title.c_str());
        busy_text_ = "\x01";                  // differs from any real text: the first SetBusy draws everything
        busy_pct_ = -2;
        ShowGroup(notice_);
    }
    SetBusy(status, percent);
}

// Called for every step of a download. Only what really changed is touched, so LVGL redraws a few small areas
// (the percentage and the filled part of the bar) instead of the whole text block each time.
void StudyView::SetBusy(const std::string& status, int percent) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    // "Đang tải cards.bin 37%" -> the text without the trailing " 37%"
    std::string text = status;
    if (percent >= 0) {
        const size_t pos = text.rfind('%');
        if (pos != std::string::npos) {
            size_t begin = pos;
            while (begin > 0 && text[begin - 1] >= '0' && text[begin - 1] <= '9') --begin;
            text.erase(begin, pos - begin + 1);
            while (!text.empty() && text.back() == ' ') text.pop_back();
        }
    }
    if (text != busy_text_) {
        busy_text_ = text;
        lv_label_set_text(body_, text.c_str());
    }
    const int pct = percent < 0 ? -1 : (percent > 100 ? 100 : percent);
    if (pct != busy_pct_) {
        const bool had_bar = busy_pct_ >= 0;
        busy_pct_ = pct;
        if (pct >= 0) {
            if (!had_bar) {
                SetShown(bar_track_, true);
                SetShown(pct_label_, true);
            }
            lv_label_set_text_fmt(pct_label_, "%d%%", pct);
            lv_obj_align(pct_label_, LV_ALIGN_TOP_MID, 0, 520);
            lv_obj_set_width(bar_fill_, (kInner - 40) * pct / 100);
        } else {
            SetShown(bar_track_, false);
            SetShown(pct_label_, false);
        }
    }
}

void StudyView::Toast(const std::string& text, uint32_t color) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    lv_label_set_text(toast_label_, text.c_str());
    lv_obj_set_style_bg_color(toast_, Hex(color), 0);
    lv_obj_align(toast_, LV_ALIGN_TOP_MID, 0, kHintsY - 76);
    SetShown(toast_, true);
    lv_obj_move_foreground(toast_);
    if (toast_timer_) lv_timer_delete(toast_timer_);
    toast_timer_ = lv_timer_create(OnStudyToastTimer, 1800, this);
    lv_timer_set_repeat_count(toast_timer_, 1);
}

void StudyView::ShowLetters(const bool* has, int selected) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    for (int i = 0; i < 27; ++i) {
        const bool on = i == selected;
        const bool avail = i == 26 || has[i];
        lv_obj_set_style_bg_color(cell_[i], Hex(on ? kRowFocus : kRowTint), 0);
        lv_obj_set_style_opa(cell_[i], avail ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_set_style_text_color(cell_label_[i], Hex(on ? kOnFocus : i == 26 ? tone::kAgain : kInk), 0);
    }
    ShowGroup(letters_);
}

void StudyView::ShowList(const std::vector<ListRow>& rows, int selected) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    for (int i = 0; i < kListRows; ++i) {
        const bool used = i < static_cast<int>(rows.size());
        SetShown(row_[i], used);
        if (!used) continue;
        const ListRow& r = rows[i];
        lv_label_set_text(row_title_[i], r.title.c_str());
        lv_label_set_text(row_sub_[i], r.subtitle.c_str());
        SetShown(row_sub_[i], !r.subtitle.empty());
        const bool on = i == selected;
        lv_obj_set_style_text_color(row_title_[i], Hex(on ? kOnFocus : r.special ? tone::kAgain : kInk), 0);
        lv_obj_set_style_text_color(row_sub_[i], Hex(on ? kSubOnFocus : kMuted), 0);
        lv_obj_set_pos(row_title_[i], 16, r.subtitle.empty() ? 20 : 0);
        lv_obj_set_style_bg_color(row_[i], Hex(on ? kRowFocus : kRowTint), 0);
    }
    ShowGroup(list_);
}

}  // namespace offline
