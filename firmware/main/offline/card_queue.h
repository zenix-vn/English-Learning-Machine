// Chooses which card to show next.
#pragma once

#include <cstdint>

#include "progress.h"

namespace offline {

enum class PickKind { kNone, kLearning, kDue, kNew, kExtra };

struct Pick {
    int card = -1;
    PickKind kind = PickKind::kNone;
};

struct QueueStats {
    uint32_t learning = 0;      // cards in their short steps that are due now
    uint32_t due = 0;           // review cards due now
    uint32_t fresh = 0;         // cards never studied
    uint32_t new_left = 0;      // new cards still allowed today
};

// Order: learning cards due now, then reviews due now, then new cards (up to `new_per_day`),
// then "extra practice" (the card that will be due soonest), so the learner is never left with an empty screen.
// `avoid` is a card that must not be returned (the one just answered).
Pick NextCard(const Progress& progress, uint32_t now, uint32_t new_today, uint32_t new_per_day, int avoid = -1);
QueueStats Stats(const Progress& progress, uint32_t now, uint32_t new_today, uint32_t new_per_day);

}  // namespace offline
