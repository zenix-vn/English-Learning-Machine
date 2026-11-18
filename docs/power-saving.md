# Power saving / sleep mode: removed (2026-11-10)

A sleep mode was tried and **removed from the code**. Do not re-add it as it was. Read this first.

## What it did
Dim after 30 s, then (60 s) backlight 0 + `esp_lcd_panel_disp_on_off(false)` + `esp_lcd_rgb_panel_set_pclk(1 MHz)` +
`lvgl_port_stop` + CPU DFS 40..80 MHz (`CONFIG_PM_ENABLE`), deep sleep after 10 min (ext0 on GPIO0).

## What went wrong
- Waking from the "screen off" stage showed horizontal stripes: a pclk change only applies at the next VSYNC (a frame at
  1 MHz lasts ~0.45 s), and DFS under the running RGB panel disturbs the bounce-buffer ISR (it copies ~43 MB/s from PSRAM).
- After the first flash of this build the **backlight started to flash permanently** (image stable). It survived
  reflashing the original firmware, a test build with GPIO4 held steady high (no PWM), lower brightness, USB vs battery
  and a USB power cycle. Nothing in the original code changes the backlight while a screen is static, and a regenerated
  `sdkconfig` was identical. The device it happened on was otherwise fine before.
- Suspected cause (not proven): the GC9503 has no reset pin here (`reset_gpio_num = NC`), keeps its registers while it
  is powered (battery), and was left in a bad state by toggling DISP / pclk under it. Try a full power-off (battery
  disconnected, Boot held 10 s, 1 min) on the affected unit.

## Rules if sleep is tried again
- Never call `esp_lcd_panel_disp_on_off`, `esp_lcd_rgb_panel_set_pclk`, change CPU frequency or enable `CONFIG_PM_ENABLE`
  while the RGB panel scans. The panel driver holds a no-light-sleep lock for its whole life, so light sleep is impossible anyway.
- Safe parts: fade the backlight down (`Backlight::FadeTo`), stop the wake word / mic, and deep sleep only after the
  backlight is off. Deep sleep wake on GPIO0 (strapping pin) was never verified.
- Try it on one unit and look at it for a day before flashing others.
