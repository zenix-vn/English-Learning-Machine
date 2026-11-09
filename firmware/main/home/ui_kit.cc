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
    LcdPageBegin();                             // from here on LVGL draws into the hidden frame buffer
    change();
    lv_obj_invalidate(lv_screen_active());      // the whole screen is redrawn, so the hidden buffer is complete
    const int64_t t0 = esp_timer_get_time();
    lv_refr_now(nullptr);
    ESP_LOGI(TAG, "page prepared in %d ms", static_cast<int>((esp_timer_get_time() - t0) / 1000));
    auto* backlight = Board::GetInstance().GetBacklight();
    backlight->FadeTo(0, 50);
    vTaskDelay(pdMS_TO_TICKS(60));
    if (while_dark) while_dark();
    LcdPageFlip();
    vTaskDelay(pdMS_TO_TICKS(25));              // let the first frame of the new page go out before the light returns
    backlight->FadeTo(backlight->user_brightness(), 110);
}

void SetRing(lv_obj_t* o, bool on, int width) {
    // Selection is shown by lifting the item (see HomeScreen::Mark) or by its background colour, not by a border.
    (void)on;
    (void)width;
    lv_obj_set_style_outline_width(o, 0, 0);
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
