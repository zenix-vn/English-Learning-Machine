// The first-run screens: start-up splash, Wi-Fi setup guide and activation code. Same look as the home screen
// (cream page, Sophia, coloured step cards). They cover the whole display while the device is starting, waiting for
// Wi-Fi setup, or waiting to be activated on the server.
//
//   Wi-Fi setup   three numbered cards: join the device's hotspot, open its address, pick the home Wi-Fi.
//   Activation    the code in big boxes; each digit lights up while the voice reads it, a click reads it again.
//   Splash        Sophia and a short status line.
//
// All calls need the display lock (the LVGL task has it). HomeScreen decides which page is shown.
#pragma once

#include <lvgl.h>

#include <cstdint>
#include <string>

namespace home {

class SetupScreen {
public:
    enum class Page { kNone, kSplash, kWifi, kCode };

    void Build();
    void ShowPage(Page page);                       // kNone hides the whole screen

    void SetSplashText(const std::string& status);
    void SetWifi(const std::string& ssid, const std::string& url);
    void SetCode(const std::string& code);          // the digits to show (and read)
    void StartReading(int64_t now_us);              // the voice starts reading the code now: light up the digits

    void Tick(int64_t now_us);                      // Sophia's movements, the dots and the lit digit; every 150 ms

    // The voice prompt before the digits lasts about this long (activation.ogg); each digit about the second figure.
    static constexpr int kPromptMs = 2900, kDigitMs = 560;

private:
    static constexpr int kMaxDigits = 8;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* mascot_ = nullptr;
    lv_obj_t* group_[4] = {};                       // splash, wifi, code (index 1..3)
    lv_obj_t* splash_status_ = nullptr;
    lv_obj_t* splash_dots_[3] = {};
    lv_obj_t* wifi_ssid_ = nullptr;
    lv_obj_t* wifi_url_ = nullptr;
    lv_obj_t* wifi_status_ = nullptr;
    lv_obj_t* code_status_ = nullptr;
    lv_obj_t* digit_box_[kMaxDigits] = {};
    lv_obj_t* digit_label_[kMaxDigits] = {};
    lv_obj_t* admin_url_ = nullptr;

    Page page_ = Page::kNone;
    int digits_ = 0;
    int lit_ = -1;                                  // digit being read, -1 = none
    int64_t read_start_us_ = 0;
    int mascot_frame_ = -1;
    int step_ = 0;
    int64_t next_blink_us_ = 0, blink_until_us_ = 0;

    void SetDigitLit(int index);
};

}  // namespace home
