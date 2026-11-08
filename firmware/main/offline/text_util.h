// Text helpers for the study screen. Pure functions (no LVGL), unit-tested on the host.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace offline {

// Number of Unicode code points in a UTF-8 string (the screen is measured in characters, not bytes).
size_t Utf8Length(const std::string& s);

// Shortens a dictionary meaning so it fits the screen: at most `max_chars` characters, cut at the last
// sense boundary (';' then ',') when there is a reasonable one, otherwise at a word boundary + "...".
// Never splits a UTF-8 sequence.
std::string ShortMeaning(const std::string& meaning, size_t max_chars);

// Examples are stored as "English sentence — Vietnamese gloss".
void SplitExample(const std::string& example, std::string& english, std::string& vietnamese);

// "gặp lại sau 10 phút / 3 giờ / 5 ngày / 2 tháng" for an interval in minutes.
std::string FormatInterval(uint32_t minutes);

// Headword size in pixels for the 64/44/28 px fonts. `narrow` is the two-column (answer) layout.
int WordFontSize(size_t chars, bool narrow);

}  // namespace offline
