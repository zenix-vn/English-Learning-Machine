#include "ui_kit.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "backlight.h"
#include "board.h"
#include "display.h"
#include "display/lcd_display.h"

#include "offline/fonts/fonts.h"

#define TAG "UiKit"

// TTF subsets embedded with EMBED_FILES and rasterised at run time by LVGL's Tiny TTF engine.
extern const uint8_t baloo_xb_start[] asm("_binary_baloo2_extrabold_ttf_start");
extern const uint8_t baloo_xb_end[] asm("_binary_baloo2_extrabold_ttf_end");
extern const uint8_t baloo_sb_start[] asm("_binary_baloo2_semibold_ttf_start");
extern const uint8_t baloo_sb_end[] asm("_binary_baloo2_semibold_ttf_end");
extern const uint8_t lucide_start[] asm("_binary_lucide_home_ttf_start");
extern const uint8_t lucide_end[] asm("_binary_lucide_home_ttf_end");

namespace ui {

namespace {

const lv_font_t* MakeFont(const uint8_t* start, const uint8_t* end, int px) {
    lv_font_t* f = lv_tiny_ttf_create_data(start, end - start, px);
    if (!f) {
        ESP_LOGE(TAG, "font %d px failed", px);
        return LV_FONT_DEFAULT;
    }
    f->fallback = LV_FONT_DEFAULT;
    return f;
}

}  // namespace

const Fonts& GetFonts() {
    static Fonts fonts = [] {
        Fonts f{};
        f.xb56 = MakeFont(baloo_xb_start, baloo_xb_end, 56);
        f.xb44 = MakeFont(baloo_xb_start, baloo_xb_end, 44);
        f.xb32 = MakeFont(baloo_xb_start, baloo_xb_end, 32);
        f.xb26 = MakeFont(baloo_xb_start, baloo_xb_end, 26);
        f.xb22 = MakeFont(baloo_xb_start, baloo_xb_end, 22);
        f.xb20 = MakeFont(baloo_xb_start, baloo_xb_end, 20);
        f.sb22 = MakeFont(baloo_sb_start, baloo_sb_end, 22);
        f.sb19 = MakeFont(baloo_sb_start, baloo_sb_end, 19);
        f.sb17 = MakeFont(baloo_sb_start, baloo_sb_end, 17);
        f.icon64 = MakeFont(lucide_start, lucide_end, 64);
        f.icon40 = MakeFont(lucide_start, lucide_end, 40);
        f.icon26 = MakeFont(lucide_start, lucide_end, 26);
#if EL_HAVE_FONT_VI22
        f.ipa = &font_vi_22;        // Baloo has no IPA letters; Noto Sans does
#else
        f.ipa = LV_FONT_DEFAULT;
#endif
        return f;
    }();
    return fonts;
}

lv_color_t Hex(uint32_t rgb) { return lv_color_hex(rgb); }

void SetShown(lv_obj_t* o, bool shown) {
    if (!o) return;
    if (shown) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

std::string Utf8(uint32_t cp) {
    char b[4];
    if (cp < 0x80) return std::string(1, static_cast<char>(cp));
    if (cp < 0x800) {
        b[0] = 0xC0 | (cp >> 6);
        b[1] = 0x80 | (cp & 0x3F);
        return std::string(b, 2);
    }
    b[0] = 0xE0 | (cp >> 12);
    b[1] = 0x80 | ((cp >> 6) & 0x3F);
    b[2] = 0x80 | (cp & 0x3F);
    return std::string(b, 3);
}

lv_obj_t* Box(lv_obj_t* parent, int w, int h, uint32_t color, int radius, lv_opa_t opa) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, Hex(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t* Label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const std::string& text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, Hex(color), 0);
    lv_label_set_text(l, text.c_str());
    return l;
}

lv_obj_t* Paragraph(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const std::string& text, int width,
                    bool center) {
    lv_obj_t* l = Label(parent, font, color, text);
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    if (center) lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

lv_obj_t* RaisedCard(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, uint32_t rim, int rim_px,
                     int radius) {
    lv_obj_t* under = Box(parent, w, h, rim, radius);
    lv_obj_set_pos(under, x, y + rim_px);
    lv_obj_t* card = Box(parent, w, h, color, radius);
    lv_obj_set_pos(card, x, y);
    return card;
}

void SetGradient(lv_obj_t* o, uint32_t from, uint32_t to, bool horizontal) {
    lv_obj_set_style_bg_color(o, Hex(from), 0);
    lv_obj_set_style_bg_grad_color(o, Hex(to), 0);
    lv_obj_set_style_bg_grad_dir(o, horizontal ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(o, 0, 0);
    lv_obj_set_style_bg_grad_stop(o, 255, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

namespace {
struct LvglJob {
    const std::function<void()>* fn;
    SemaphoreHandle_t done;
};
void RunLvglJob(void* arg) {
    auto* job = static_cast<LvglJob*>(arg);
    (*job->fn)();
    xSemaphoreGive(job->done);
}
}  // namespace

void RunInLvglTask(const std::function<void()>& fn) {
    LvglJob job{&fn, xSemaphoreCreateBinary()};
    {
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        lv_async_call(RunLvglJob, &job);        // runs in the LVGL task at its next timer pass
    }
    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
}

void SwitchPage(const std::function<void()>& change, const std::function<void()>& while_dark) {
    // One fixed rhythm for every page change: dim, swap while dark, brighten. Keep these equal everywhere so all
    // transitions feel alike; they are short because the new page is already fully drawn before the screen dims.
    constexpr uint32_t kFadeOutMs = 40;
    constexpr uint32_t kFadeInMs = 90;
    constexpr uint32_t kFrameMs = 20;           // one display frame, so the first frame of the new page is out
    LcdPageBegin();                             // from here on LVGL draws into the hidden frame buffer
    change();
    lv_obj_invalidate(lv_screen_active());      // the whole screen is redrawn, so the hidden buffer is complete
    const int64_t t0 = esp_timer_get_time();
    lv_refr_now(nullptr);
    ESP_LOGI(TAG, "page prepared in %d ms", static_cast<int>((esp_timer_get_time() - t0) / 1000));
    auto* backlight = Board::GetInstance().GetBacklight();
    backlight->FadeTo(0, kFadeOutMs);
    vTaskDelay(pdMS_TO_TICKS(kFadeOutMs + 5));
    if (while_dark) while_dark();
    LcdPageFlip();
    vTaskDelay(pdMS_TO_TICKS(kFrameMs));
    backlight->FadeTo(backlight->user_brightness(), kFadeInMs);
}

uint32_t Mix(uint32_t a, uint32_t b, int pct_b) {
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const int ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;
        out |= static_cast<uint32_t>(ca + (cb - ca) * pct_b / 100) << shift;
    }
    return out;
}

lv_obj_t* HintBar(lv_obj_t* parent) {
    lv_obj_t* bar = Box(parent, kW - 2 * kPad, kHintBarH, kWhite, 0, LV_OPA_TRANSP);
    lv_obj_set_pos(bar, kPad, kHintBarY);
    return bar;
}

void FillHintBar(lv_obj_t* bar, const Hint* hints, int n, lv_obj_t** labels) {
    lv_obj_clean(bar);
    if (n <= 0) return;
    if (n > 3) n = 3;
    const Fonts& f = GetFonts();
    constexpr int kGap = 8, kTotal = kW - 2 * kPad;
    int units = 0;
    for (int i = 0; i < n; ++i) units += hints[i].strong ? 2 : 1;
    const int unit = (kTotal - kGap * (n - 1)) / units;
    int x = 0;
    for (int i = 0; i < n; ++i) {
        const Hint& h = hints[i];
        const int w = i == n - 1 ? kTotal - x : unit * (h.strong ? 2 : 1);
        uint32_t bg = kWhite, fg = kInk, text = kMuted;
        lv_opa_t opa = LV_OPA_COVER;
        if (h.strong) {
            bg = h.color ? h.color : kInk;
            fg = text = kWhite;
        } else if (h.color) {
            bg = fg = text = h.color;
            opa = 36;
        }
        lv_obj_t* seg = Box(bar, w, kHintBarH, bg, 20, opa);
        lv_obj_set_pos(seg, x, 0);
        lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(seg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(seg, 4, 0);
        GlyphView(seg, h.glyph, fg);
        lv_obj_t* label = Label(seg, f.xb20, text, h.label);
        if (labels) labels[i] = label;
        // a long label in a narrow entry: the smaller face first, then "..." rather than spilling over the edge
        lv_obj_update_layout(label);
        if (lv_obj_get_width(label) > w - 12) {
            lv_obj_set_style_text_font(label, f.sb17, 0);
            lv_obj_update_layout(label);
            if (lv_obj_get_width(label) > w - 12) {
                lv_obj_set_size(label, w - 12, 26);
                lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
                lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            }
        }
        x += w + kGap;
    }
}

lv_obj_t* GlyphView(lv_obj_t* parent, Glyph glyph, uint32_t color) {
    lv_obj_t* row = Box(parent, LV_SIZE_CONTENT, 12, kWhite, 0, LV_OPA_TRANSP);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    switch (glyph) {
        case Glyph::kClick: Box(row, 11, 11, color, LV_RADIUS_CIRCLE); break;
        case Glyph::kDouble:
            Box(row, 11, 11, color, LV_RADIUS_CIRCLE);
            Box(row, 11, 11, color, LV_RADIUS_CIRCLE);
            break;
        case Glyph::kHold: Box(row, 26, 11, color, 6); break;
    }
    return row;
}

}  // namespace ui
