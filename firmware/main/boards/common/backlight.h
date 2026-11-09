#pragma once

#include <cstdint>
#include <functional>

#include <driver/gpio.h>
#include <esp_timer.h>


class Backlight {
public:
    Backlight();
    ~Backlight();

    void RestoreBrightness();
    void SetBrightness(uint8_t brightness, bool permanent = false);
    inline uint8_t brightness() const { return brightness_; }
    // The level the user chose (not the momentary level while a page transition has darkened the screen).
    inline uint8_t user_brightness() const { return user_brightness_; }
    // Fades to `level` in about `ms` milliseconds without changing the level the user chose. Used by the page
    // transitions: fade to 0, change the page while dark, fade back to user_brightness().
    void FadeTo(uint8_t level, uint32_t ms);
    // Switches the light off and on again at once, without fading and without touching the saved level.
    // Used around flash writes: a write pauses the cache, which makes an RGB panel show noise, so the screen is
    // darkened for those few milliseconds instead.
    void SetBlank(bool blank);

protected:
    void OnTransitionTimer();
    virtual void SetBrightnessImpl(uint8_t brightness) = 0;

    esp_timer_handle_t transition_timer_ = nullptr;
    uint8_t brightness_ = 0;
    uint8_t target_brightness_ = 0;
    int step_ = 1;
    uint8_t user_brightness_ = 0;
    bool blanked_ = false;
};


class PwmBacklight : public Backlight {
public:
    PwmBacklight(gpio_num_t pin, bool output_invert = false, uint32_t freq_hz = 25000);
    ~PwmBacklight();

    void SetBrightnessImpl(uint8_t brightness) override;
};
