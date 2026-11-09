#include "home_screen.h"

#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <miniz.h>

#include <cstdio>
#include <ctime>
#include <string>

#include "application.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "display.h"
#include "home_images.h"
#include "mascot.h"
#include "settings.h"
#include "ui_kit.h"
#if CONFIG_EL_OFFLINE_STUDY
#include "offline/study.h"
#endif

#define TAG "Home"

// Sophia's frames: RGB565 + alpha, deflated, embedded in the firmware (main/home/assets/sophia_*.z565, cut from the
// sprite sheet by scripts/gen_sophia_frames.py). Regenerate the files and rebuild to change the pictures.
#define SOPHIA_PNG(name)                                                                \
    extern const uint8_t sophia_##name##_start[] asm("_binary_sophia_" #name "_z565_start"); \
    extern const uint8_t sophia_##name##_end[] asm("_binary_sophia_" #name "_z565_end")
SOPHIA_PNG(idle);
SOPHIA_PNG(blink);
SOPHIA_PNG(talk1);
SOPHIA_PNG(talk2);
SOPHIA_PNG(listen);
SOPHIA_PNG(think);
SOPHIA_PNG(happy);
SOPHIA_PNG(wave);
SOPHIA_PNG(small);

namespace home {

namespace {

using namespace ui;

constexpr int kTileGap = 14;                       // between the two columns
constexpr int kRowGap = 10;                        // between the rows of tiles
// The selected tile is lifted by a few pixels. The area that holds the tiles starts kLiftPad above the first row, so
// the lifted tile of the first row is still inside it (anything outside is not redrawn and looked cut off).
constexpr int kLiftPad = 10;
constexpr int kGridY = 196;                        // top of the first row of tiles
constexpr int kSettingsBtnY = 828, kSettingsBtnH = 56;
constexpr int kFooterH = 44;

// Tile row geometry for the two layouts (rim = the darker edge under each tile)
constexpr int kGridW = (kW - 2 * kPad - kTileGap) / 2, kGridH = 140, kGridRim = 6;      // 3 rows of 2 tiles and one wide tile
// List layout: one full-width row per item with a small gap between rows (7 programmes + the settings row)
constexpr int kListRowH = 78, kListGap = 8, kListStep = kListRowH + kListGap;

struct TileSpec {
    const char* name;
    uint32_t icon;
    uint32_t color;         // tile (the darker end of the gradient)
    uint32_t deep;          // rim under the tile and badge text
    uint32_t light;         // the lighter end of the gradient
};

const TileSpec kTileSpecs[] = {
    {"Học từ mới", icon::kBook, kBlue, kBlueDeep, 0x60A5FA},
    {"Ôn tập", icon::kReview, kOrange, kOrangeDeep, 0xFDA55A},
    {"Từ điển", icon::kSearch, kTeal, kTealDeep, 0x5EEAD4},
    {"Trò chuyện", icon::kChat, kPurple, kPurpleDeep, 0xB69CFB},
    {"Phát âm", icon::kMic, kPink, kPinkDeep, 0xF783B9},
    {"Thành tích", icon::kTrophy, kGreen, kGreenDeep, 0x5BE08E},
    {"Phiên dịch", icon::kLanguages, 0x4F46E5, 0x3730A3, 0x818CF8},      // Vietnamese <-> English with the AI
};

const char* Greeting() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year < 2024 - 1900) return "Chào bé!";
    if (t.tm_hour < 11) return "Chào buổi sáng!";
    if (t.tm_hour < 18) return "Chào buổi chiều!";
    return "Chào buổi tối!";
}

bool WifiConnected() {
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

struct PngRef {
    const uint8_t* start;
    const uint8_t* end;
};
const PngRef kPngs[kFrameCount] = {
    {sophia_idle_start, sophia_idle_end},     {sophia_blink_start, sophia_blink_end},
    {sophia_talk1_start, sophia_talk1_end},   {sophia_talk2_start, sophia_talk2_end},
    {sophia_listen_start, sophia_listen_end}, {sophia_think_start, sophia_think_end},
    {sophia_happy_start, sophia_happy_end},   {sophia_wave_start, sophia_wave_end},
    {sophia_small_start, sophia_small_end},
};

// Inflates a frame once into PSRAM, ready to draw as LVGL's RGB565A8 (RGB565 plane, then an alpha plane). The ROM's
// inflate writes straight into the final buffer and needs no working memory. Falls back to the built-in owl when
// a frame cannot be inflated.
const lv_image_dsc_t* FrameImage(int frame) {
    static lv_image_dsc_t dsc[kFrameCount];
    static uint8_t state[kFrameCount];          // 0 = not tried, 1 = ready, 2 = failed
    if (frame < 0 || frame >= kFrameCount) return &home_img_owl;
    if (state[frame] == 1) return &dsc[frame];
    if (state[frame] == 2) return &home_img_owl;
    state[frame] = 2;
    const uint32_t side = frame == kSmall ? 100 : 280;
    const size_t bytes = static_cast<size_t>(side) * side * 3;
    auto* buf = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf) {
        ESP_LOGE(TAG, "no memory for frame %d", frame);
        return &home_img_owl;
    }
    // tinfl_decompress_mem_to_mem would put the 11 KB decompressor on this task's stack (the main task has 8 KB),
    // so the decompressor lives on the heap.
    auto* inflater = static_cast<tinfl_decompressor*>(heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_INTERNAL));
    if (!inflater) {
        heap_caps_free(buf);
        return &home_img_owl;
    }
    tinfl_init(inflater);
    size_t in_bytes = kPngs[frame].end - kPngs[frame].start;
    size_t out_bytes = bytes;
    const tinfl_status status = tinfl_decompress(inflater, kPngs[frame].start, &in_bytes, buf, buf, &out_bytes,
                                                 TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    heap_caps_free(inflater);
    if (status != TINFL_STATUS_DONE || out_bytes != bytes) {
        ESP_LOGE(TAG, "frame %d cannot be inflated (status %d, %u of %u bytes)", frame, static_cast<int>(status),
                 static_cast<unsigned>(out_bytes), static_cast<unsigned>(bytes));
        heap_caps_free(buf);
        return &home_img_owl;
    }
    dsc[frame].header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc[frame].header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc[frame].header.w = side;
    dsc[frame].header.h = side;
    dsc[frame].header.stride = side * 2;
    dsc[frame].data_size = bytes;
    dsc[frame].data = buf;
    state[frame] = 1;
    return &dsc[frame];
}

