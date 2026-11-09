#include "setup_screen.h"

#include <esp_log.h>
#include <esp_random.h>
#include <sdkconfig.h>

#include <algorithm>

#include "mascot.h"
#include "ui_kit.h"

#define TAG "Setup"

namespace home {

namespace {

using namespace ui;

constexpr int kCodeY = 394, kCodeH = 84;

// "https://e-learning.zenix.vn/ota/" -> "e-learning.zenix.vn/admin": where the learner enters the activation code.
std::string AdminAddress() {
    std::string url = CONFIG_OTA_URL;
    if (const size_t p = url.find("://"); p != std::string::npos) url.erase(0, p + 3);
    if (const size_t slash = url.find('/'); slash != std::string::npos) url.erase(slash);
    return url + "/admin";
}

// Pill with a round dot and a label, centered horizontally. Returns the label.
lv_obj_t* StatusPill(lv_obj_t* parent, int y, uint32_t dot_color, const char* text) {
    const Fonts& f = GetFonts();
    lv_obj_t* pill = Box(parent, LV_SIZE_CONTENT, 46, kWhite, 23);
    lv_obj_set_style_pad_hor(pill, 20, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pill, 12, 0);
    Box(pill, 14, 14, dot_color, LV_RADIUS_CIRCLE);
    lv_obj_t* label = Label(pill, f.xb22, kInk, text);
    lv_obj_set_width(label, 266);                       // fixed, so the pill does not jump while the dots change
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

// The button legend at the bottom, the same as on every other page.
void HintStrip(lv_obj_t* parent, Glyph glyph, const char* text) {
    const Hint hint{glyph, text};
    FillHintBar(HintBar(parent), &hint, 1);
}

// A numbered step card. `value` (optional) is the thing to look for, shown big in a tinted pill. Returns the value label.
lv_obj_t* StepCard(lv_obj_t* parent, int y, int number, uint32_t light, uint32_t deep, uint32_t tint,
                   const char* instruction, const char* value) {
    const Fonts& f = GetFonts();
    lv_obj_t* card = RaisedCard(parent, kPad, y, kW - 2 * kPad, 112, kWhite, kRim, 4, 24);
    lv_obj_t* chip = Box(card, 56, 56, deep, LV_RADIUS_CIRCLE);
    SetGradient(chip, light, deep, false);
    lv_obj_set_pos(chip, 16, 28);
    lv_obj_center(Label(chip, f.xb26, kWhite, std::to_string(number)));
    const int tw = kW - 2 * kPad - 88 - 12;
    if (!value) {
        lv_obj_t* text = Paragraph(card, f.xb22, kInk, instruction, tw);
        lv_obj_set_pos(text, 88, 22);
        return nullptr;
    }
    lv_obj_t* text = Paragraph(card, f.sb19, kMuted, instruction, tw);
    lv_obj_set_pos(text, 88, 10);
    lv_obj_t* pill = Box(card, LV_SIZE_CONTENT, 44, tint, 22);
    lv_obj_set_style_pad_hor(pill, 18, 0);
    lv_obj_set_style_max_width(pill, tw, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_pos(pill, 88, 56);
    return Label(pill, f.xb26, deep, value);
}

lv_obj_t* Group(lv_obj_t* root) {
    lv_obj_t* g = Box(root, kW, kH, kWhite, 0, LV_OPA_TRANSP);
    lv_obj_set_pos(g, 0, 0);
    SetShown(g, false);
    return g;
}

const char* Dots(int step) {
    static const char* const kDots[] = {"", ".", "..", "..."};
    return kDots[step % 4];
}

}  // namespace

void SetupScreen::Build() {
    if (root_) return;
    const Fonts& f = GetFonts();
    root_ = Box(lv_layer_top(), kW, kH, kBg, 0);
    lv_obj_set_pos(root_, 0, 0);
    SetShown(root_, false);

    // Sophia on a soft round backdrop, shared by all pages
    backdrop_ = Box(root_, 268, 268, kWhite, LV_RADIUS_CIRCLE, 150);
    lv_obj_align(backdrop_, LV_ALIGN_TOP_MID, 0, 14);
    mascot_ = lv_image_create(root_);
    lv_image_set_src(mascot_, MascotFrame(kIdle));
    lv_obj_align(mascot_, LV_ALIGN_TOP_MID, 0, 8);

    // ---- splash
    lv_obj_t* g = group_[1] = Group(root_);
    lv_obj_t* name = Label(g, f.xb44, kInk, "Sophia");
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 310);
    splash_status_ = Paragraph(g, f.xb22, kMuted, "Đang khởi động...", kW - 2 * kPad, true);
    lv_obj_align(splash_status_, LV_ALIGN_TOP_MID, 0, 376);
    for (int i = 0; i < 3; ++i) {
        splash_dots_[i] = Box(g, 16, 16, kPurple, LV_RADIUS_CIRCLE);
        lv_obj_align(splash_dots_[i], LV_ALIGN_TOP_MID, (i - 1) * 32, 450);
    }

    // ---- firmware download: same plain look as the vocabulary download (badge, title, percentage, bar)
    upgrade_group_ = Group(root_);
    lv_obj_t* badge = Box(upgrade_group_, 128, 128, kBlue, LV_RADIUS_CIRCLE);
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 216);
    lv_obj_center(Label(badge, f.icon64, kWhite, Utf8(icon::kSync)));
    lv_obj_align(Paragraph(upgrade_group_, f.xb32, kInk, "Đang nâng cấp", kW - 2 * kPad, true), LV_ALIGN_TOP_MID, 0, 380);
    lv_obj_align(Paragraph(upgrade_group_, f.sb22, kMuted, "Vui lòng đợi, đừng tắt máy.", kW - 2 * kPad, true),
                 LV_ALIGN_TOP_MID, 0, 436);
    upgrade_pct_ = Label(upgrade_group_, f.xb44, kBlue, "0%");
    lv_obj_set_width(upgrade_pct_, 160);
    lv_obj_set_style_text_align(upgrade_pct_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(upgrade_pct_, LV_ALIGN_TOP_MID, 0, 520);
    lv_obj_t* track = Box(upgrade_group_, 280, 16, kRim, 8);
    lv_obj_align(track, LV_ALIGN_TOP_MID, 0, 596);
    upgrade_fill_ = Box(track, 0, 16, kBlue, 8);

    // ---- Wi-Fi setup: three steps
    g = group_[2] = Group(root_);
    lv_obj_align(Paragraph(g, f.xb32, kInk, "Cài Wi-Fi cho Sophia", kW - 2 * kPad, true), LV_ALIGN_TOP_MID, 0, 296);
    lv_obj_align(Paragraph(g, f.sb19, kMuted, "Làm theo 3 bước trên điện thoại của bạn", kW - 2 * kPad, true),
                 LV_ALIGN_TOP_MID, 0, 350);
    wifi_ssid_ = StepCard(g, 392, 1, 0x60A5FA, kBlue, 0xE3EBFD, "Bật Wi-Fi điện thoại, chọn mạng", "Sophia");
    wifi_url_ = StepCard(g, 516, 2, 0x5EEAD4, kTeal, 0xDDF3EF, "Mở trình duyệt, vào địa chỉ", "192.168.4.1");
    StepCard(g, 640, 3, 0xB69CFB, kPurple, 0xEDE6FC, "Chọn Wi-Fi nhà bạn, nhập mật khẩu rồi bấm lưu", nullptr);
    wifi_status_ = StatusPill(g, 776, kGreen, "Đang chờ bạn kết nối");
    HintStrip(g, Glyph::kDouble, "Bấm 2 lần: bỏ qua, học offline");

    // ---- activation code
    g = group_[3] = Group(root_);
    lv_obj_align(Paragraph(g, f.xb32, kInk, "Kích hoạt Sophia", kW - 2 * kPad, true), LV_ALIGN_TOP_MID, 0, 296);
    lv_obj_align(Paragraph(g, f.sb19, kMuted, "Nhập mã này vào trang quản trị", kW - 2 * kPad, true),
                 LV_ALIGN_TOP_MID, 0, 346);
    for (int i = 0; i < kMaxDigits; ++i) {
        digit_box_[i] = Box(g, 50, kCodeH, kPurple, 18);
        SetGradient(digit_box_[i], 0xA78BFA, 0x6D28D9, false);
        digit_label_[i] = Label(digit_box_[i], f.xb44, kWhite, "0");
        lv_obj_center(digit_label_[i]);
        SetShown(digit_box_[i], false);
    }
    lv_obj_t* url_card = RaisedCard(g, kPad, 506, kW - 2 * kPad, 96, kWhite, kRim, 4, 24);
    lv_obj_t* url_title = Label(url_card, f.sb17, kMuted, "Trang quản trị");
    lv_obj_align(url_title, LV_ALIGN_TOP_LEFT, 22, 10);
    admin_url_ = Label(url_card, f.xb26, kBlue, AdminAddress());
    lv_obj_align(admin_url_, LV_ALIGN_TOP_LEFT, 22, 44);
    lv_obj_t* steps = RaisedCard(g, kPad, 622, kW - 2 * kPad, 124, kWhite, kRim, 4, 24);
    const char* const kSteps[3] = {"Mở trang quản trị", "Vào mục Thiết bị", "Nhập mã, bấm Kích hoạt"};
    const uint32_t kChip[3] = {kBlue, kTeal, kPurple};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* chip = Box(steps, 30, 30, kChip[i], LV_RADIUS_CIRCLE);
        lv_obj_set_pos(chip, 18, 12 + i * 36);
        lv_obj_center(Label(chip, f.xb20, kWhite, std::to_string(i + 1)));
        lv_obj_t* text = Label(steps, f.sb19, kInk, kSteps[i]);
        lv_obj_set_pos(text, 62, 10 + i * 36);
    }
    code_status_ = StatusPill(g, 776, kPurple, "Đang chờ kích hoạt");
    HintStrip(g, Glyph::kClick, "Bấm: nghe lại mã");
}

void SetupScreen::ApplyVisibility() {
    const bool up = upgrade_percent_ >= 0 && page_ == Page::kSplash;     // the light download page replaces the splash
    SetShown(backdrop_, !up);
    SetShown(mascot_, !up);
    for (int i = 1; i <= 3; ++i) SetShown(group_[i], static_cast<int>(page_) == i && !(up && i == 1));
    SetShown(upgrade_group_, up);
}

void SetupScreen::ShowPage(Page page) {
    if (!root_) return;
    page_ = page;
    SetShown(root_, page != Page::kNone);
    ApplyVisibility();
    mascot_frame_ = -1;
    step_ = 0;
    next_blink_us_ = 0;
}

void SetupScreen::SetSplashText(const std::string& status) {
    if (splash_status_) lv_label_set_text(splash_status_, status.c_str());
}

void SetupScreen::SetUpgrade(int percent) {
    if (!upgrade_pct_) return;
    if (percent < 0) {                                  // leaving: the splash comes back
        if (upgrade_percent_ < 0) return;
        upgrade_percent_ = -1;
        ApplyVisibility();
        return;
    }
    if (percent > 100) percent = 100;
    if (percent == upgrade_percent_) return;            // nothing changed: nothing is drawn
    const bool entering = upgrade_percent_ < 0;
    upgrade_percent_ = percent;
    lv_label_set_text_fmt(upgrade_pct_, "%d%%", percent);
    lv_obj_set_width(upgrade_fill_, 280 * percent / 100);
    if (entering) ApplyVisibility();
}

void SetupScreen::SetWifi(const std::string& ssid, const std::string& url) {
    if (!wifi_ssid_) return;
    lv_label_set_text(wifi_ssid_, ssid.c_str());
    std::string address = url;
    if (const size_t p = address.find("://"); p != std::string::npos) address.erase(0, p + 3);
    while (!address.empty() && address.back() == '/') address.pop_back();
    lv_label_set_text(wifi_url_, address.c_str());
}

void SetupScreen::SetCode(const std::string& code) {
    if (!root_) return;
    digits_ = static_cast<int>(std::min<size_t>(code.size(), kMaxDigits));
    const int gap = 8;
    const int w = digits_ > 0 ? std::min(50, (kW - 2 * kPad - (digits_ - 1) * gap) / digits_) : 50;
    const int x0 = (kW - (digits_ * w + (digits_ - 1) * gap)) / 2;
    for (int i = 0; i < kMaxDigits; ++i) {
        const bool used = i < digits_;
        SetShown(digit_box_[i], used);
        if (!used) continue;
        lv_obj_set_size(digit_box_[i], w, kCodeH);
        lv_obj_set_pos(digit_box_[i], x0 + i * (w + gap), kCodeY);
        const char digit[2] = {code[i], 0};
        lv_label_set_text(digit_label_[i], digit);
        lv_obj_center(digit_label_[i]);
    }
    lit_ = -1;
}

void SetupScreen::StartReading(int64_t now_us) {
    read_start_us_ = now_us;
    SetDigitLit(-1);
}

void SetupScreen::SetDigitLit(int index) {
    if (index == lit_) return;
    lit_ = index;
    for (int i = 0; i < digits_; ++i) {
        const bool on = i == index;
        SetGradient(digit_box_[i], on ? 0xFDE047 : 0xA78BFA, on ? 0xF59E0B : 0x6D28D9, false);
        lv_obj_set_style_text_color(digit_label_[i], Hex(on ? kInk : kWhite), 0);
        lv_obj_set_y(digit_box_[i], on ? kCodeY - 8 : kCodeY);
    }
}

void SetupScreen::Tick(int64_t now_us) {
    if (!root_ || page_ == Page::kNone) return;
    if (upgrade_percent_ >= 0) return;               // downloading firmware: keep the page still
    ++step_;

    // is the voice reading the code?
    bool reading = false;
    if (page_ == Page::kCode && read_start_us_ > 0) {
        const int64_t elapsed_ms = (now_us - read_start_us_) / 1000;
        const int64_t total_ms = kPromptMs + static_cast<int64_t>(digits_) * kDigitMs + 300;
        reading = elapsed_ms >= 0 && elapsed_ms < total_ms;
        int lit = -1;
        if (elapsed_ms >= kPromptMs) {
            const int64_t index = (elapsed_ms - kPromptMs) / kDigitMs;
            if (index < digits_) lit = static_cast<int>(index);
        }
        SetDigitLit(lit);
    }

    // Sophia
    int frame = kIdle;
    if (page_ == Page::kWifi) {
        frame = (step_ / 8) % 2 == 0 ? kWave : kIdle;                // waves, rests, waves...
    } else if (page_ == Page::kCode && reading) {
        static const int kMouth[] = {kTalk1, kTalk2, kTalk2, kTalk1, kIdle, kTalk2, kTalk1, kTalk2};
        frame = kMouth[step_ % 8];
    }
    if (frame == kIdle) {                                            // calm: blink every 3 to 5 seconds
        if (now_us >= next_blink_us_) {
            blink_until_us_ = now_us + 200 * 1000;
            next_blink_us_ = now_us + 3000 * 1000 + static_cast<int64_t>(esp_random() % 2000000);
        }
        if (now_us < blink_until_us_) frame = kBlink;
    }
    if (frame != mascot_frame_) {
        mascot_frame_ = frame;
        lv_image_set_src(mascot_, MascotFrame(frame));
    }

    // dots: the three splash dots chase each other, the status lines add one dot at a time
    if (step_ % 3 == 0) {
        if (page_ == Page::kSplash) {
            for (int i = 0; i < 3; ++i) {
                lv_obj_set_style_bg_opa(splash_dots_[i], (step_ / 3) % 3 == i ? LV_OPA_COVER : LV_OPA_30, 0);
            }
        } else if (page_ == Page::kWifi) {
            lv_label_set_text(wifi_status_, (std::string("Đang chờ bạn kết nối") + Dots(step_ / 3)).c_str());
        } else if (page_ == Page::kCode) {
            lv_label_set_text(code_status_, (std::string(reading ? "Sophia đang đọc mã" : "Đang chờ kích hoạt") +
                                             Dots(step_ / 3)).c_str());
        }
    }
}

}  // namespace home
