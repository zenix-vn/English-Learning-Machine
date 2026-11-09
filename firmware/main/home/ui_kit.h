// Shared look of the child-friendly screens (home, settings, study, dictionary): colours, fonts and the
// few building blocks every screen uses. Visual reference: the "English Learner – Giao diện máy" canvas.
// All functions that touch LVGL must be called with the display lock held.
#pragma once

#include <lvgl.h>

#include <cstdint>
#include <functional>
#include <string>

namespace ui {

constexpr int kW = 376, kH = 960, kPad = 16;

// Shared layout tokens, so every page has the same rhythm.
constexpr int kHintBarH = 64;                       // the button legend at the bottom of every page
constexpr int kHintBarY = kH - kPad - kHintBarH;
// Selection, the same on every page and without outlines: the selected card is raised (moved up, its darker edge
// showing) in full colour; the others lie flat and a little faded, so they step back.
constexpr int kLift = 4;                            // the selected card moves up this much, uncovering its edge
constexpr int kFadePct = 30;                        // how far an unselected colour is mixed towards the page

// colours
constexpr uint32_t kBg = 0xFFF8EC;          // warm cream page
constexpr uint32_t kInk = 0x1F2340;         // text
constexpr uint32_t kMuted = 0x4A4F6A;       // secondary text
constexpr uint32_t kFaint = 0x8A8FA8;       // labels
constexpr uint32_t kWhite = 0xFFFFFF;
constexpr uint32_t kRim = 0xE9DCC6;         // "pressed paper" edge under white cards
constexpr uint32_t kFocus = 0xFACC15;       // yellow focus ring
constexpr uint32_t kBlue = 0x2563EB, kBlueDeep = 0x1E40AF;
constexpr uint32_t kOrange = 0xEA580C, kOrangeDeep = 0x9A3412;
constexpr uint32_t kTeal = 0x0D9488, kTealDeep = 0x115E59;
constexpr uint32_t kPurple = 0x7C3AED, kPurpleDeep = 0x5B21B6;
constexpr uint32_t kPink = 0xDB2777, kPinkDeep = 0x9D174D;
constexpr uint32_t kGreen = 0x16A34A, kGreenDeep = 0x166534;
constexpr uint32_t kRed = 0xDC2626, kAmber = 0xD97706;

// Lucide icon code points (subset embedded by scripts/gen_home_fonts.sh)
namespace icon {
constexpr uint32_t kBook = 0xE063, kReview = 0xE14C, kSearch = 0xE155, kChat = 0xE11A, kMic = 0xE11C,
                   kTrophy = 0xE377, kWifi = 0xE1AE, kWifiOff = 0xE1AF, kFlame = 0xE0D7, kSettings = 0xE158,
                   kCheck = 0xE070, kGrid = 0xE105, kList = 0xE10C, kSun = 0xE17C, kVolume = 0xE1AB,
                   kSync = 0xE149, kBack = 0xE04C, kLeft = 0xE072, kRight = 0xE073, kClose = 0xE1B2,
                   kAZ = 0xE41A, kOk = 0xE226, kLanguages = 0xE103, kGradCap = 0xE234;
}

struct Fonts {
    const lv_font_t *xb56, *xb44, *xb32, *xb26, *xb22, *xb20;     // Baloo 2 ExtraBold
    const lv_font_t *sb22, *sb19, *sb17;                          // Baloo 2 SemiBold
    const lv_font_t *icon64, *icon40, *icon26;                    // Lucide
    const lv_font_t* ipa;                                         // Noto Sans with IPA symbols (or the default font)
};

// Creates the fonts on first use.
const Fonts& GetFonts();

lv_color_t Hex(uint32_t rgb);
void SetShown(lv_obj_t* o, bool shown);
std::string Utf8(uint32_t code_point);

// Filled rounded rectangle without border, padding or scrolling.
lv_obj_t* Box(lv_obj_t* parent, int w, int h, uint32_t color, int radius, lv_opa_t opa = LV_OPA_COVER);
lv_obj_t* Label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const std::string& text);
// Wrapping label of a fixed width (centered or left aligned).
lv_obj_t* Paragraph(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const std::string& text, int width,
                    bool center = false);
// A card with a darker copy `rim_px` below it, like a paper button. Returns the top card; the rim is its sibling.
lv_obj_t* RaisedCard(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, uint32_t rim, int rim_px,
                     int radius);
// Switches to another page without a blank gap. `change` hides and shows objects (and may create or delete them);
// the new page is drawn completely into the hidden frame buffer while the old page stays on screen, then the
// screen dims for a moment, `while_dark` runs (it may write to the flash, which would otherwise show as noise), the
// frame buffers are flipped and the light fades back in. Call with the display lock held.
void SwitchPage(const std::function<void()>& change, const std::function<void()>& while_dark = nullptr);

// Runs `fn` in the LVGL task and waits until it is done. Drawing a whole page needs a deep stack (about 8 KB), which
// only the LVGL task has: tasks with a small stack (the button task, the study task) must not call SwitchPage
// themselves. The caller must NOT hold the display lock.
void RunInLvglTask(const std::function<void()>& fn);

// Two-colour background gradient: horizontal (from left to right) or vertical (from top to bottom).
void SetGradient(lv_obj_t* o, uint32_t from, uint32_t to, bool horizontal);
// `a` mixed with `b`: pct_b = 0 gives a, 100 gives b. Used to fade unselected items towards the page colour.
uint32_t Mix(uint32_t a, uint32_t b, int pct_b);
// The three-glyph legend used at the bottom of every screen: dot = click, two dots = double click, bar = hold.
enum class Glyph { kClick, kDouble, kHold };
lv_obj_t* GlyphView(lv_obj_t* parent, Glyph glyph, uint32_t color);

// One entry of the button legend. color 0 = neutral (white); otherwise a light tint of the colour. `strong` marks
// the main action of the page: it is filled (ink, or the colour) and twice as wide.
struct Hint {
    Glyph glyph;
    const char* label;
    uint32_t color = 0;
    bool strong = false;
};
// Empty legend container at the bottom of the page (kHintBarY).
lv_obj_t* HintBar(lv_obj_t* parent);
// Replaces the legend with up to three entries. `labels` (optional, n entries) receives the label objects, so a
// page can change a text later (for example "Mở Học từ mới" when the focus moves).
void FillHintBar(lv_obj_t* bar, const Hint* hints, int n, lv_obj_t** labels = nullptr);

}  // namespace ui
