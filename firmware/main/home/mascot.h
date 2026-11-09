// Sophia's pictures, shared by the home, chat and setup screens.
#pragma once

#include <lvgl.h>

namespace home {

enum Frame { kIdle, kBlink, kTalk1, kTalk2, kListen, kThink, kHappy, kWave, kSmall, kFrameCount };

// The frame as an LVGL image (RGB565 + alpha in PSRAM), inflated on first use. kSmall is 100 x 100, the others 280 x 280.
const lv_image_dsc_t* MascotFrame(int frame);

}  // namespace home
