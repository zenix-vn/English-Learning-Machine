#include "card_queue.h"

#include <limits>

namespace offline {

Pick NextCard(const Progress& progress, uint32_t now, uint32_t new_today, uint32_t new_per_day, int avoid) {
    int learning = -1, review = -1, fresh = -1, soonest = -1;
    uint32_t learning_due = std::numeric_limits<uint32_t>::max();
    uint32_t review_due = learning_due, soonest_due = learning_due;
    for (size_t i = 0; i < progress.Count(); ++i) {
        if (static_cast<int>(i) == avoid) continue;
        const CardState& c = progress.At(i);
        if (!(c.flags & kSeen)) {
            if (fresh < 0) fresh = static_cast<int>(i);
            continue;
        }
        if (c.due <= now) {
            if (IsLearning(c)) {
                if (c.due < learning_due) { learning_due = c.due; learning = static_cast<int>(i); }
            } else if (c.due < review_due) {
                review_due = c.due; review = static_cast<int>(i);
            }
        } else if (c.due < soonest_due) {
            soonest_due = c.due; soonest = static_cast<int>(i);
        }
    }
    if (learning >= 0) return {learning, PickKind::kLearning};
    if (review >= 0) return {review, PickKind::kDue};
    if (fresh >= 0 && new_today < new_per_day) return {fresh, PickKind::kNew};
    if (soonest >= 0) return {soonest, PickKind::kExtra};
    if (fresh >= 0) return {fresh, PickKind::kNew};          // daily new-card limit reached but nothing else to do
    return {};
}

Pick NextCardFor(const Progress& progress, uint32_t now, uint32_t new_today, uint32_t new_per_day, int avoid,
                 Focus focus, bool extra) {
    int learning = -1, review = -1, fresh = -1, soonest = -1;
    uint32_t learning_due = std::numeric_limits<uint32_t>::max();
    uint32_t review_due = learning_due, soonest_due = learning_due;
    for (size_t i = 0; i < progress.Count(); ++i) {
        if (static_cast<int>(i) == avoid) continue;
        const CardState& c = progress.At(i);
        if (!(c.flags & kSeen)) {
            if (fresh < 0) fresh = static_cast<int>(i);
            continue;
        }
        if (c.due <= now) {
            if (IsLearning(c)) {
                if (c.due < learning_due) { learning_due = c.due; learning = static_cast<int>(i); }
            } else if (c.due < review_due) {
                review_due = c.due; review = static_cast<int>(i);
            }
        } else if (c.due < soonest_due) {
            soonest_due = c.due; soonest = static_cast<int>(i);
        }
    }
    if (learning >= 0) return {learning, PickKind::kLearning};
    if (focus == Focus::kNew) {
        if (fresh >= 0 && (extra || new_today < new_per_day)) return {fresh, PickKind::kNew};
        return {};
    }
    if (review >= 0) return {review, PickKind::kDue};
    if (extra && soonest >= 0) return {soonest, PickKind::kExtra};
    return {};
}

QueueStats Stats(const Progress& progress, uint32_t now, uint32_t new_today, uint32_t new_per_day) {
    QueueStats s;
    for (size_t i = 0; i < progress.Count(); ++i) {
        const CardState& c = progress.At(i);
        if (!(c.flags & kSeen)) { s.fresh++; continue; }
        if (c.due <= now) (IsLearning(c) ? s.learning : s.due)++;
    }
    s.new_left = new_today >= new_per_day ? 0 : (new_per_day - new_today < s.fresh ? new_per_day - new_today : s.fresh);
    return s;
}

}  // namespace offline
