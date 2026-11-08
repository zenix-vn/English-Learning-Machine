#include "sm2.h"

#include <algorithm>

namespace offline {

void Grade(CardState& card, int quality, uint32_t now) {
    const int q = std::clamp(quality, 0, 5);
    if (q < 3) {
        card.reps = 0;
        card.interval_min = 10;                                   // try again in 10 minutes
    } else {
        if (card.reps < 255) card.reps++;
        if (card.reps == 1) {
            card.interval_min = kDayMinutes;
        } else if (card.reps == 2) {
            card.interval_min = 3 * kDayMinutes;
        } else {
            card.interval_min = static_cast<uint32_t>(
                (static_cast<uint64_t>(card.interval_min) * card.ease_milli + 500) / 1000);
        }
        // ease += 0.1 - (5-q) * (0.08 + (5-q) * 0.02), in thousandths, never below 1.3
        const int miss = 5 - q;
        const int ease = static_cast<int>(card.ease_milli) + 100 - miss * (80 + miss * 20);
        card.ease_milli = static_cast<uint16_t>(std::max(ease, 1300));
    }
    card.due = now + card.interval_min * 60u;
    card.flags |= kSeen;
}

}  // namespace offline
