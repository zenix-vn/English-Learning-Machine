// Spaced-repetition scheduling (SM-2 variant) used by offline study.
//
// The rules are identical to backend/app/tools.py apply_grade(), so progress made online and offline
// stays consistent. Intervals are kept in whole minutes because the device has to handle the
// 10-minute "again" step as well as multi-day intervals.
#pragma once

#include <cstdint>

namespace offline {

constexpr uint8_t kSeen = 1;              // CardState::flags bit: the card has been studied at least once
constexpr uint32_t kDayMinutes = 1440;

struct CardState {                        // 12 bytes, stored as is in progress.bin
    uint32_t due = 0;                     // unix seconds when the card is due
    uint32_t interval_min = 0;            // current interval in minutes
    uint16_t ease_milli = 2500;           // ease factor x1000 (1300 .. )
    uint8_t reps = 0;                     // consecutive successful reviews
    uint8_t flags = 0;
};
static_assert(sizeof(CardState) == 12, "CardState must stay 12 bytes: it is written to flash as is");

// Quality: 0 = forgot, 3 = hard, 4 = good, 5 = easy. Values below 3 reset the card.
void Grade(CardState& card, int quality, uint32_t now);

// True while a card is still in its short "learning" steps (interval below one day).
inline bool IsLearning(const CardState& card) { return card.interval_min < kDayMinutes; }

}  // namespace offline