void PulseCb(void* obj, int32_t v) { lv_obj_set_style_border_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(v), 0); }

// Volume and brightness step through 20, 40, 60, 80, 100 and wrap around.
int NextStep(int value) {
    const int next = (value / 20 + 1) * 20;
    return next > 100 ? 20 : next;
}

}  // namespace

const lv_image_dsc_t* MascotFrame(int frame) { return FrameImage(frame); }

void OnHomeTimer(lv_timer_t* timer) { static_cast<HomeScreen*>(lv_timer_get_user_data(timer))->Tick(); }

void OnHomeAnim(lv_timer_t* timer) { static_cast<HomeScreen*>(lv_timer_get_user_data(timer))->AnimTick(); }

void OnHomeRefresh(void* arg) { static_cast<HomeScreen*>(arg)->Tick(); }

void OnHomeToastTimer(lv_timer_t* timer) {
    auto* h = static_cast<HomeScreen*>(lv_timer_get_user_data(timer));
    h->toast_timer_ = nullptr;
    SetShown(h->toast_, false);
}

HomeScreen& HomeScreen::GetInstance() {
    static HomeScreen instance;
    return instance;
}

void HomeScreen::Build() {
    if (root_) return;
    auto display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);
    const Fonts& f = GetFonts();

    {
        Settings settings("home", false);
        layout_ = settings.GetInt("layout", 0) == 1 ? Layout::kList : Layout::kGrid;
    }

    root_ = Box(lv_layer_top(), kW, kH, kBg, 0);
    lv_obj_set_pos(root_, 0, 0);

    // ---- status bar: Wi-Fi, clock, streak
    wifi_ = Label(root_, f.icon26, kInk, Utf8(icon::kWifi));
    lv_obj_set_pos(wifi_, kPad, 13);
    clock_ = Label(root_, f.xb20, kInk, "--:--");
    lv_obj_set_pos(clock_, kPad + 36, 14);

    streak_ = Box(root_, LV_SIZE_CONTENT, 36, 0xFFE3C2, 18);
    lv_obj_set_style_pad_left(streak_, 8, 0);
    lv_obj_set_style_pad_right(streak_, 12, 0);
    lv_obj_set_style_pad_column(streak_, 4, 0);
    lv_obj_set_flex_flow(streak_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(streak_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    Label(streak_, f.icon26, 0xF97316, Utf8(icon::kFlame));
    streak_label_ = Label(streak_, f.xb20, kOrangeDeep, "");
    lv_obj_align(streak_, LV_ALIGN_TOP_RIGHT, -kPad, 10);
    SetShown(streak_, false);

    // ---- Sophia and the speech bubble
    lv_obj_t* owl = lv_image_create(root_);
    lv_image_set_src(owl, FrameImage(kSmall));
    lv_obj_set_pos(owl, kPad - 8, 68);

    const int bubble_x = kPad + 96, bubble_w = kW - kPad - bubble_x;
    lv_obj_t* bubble = RaisedCard(root_, bubble_x, 62, bubble_w, 114, kWhite, kRim, 3, 22);
    lv_obj_set_style_pad_hor(bubble, 14, 0);
    lv_obj_set_style_pad_ver(bubble, 10, 0);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bubble, 2, 0);
    greeting_ = Label(bubble, f.xb22, kInk, Greeting());
    Paragraph(bubble, f.sb17, kMuted, "Hôm nay mình học gì nào?", bubble_w - 28);
    stars_ = Box(bubble, LV_SIZE_CONTENT, 22, kWhite, 0, LV_OPA_TRANSP);
    lv_obj_set_flex_flow(stars_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(stars_, 1, 0);
    for (auto& s : star_) {
        s = lv_image_create(stars_);
        lv_image_set_src(s, &home_img_star_off);
    }
    SetShown(stars_, false);

    // ---- tiles (rebuilt when the layout changes) and the settings button at the bottom
    tiles_ = Box(root_, kW, kSettingsBtnY - kGridY + kLiftPad, kWhite, 0, LV_OPA_TRANSP);
    lv_obj_set_pos(tiles_, 0, kGridY - kLiftPad);
    lv_obj_add_flag(tiles_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);      // the selected tile grows and casts a shadow outside

    settings_under_ = Box(root_, kW - 2 * kPad, kSettingsBtnH, kRim, kSettingsBtnH / 2);
    lv_obj_set_pos(settings_under_, kPad, kSettingsBtnY + 5);
    settings_btn_ = Box(root_, kW - 2 * kPad, kSettingsBtnH, kWhite, kSettingsBtnH / 2);
    lv_obj_set_pos(settings_btn_, kPad, kSettingsBtnY);
    lv_obj_set_flex_flow(settings_btn_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(settings_btn_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(settings_btn_, 12, 0);
    Label(settings_btn_, f.icon26, kInk, Utf8(icon::kSettings));
    Label(settings_btn_, f.xb22, kInk, "Cài đặt");
    BuildTiles();                           // lays out the settings button for the current layout too

    // ---- control hints
    lv_obj_t* footer = Box(root_, kW - 2 * kPad, kFooterH, kWhite, kFooterH / 2);
    lv_obj_set_pos(footer, kPad, kH - kPad - kFooterH);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    struct HintSpec { Glyph glyph; const char* text; };
    const HintSpec hints[] = {{Glyph::kClick, "Chọn"}, {Glyph::kDouble, "Lùi"}, {Glyph::kHold, "Vào"}};
    for (const auto& h : hints) {
        lv_obj_t* item = Box(footer, LV_SIZE_CONTENT, LV_SIZE_CONTENT, kWhite, 0, LV_OPA_TRANSP);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 8, 0);
        GlyphView(item, h.glyph, kInk);
        Label(item, f.sb17, kMuted, h.text);
    }

    // ---- toast (hidden)
    toast_ = Box(root_, LV_SIZE_CONTENT, 48, kInk, 24);
    lv_obj_set_style_pad_hor(toast_, 20, 0);
    toast_label_ = Label(toast_, f.sb19, kWhite, "");
    lv_obj_center(toast_label_);
    lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -(kPad + kFooterH + 16));
    SetShown(toast_, false);

    BuildSettings();
    BuildChat();
    setup_.Build();
    Focus(0);
    UpdateClock();
    SetShown(root_, false);
    SetShown(set_root_, false);
    SetShown(chat_root_, false);
    lv_timer_create(OnHomeTimer, 300, this);
    lv_timer_create(OnHomeAnim, 150, this);
    ESP_LOGI(TAG, "home screen built, layout %d", static_cast<int>(layout_));
}

// ------------------------------------------------------------------ tiles
void HomeScreen::BuildTiles() {
    const Fonts& f = GetFonts();
    lv_obj_clean(tiles_);
    const bool grid = layout_ == Layout::kGrid;
    for (int i = 0; i < kTiles; ++i) {
        const TileSpec& spec = kTileSpecs[i];
        lv_obj_t* t;
        if (grid) {
            const bool wide = i == kTiles - 1 && kTiles % 2 == 1;           // the last tile spans both columns
            const int x = wide ? kPad : kPad + (i % 2) * (kGridW + kTileGap);
            const int w = wide ? kW - 2 * kPad : kGridW;
            const int y = kLiftPad + (i / 2) * (kGridH + kGridRim + kRowGap);
            lv_obj_set_pos(Box(tiles_, w, kGridH, spec.deep, 28), x, y + kGridRim);      // edge under the tile
            t = Box(tiles_, w, kGridH, spec.color, 28);
            lv_obj_set_pos(t, x, y);
            tile_y_[i] = y;
            SetGradient(t, spec.light, spec.color, false);                    // light at the top, deep at the bottom
            if (wide) {                                                       // icon on the left, name beside it
                lv_obj_t* disc = Box(t, 76, 76, kWhite, LV_RADIUS_CIRCLE, 56);
                lv_obj_align(disc, LV_ALIGN_LEFT_MID, 28, 0);
                lv_obj_center(Label(disc, f.icon40, kWhite, Utf8(spec.icon)));
                lv_obj_align(Label(t, f.xb26, kWhite, spec.name), LV_ALIGN_LEFT_MID, 122, 0);
            } else {
                lv_obj_t* disc = Box(t, 66, 66, kWhite, LV_RADIUS_CIRCLE, 56);       // 22 % white
                lv_obj_align(disc, LV_ALIGN_CENTER, 0, -26);
                lv_obj_center(Label(disc, f.icon40, kWhite, Utf8(spec.icon)));
                lv_obj_align(Label(t, f.xb22, kWhite, spec.name), LV_ALIGN_CENTER, 0, 44);
            }
        } else {                    // full width, a gap between rows, deep colour on the left fading to light
            lv_obj_set_pos(Box(tiles_, kW, kListRowH, spec.deep, 0), 0, kLiftPad + i * kListStep + 5);   // edge under the row
            t = Box(tiles_, kW, kListRowH, spec.color, 0);
            SetGradient(t, spec.color, spec.light, true);
            lv_obj_set_pos(t, 0, kLiftPad + i * kListStep);
            tile_y_[i] = kLiftPad + i * kListStep;
            lv_obj_t* disc = Box(t, 60, 60, kWhite, LV_RADIUS_CIRCLE, 56);
            lv_obj_align(disc, LV_ALIGN_LEFT_MID, 20, 0);
            lv_obj_center(Label(disc, f.icon40, kWhite, Utf8(spec.icon)));
            lv_obj_t* name = Label(t, f.xb26, kWhite, spec.name);
            lv_obj_align(name, LV_ALIGN_LEFT_MID, 98, 0);
        }
        tile_[i] = t;

        badge_[i] = Box(t, LV_SIZE_CONTENT, 32, kWhite, 16);
        lv_obj_set_style_pad_hor(badge_[i], 10, 0);
        lv_obj_set_style_min_width(badge_[i], 32, 0);
        badge_label_[i] = Label(badge_[i], f.xb20, spec.deep, std::to_string(badge_count_[i]));
        lv_obj_center(badge_label_[i]);
        if (grid) lv_obj_align(badge_[i], LV_ALIGN_TOP_RIGHT, -10, 10);
        else lv_obj_align(badge_[i], LV_ALIGN_RIGHT_MID, -22, 0);
        SetShown(badge_[i], badge_count_[i] > 0);
    }

    // the settings button: a pill under the grid, or the last full-width row of the list
    if (settings_btn_) {
        const lv_flex_align_t main = grid ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START;
        lv_obj_set_flex_align(settings_btn_, main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(settings_btn_, grid ? 0 : 36, 0);
        if (grid) {
            settings_y_ = kSettingsBtnY;
            lv_obj_set_size(settings_btn_, kW - 2 * kPad, kSettingsBtnH);
            lv_obj_set_pos(settings_btn_, kPad, settings_y_);
            lv_obj_set_style_radius(settings_btn_, kSettingsBtnH / 2, 0);
            SetGradient(settings_btn_, kWhite, kWhite, false);
            lv_obj_set_size(settings_under_, kW - 2 * kPad, kSettingsBtnH);
            lv_obj_set_pos(settings_under_, kPad, settings_y_ + 5);
            lv_obj_set_style_radius(settings_under_, kSettingsBtnH / 2, 0);
        } else {
            settings_y_ = kGridY + kTiles * kListStep;
            lv_obj_set_size(settings_btn_, kW, kListRowH);
            lv_obj_set_pos(settings_btn_, 0, settings_y_);
            lv_obj_set_style_radius(settings_btn_, 0, 0);
            SetGradient(settings_btn_, kWhite, 0xEADFCB, true);
            lv_obj_set_size(settings_under_, kW, kListRowH);
            lv_obj_set_pos(settings_under_, 0, settings_y_ + 5);
            lv_obj_set_style_radius(settings_under_, 0, 0);
        }
    }
    Focus(focus_);
}

// ------------------------------------------------------------------- chat
void HomeScreen::BuildChat() {
    const Fonts& f = GetFonts();
    chat_root_ = Box(lv_layer_top(), kW, kH, kBg, 0);
    lv_obj_set_pos(chat_root_, 0, 0);

    // what is happening now
    chat_pill_ = Box(chat_root_, LV_SIZE_CONTENT, 46, kWhite, 23);
    lv_obj_set_style_pad_hor(chat_pill_, 20, 0);
    lv_obj_set_flex_flow(chat_pill_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chat_pill_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(chat_pill_, 10, 0);
    chat_dot_ = Box(chat_pill_, 14, 14, kFaint, LV_RADIUS_CIRCLE);
    chat_status_ = Label(chat_pill_, f.xb22, kInk, "");
    lv_obj_align(chat_pill_, LV_ALIGN_TOP_MID, 0, 16);

    // mascot in a ring that pulses while someone is talking
    chat_ring_ = Box(chat_root_, 304, 304, kWhite, LV_RADIUS_CIRCLE, 120);
    lv_obj_set_style_border_width(chat_ring_, 8, 0);
    lv_obj_set_style_border_color(chat_ring_, Hex(kFaint), 0);
    lv_obj_set_style_border_opa(chat_ring_, LV_OPA_COVER, 0);
    lv_obj_align(chat_ring_, LV_ALIGN_TOP_MID, 0, 76);
    for (int f_i = 0; f_i < kSmall; ++f_i) FrameImage(f_i);         // decode every frame now, not in the middle of a chat
    chat_mascot_ = lv_image_create(chat_ring_);
    lv_image_set_src(chat_mascot_, FrameImage(kIdle));
    lv_obj_center(chat_mascot_);
    ESP_LOGI(TAG, "Sophia frames ready, free PSRAM %u KB",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    lv_anim_t pulse;
    lv_anim_init(&pulse);
    lv_anim_set_var(&pulse, chat_ring_);
    lv_anim_set_exec_cb(&pulse, PulseCb);
    lv_anim_set_values(&pulse, 70, 255);
    lv_anim_set_duration(&pulse, 700);
    lv_anim_set_playback_duration(&pulse, 700);
    lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&pulse);

    // the child's words, then Sophia's answer
    const int tw = kW - 2 * kPad;
    chat_user_box_ = Box(chat_root_, tw - 40, 92, kPurple, 24);
    lv_obj_set_pos(chat_user_box_, kW - kPad - (tw - 40), 396);
    chat_user_ = Label(chat_user_box_, f.sb22, kWhite, "");
    lv_obj_set_size(chat_user_, tw - 40 - 32, 76);
    lv_label_set_long_mode(chat_user_, LV_LABEL_LONG_DOT);
    lv_obj_align(chat_user_, LV_ALIGN_TOP_LEFT, 16, 8);
    SetShown(chat_user_box_, false);

    lv_obj_t* bot_under = Box(chat_root_, tw, 268, kRim, 26);   // paper edge under the answer card
    lv_obj_set_pos(bot_under, kPad, 500 + 5);
    chat_bot_box_ = Box(chat_root_, tw, 268, kWhite, 26);
    lv_obj_set_pos(chat_bot_box_, kPad, 500);
    chat_bot_ = Label(chat_bot_box_, f.sb22, kInk, "");
    lv_obj_set_size(chat_bot_, tw - 32, 268 - 24);
    lv_label_set_long_mode(chat_bot_, LV_LABEL_LONG_DOT);
    lv_obj_align(chat_bot_, LV_ALIGN_TOP_LEFT, 16, 12);

    // the exit button and the control legend
    lv_obj_set_pos(Box(chat_root_, tw, 60, 0x991B1B, 30), kPad, 800 + 5);        // darker edge under the button
    lv_obj_t* exit_btn = Box(chat_root_, tw, 60, kRed, 30);
    lv_obj_set_pos(exit_btn, kPad, 800);
    SetRing(exit_btn, true, 4);
    lv_obj_set_flex_flow(exit_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(exit_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(exit_btn, 12, 0);
    Label(exit_btn, f.icon26, kWhite, Utf8(icon::kClose));
    Label(exit_btn, f.xb26, kWhite, "Thoát trò chuyện");

    lv_obj_t* footer = Box(chat_root_, tw, kFooterH, kWhite, kFooterH / 2);
    lv_obj_set_pos(footer, kPad, kH - kPad - kFooterH);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    struct HintSpec { Glyph glyph; const char* text; };
    const HintSpec hints[] = {{Glyph::kClick, "Nói / dừng"}, {Glyph::kDouble, "Thoát"}, {Glyph::kHold, "Thoát"}};
    for (const auto& h : hints) {
        lv_obj_t* item = Box(footer, LV_SIZE_CONTENT, LV_SIZE_CONTENT, kWhite, 0, LV_OPA_TRANSP);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 8, 0);
        GlyphView(item, h.glyph, kInk);
        Label(item, f.sb17, kMuted, h.text);
    }
}

// state: a DeviceState value
void HomeScreen::UpdateChat(int state) {
    chat_state_ = state;
    const char* text = "Sẵn sàng";
    uint32_t color = kFaint;
    switch (state) {
        case kDeviceStateConnecting: text = "Đang kết nối..."; color = kAmber; break;
        case kDeviceStateListening: text = chat_translate_ ? "Hãy nói, mình sẽ dịch" : "Sophia đang nghe bạn"; color = kGreen; break;
        case kDeviceStateSpeaking:
        case kDeviceStateNotifying: text = chat_translate_ ? "Đang đọc bản dịch" : "Sophia đang nói"; color = kPurple; break;
        default: break;
    }
    lv_label_set_text(chat_status_, text);
    lv_obj_set_style_bg_color(chat_dot_, Hex(color), 0);
    lv_obj_set_style_border_color(chat_ring_, Hex(color), 0);
    lv_obj_set_style_border_opa(chat_ring_, LV_OPA_COVER, 0);
    lv_obj_align(chat_pill_, LV_ALIGN_TOP_MID, 0, 16);
    // while nothing has been said yet, tell the child what to do
    if (chat_bot_text_.empty()) {
        const char* hint = state == kDeviceStateListening
                               ? (chat_translate_ ? "Nói tiếng Việt, mình dịch sang tiếng Anh. Nói tiếng Anh, mình dịch sang tiếng Việt."
                                                  : "Hãy nói gì đó với Sophia nhé!")
                               : "Sophia đang chuẩn bị...";
        lv_label_set_text(chat_bot_, hint);
        lv_obj_set_style_text_color(chat_bot_, Hex(kFaint), 0);
    }
}

void HomeScreen::SetChatText(const char* role, const char* content) {
    if (!chat_root_ || !role || !content) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    const std::string text = content;
    if (text.empty()) return;
    if (std::string(role) == "user") {
        chat_user_text_ = text;
        chat_bot_text_.clear();                         // a new turn starts
        lv_label_set_text(chat_user_, text.c_str());
        SetShown(chat_user_box_, true);
        UpdateChat(chat_state_);
    } else if (std::string(role) == "assistant") {
        if (!chat_bot_text_.empty()) chat_bot_text_ += " ";
        chat_bot_text_ += text;
        while (chat_bot_text_.size() > 330) {          // keep the latest sentences; the panel holds about 9 lines
            const size_t cut = chat_bot_text_.find(". ");
            if (cut == std::string::npos) break;
            chat_bot_text_.erase(0, cut + 2);
        }
        lv_label_set_text(chat_bot_, chat_bot_text_.c_str());
        lv_obj_set_style_text_color(chat_bot_, Hex(kInk), 0);
    }
}

void HomeScreen::ClearChatText() {
    if (!chat_root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    chat_user_text_.clear();
    chat_bot_text_.clear();
    SetShown(chat_user_box_, false);
    UpdateChat(chat_state_);
}

// ---------------------------------------------------------------- settings
void HomeScreen::BuildSettings() {
    const Fonts& f = GetFonts();
    set_root_ = Box(lv_layer_top(), kW, kH, kBg, 0);
    lv_obj_set_pos(set_root_, 0, 0);

    lv_obj_t* back = Label(set_root_, f.icon26, kInk, Utf8(icon::kBack));
    lv_obj_set_pos(back, kPad, 24);
    lv_obj_t* title = Label(set_root_, f.xb32, kInk, "Cài đặt");
    lv_obj_set_pos(title, kPad + 40, 8);

    struct RowSpec { uint32_t icon; const char* name; uint32_t color, deep; };
    const RowSpec specs[kSettingRows] = {
        {icon::kGrid, "Giao diện", kBlue, kBlueDeep},
        {icon::kVolume, "Âm lượng", kOrange, kOrangeDeep},
        {icon::kSun, "Độ sáng", kAmber, 0xB45309},
        {icon::kSync, "Từ vựng", kTeal, kTealDeep},
        {icon::kBack, "Quay lại", kPurple, kPurpleDeep},
    };
    const int rowH = 112, step = 132, y0 = 90, w = kW - 2 * kPad;
    for (int i = 0; i < kSettingRows; ++i) {
        lv_obj_t* r = RaisedCard(set_root_, kPad, y0 + i * step, w, rowH, kWhite, kRim, 5, 26);
        set_row_[i] = r;
        lv_obj_t* disc = Box(r, 64, 64, specs[i].color, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(disc, 16, 16);
        set_icon_[i] = Label(disc, f.icon40, kWhite, Utf8(specs[i].icon));
        lv_obj_center(set_icon_[i]);
        lv_obj_t* name = Label(r, f.xb26, kInk, specs[i].name);
        lv_obj_set_pos(name, 96, 4);
        set_value_[i] = Label(r, f.sb22, kMuted, "");
        lv_obj_set_pos(set_value_[i], 96, 44);
        if (i == 1 || i == 2) {
            lv_obj_t* track = Box(r, w - 32, 14, kRim, 7);
            lv_obj_set_pos(track, 16, 88);
            set_bar_[i] = Box(track, 0, 14, specs[i].color, 7);
        }
    }

    lv_obj_t* footer = Box(set_root_, kW - 2 * kPad, kFooterH, kWhite, kFooterH / 2);
    lv_obj_set_pos(footer, kPad, kH - kPad - kFooterH);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    struct HintSpec { Glyph glyph; const char* text; };
    const HintSpec hints[] = {{Glyph::kClick, "Chọn"}, {Glyph::kDouble, "Lùi"}, {Glyph::kHold, "Đổi"}};
    for (const auto& h : hints) {
        lv_obj_t* item = Box(footer, LV_SIZE_CONTENT, LV_SIZE_CONTENT, kWhite, 0, LV_OPA_TRANSP);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 8, 0);
        GlyphView(item, h.glyph, kInk);
        Label(item, f.sb17, kMuted, h.text);
    }
    UpdateSettings();
}

void HomeScreen::UpdateSettings() {
    auto board = &Board::GetInstance();
    const int volume = board->GetAudioCodec()->output_volume();
    const int brightness = board->GetBacklight()->user_brightness();
    const bool grid = layout_ == Layout::kGrid;
    lv_label_set_text(set_icon_[0], Utf8(grid ? icon::kGrid : icon::kList).c_str());
    lv_label_set_text(set_value_[0], grid ? "Lưới: 2 cột, 3 hàng" : "Danh sách: mỗi mục 1 hàng");
    lv_label_set_text(set_value_[1], (std::to_string(volume) + "%").c_str());
    lv_label_set_text(set_value_[2], (std::to_string(brightness) + "%").c_str());
    lv_label_set_text(set_value_[3], "Tải bộ từ mới qua Wi-Fi");
    lv_label_set_text(set_value_[4], "Về màn hình chính");
    const int w = kW - 2 * kPad - 32;
    lv_obj_set_width(set_bar_[1], w * (volume > 100 ? 100 : volume) / 100);
    lv_obj_set_width(set_bar_[2], w * (brightness > 100 ? 100 : brightness) / 100);
    for (int i = 0; i < kSettingRows; ++i) lv_obj_set_y(set_row_[i], 90 + i * 132 - (i == set_focus_ ? 7 : 0));
}

void HomeScreen::ChangeSetting(int row) {
    auto board = &Board::GetInstance();
    switch (row) {
        case 0: {
            layout_ = layout_ == Layout::kGrid ? Layout::kList : Layout::kGrid;
            Settings settings("home", true);
            settings.SetInt("layout", static_cast<int>(layout_));
            BuildTiles();
            break;
        }
        case 1: {
            const int v = NextStep(board->GetAudioCodec()->output_volume());
            board->GetAudioCodec()->SetOutputVolume(v);
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);     // let the child hear the new level
            break;
        }
        case 2: {
            int v = NextStep(board->GetBacklight()->user_brightness());
            // Applied now, saved when the learner leaves the settings screen: a flash write pauses the cache and
            // makes the panel show noise, so it is not done on every press.
            board->GetBacklight()->SetBrightness(static_cast<uint8_t>(v), false);
            break;
        }
        case 3:
#if CONFIG_EL_OFFLINE_STUDY
            offline::OfflineStudy::GetInstance().Start(offline::Mode::kSync);
            RequestRefresh();
#else
            Toast("Chưa hỗ trợ");
#endif
            break;
        default:
            OpenSettings(false);
            break;
    }
    UpdateSettings();
}

void HomeScreen::OpenSettings(bool open) {
    if (settings_open_ && !open) {          // leaving: save the brightness chosen while the screen was open
        Settings display("display", true);
        display.SetInt("brightness", Board::GetInstance().GetBacklight()->user_brightness());
    }
    settings_open_ = open;
    if (open) {
        set_focus_ = 0;
        UpdateSettings();
    }
    RequestRefresh();
}

// ---------------------------------------------------------------- state
HomeScreen::Mode HomeScreen::Target() const {
    if (visible_) return settings_open_ ? Mode::kSettings : Mode::kHome;
    if (chat_shown_) return Mode::kChat;
    return setup_target_;                       // the first-run pages (none once the device is running)
}

void HomeScreen::ShowTarget() {
    const Mode t = Target();
    SetShown(root_, t == Mode::kHome);
    SetShown(set_root_, t == Mode::kSettings);
    SetShown(chat_root_, t == Mode::kChat);
    setup_.ShowPage(t == Mode::kSplash ? SetupScreen::Page::kSplash
                    : t == Mode::kWifi ? SetupScreen::Page::kWifi
                    : t == Mode::kCode ? SetupScreen::Page::kCode
                                       : SetupScreen::Page::kNone);
    shown_ = t;
}

// Sophia's small movements on the chat screen: she waves while connecting, tilts her head to listen, looks up while
// she thinks, moves her mouth while she speaks, and blinks now and then when she is calm.
void HomeScreen::AnimTick() {
    if (shown_ == Mode::kSplash || shown_ == Mode::kWifi || shown_ == Mode::kCode) {
        setup_.Tick(esp_timer_get_time());
        return;
    }
    if (!chat_mascot_ || shown_ != Mode::kChat) {
        anim_frame_ = -1;
        return;
    }
    const int64_t now = esp_timer_get_time();
    const auto state = Application::GetInstance().GetDeviceState();
    int frame = kIdle;
    if (state == kDeviceStateSpeaking || state == kDeviceStateNotifying) {
        static const int kMouth[] = {kTalk1, kTalk2, kTalk2, kTalk1, kIdle, kTalk2, kTalk1, kTalk2};
        frame = kMouth[anim_step_++ % 8];
    } else if (state == kDeviceStateConnecting) {
        frame = kWave;
    } else if (state == kDeviceStateListening) {
        const bool waiting_for_answer = !chat_user_text_.empty() && chat_bot_text_.empty();
        frame = waiting_for_answer ? kThink : kListen;
    }
    if (frame == kIdle || frame == kListen) {           // calm: blink every 3 to 5 seconds
        if (now >= next_blink_us_) {
            blink_until_us_ = now + 200 * 1000;
            next_blink_us_ = now + 3000 * 1000 + static_cast<int64_t>(esp_random() % 2000000);
        }
        if (now < blink_until_us_) frame = kBlink;
    }
    if (frame != anim_frame_) {
        anim_frame_ = frame;
        lv_image_set_src(chat_mascot_, FrameImage(frame));
    }
}

void HomeScreen::RequestRefresh() { lv_async_call(OnHomeRefresh, this); }     // the display lock must be held

void HomeScreen::Apply() {
    if (Target() == shown_) return;
    ui::SwitchPage([this] { ShowTarget(); });
}

void HomeScreen::UpdateClock() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year >= 2024 - 1900 && t.tm_min != last_minute_) {
        last_minute_ = t.tm_min;
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        lv_label_set_text(clock_, buf);
    }
    const int wifi = WifiConnected() ? 1 : 0;
    if (wifi != last_wifi_) {
        last_wifi_ = wifi;
        lv_label_set_text(wifi_, Utf8(wifi ? icon::kWifi : icon::kWifiOff).c_str());
    }
}

void HomeScreen::UpdateState() {
    auto state = Application::GetInstance().GetDeviceState();
    bool offline = false;
#if CONFIG_EL_OFFLINE_STUDY
    offline = offline::OfflineStudy::GetInstance().Active();
#endif
    offline_ = offline;
    switch (state) {
        case kDeviceStateConnecting:
        case kDeviceStateListening:
        case kDeviceStateSpeaking:
        case kDeviceStateNotifying:
            chat_ = true;           // a tile, the wake word or the server started a conversation
            idle_since_us_ = 0;
            break;
        case kDeviceStateIdle:
            if (chat_) {
                const int64_t now = esp_timer_get_time();
                if (idle_since_us_ == 0) idle_since_us_ = now;
                if (now - idle_since_us_ > 8 * 1000 * 1000) {
                    chat_ = false;
                    chat_translate_ = false;
                    idle_since_us_ = 0;
                }
            }
            break;
        default:
            break;
    }
    // first-run pages: start-up, Wi-Fi setup, waiting for activation
    setup_target_ = Mode::kNone;
    const char* splash = nullptr;
    if (!offline) {
        switch (state) {
            case kDeviceStateWifiConfiguring: setup_target_ = Mode::kWifi; break;
            case kDeviceStateActivating:
                setup_target_ = activation_code_.empty() ? Mode::kSplash : Mode::kCode;
                splash = "Đang kết nối máy chủ...";
                break;
            case kDeviceStateStarting: setup_target_ = Mode::kSplash; splash = "Đang khởi động..."; break;
            case kDeviceStateUpgrading: setup_target_ = Mode::kSplash; splash = "Đang nâng cấp, vui lòng chờ..."; break;
            default: break;
        }
    }
    if (state != kDeviceStateActivating) activation_code_.clear();
    if (splash && splash_text_ != splash) {
        splash_text_ = splash;
        setup_.SetSplashText(splash_text_);
    }
    const bool show = state == kDeviceStateIdle && !chat_ && !offline;
    const bool chat_show = chat_ && !offline && !show;
    if (chat_show && (chat_state_ != static_cast<int>(state) || !chat_shown_)) UpdateChat(static_cast<int>(state));
    chat_shown_ = chat_show;
    if (show != visible_) {
        visible_ = show;
        if (show) {
            lv_label_set_text(greeting_, Greeting());
            UpdateClock();
            UpdateSettings();
        }
    } else if (show) {
        UpdateClock();
    }
}

void HomeScreen::Tick() {
    UpdateState();
    if (offline_) return;       // a study screen owns the display and switches pages itself
    Apply();
}

void HomeScreen::RefreshNow() {
    if (!root_) return;
    UpdateState();
    ShowTarget();
}

// Selected item: lifted off the page. The item moves up a few pixels, which uncovers the darker edge that sits under
// it; it is also moved in front of its neighbours. (Shadows and scaling were tried and
// overflowed a task stack of the software renderer.)
void HomeScreen::Mark(lv_obj_t* o, int base_y, bool on) {
    if (!o) return;
    lv_obj_set_y(o, base_y - (on ? 6 : 0));
    if (on && o != settings_btn_) lv_obj_move_foreground(o);
}

void HomeScreen::Focus(int index) {
    focus_ = (index + kItems) % kItems;
    for (int i = 0; i < kTiles; ++i) Mark(tile_[i], tile_y_[i], i == focus_);
    Mark(settings_btn_, settings_y_, focus_ == kTiles);
}

void HomeScreen::Toast(const char* text) {
    lv_label_set_text(toast_label_, text);
    lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -(kPad + kFooterH + 16));
    SetShown(toast_, true);
    if (toast_timer_) lv_timer_delete(toast_timer_);
    toast_timer_ = lv_timer_create(OnHomeToastTimer, 2500, this);
    lv_timer_set_repeat_count(toast_timer_, 1);
}

void HomeScreen::Open(int index) {
    if (index == kTiles) {
        OpenSettings(true);
        return;
    }
    ESP_LOGI(TAG, "open %s", kTileSpecs[index].name);
    switch (index) {
#if CONFIG_EL_OFFLINE_STUDY
        case 0: offline::OfflineStudy::GetInstance().Start(offline::Mode::kNew); break;
        case 1: offline::OfflineStudy::GetInstance().Start(offline::Mode::kReview); break;
        case 2: offline::OfflineStudy::GetInstance().Start(offline::Mode::kDict); break;
        case 5: offline::OfflineStudy::GetInstance().Start(offline::Mode::kStats); break;
#endif
        case 3:     // talk with the tutor on the server
        case 4:     // pronunciation practice with the tutor
        case 6:     // interpreter: the AI translates every sentence, Vietnamese <-> English
            chat_ = true;
            chat_translate_ = index == 6;
            idle_since_us_ = 0;
            RequestRefresh();       // starts the transition to the chat screen (in the LVGL task)
            Application::GetInstance().InvokeText(index == 3   ? "Sophia"
                                                  : index == 6 ? "Chế độ phiên dịch."
                                                               : "Mình muốn luyện phát âm.");
            return;
        default:
            Toast("Sắp có!");
            return;
    }
    RequestRefresh();           // the study task is starting
}

// ---------------------------------------------------------------- gestures
bool HomeScreen::OnClick() {
    if (!root_) return false;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if (shown_ == Mode::kCode && !activation_code_.empty()) {     // one click reads the code again
        Application::GetInstance().ReadActivationCode(activation_code_);
        return true;
    }
    if (visible_) {
        if (settings_open_) {
            set_focus_ = (set_focus_ + 1) % kSettingRows;
            UpdateSettings();
        } else {
            Focus(focus_ + 1);
        }
        return true;
    }
    if (chat_) {
        Application::GetInstance().ToggleChatState();   // start / stop talking
        return true;
    }
    return false;
}

bool HomeScreen::OnDoubleClick() {
    if (!root_) return false;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if (visible_) {
        if (settings_open_) {
            set_focus_ = (set_focus_ + kSettingRows - 1) % kSettingRows;
            UpdateSettings();
        } else {
            Focus(focus_ - 1);
        }
        return true;
    }
    if (chat_) {
        chat_ = false;          // back to the home screen as soon as the device is idle
        chat_translate_ = false;
        idle_since_us_ = 0;
        Application::GetInstance().EndConversation();
        return true;
    }
    return false;
}

bool HomeScreen::OnLongPress() {
    if (!root_) return false;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if (!visible_) {
        if (!chat_) return false;
        chat_ = false;          // hold = leave the conversation, like a double click
        chat_translate_ = false;
        idle_since_us_ = 0;
        Application::GetInstance().EndConversation();
        return true;
    }
    if (settings_open_) ChangeSetting(set_focus_);
    else Open(focus_);
    return true;
}

// ------------------------------------------------------------ first-run screens
void HomeScreen::ShowWifiSetup(const std::string& ssid, const std::string& url) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    setup_.SetWifi(ssid, url);
}

void HomeScreen::ShowActivation(const std::string& code) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    if (code != activation_code_) {
        activation_code_ = code;
        setup_.SetCode(code);
    }
    setup_.StartReading(esp_timer_get_time());
    RequestRefresh();                           // switches from the splash to the code page
}

// ------------------------------------------------------------ learning data
void HomeScreen::SetGoal(int done, int total) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    const bool shown = total > 0;
    SetShown(stars_, shown);
    for (int i = 0; i < 10; ++i) {
        SetShown(star_[i], shown && i < total);
        lv_image_set_src(star_[i], i < done ? &home_img_star_on : &home_img_star_off);
    }
}

void HomeScreen::SetBadge(int tile, int count) {
    if (!root_ || tile < 0 || tile >= kTiles) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    badge_count_[tile] = count;
    SetShown(badge_[tile], count > 0);
    lv_label_set_text(badge_label_[tile], std::to_string(count).c_str());
}

void HomeScreen::SetStreak(int days) {
    if (!root_) return;
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    SetShown(streak_, days > 0);
    lv_label_set_text(streak_label_, (std::to_string(days) + " ngày").c_str());
}

}  // namespace home
