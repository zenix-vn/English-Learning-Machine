// Study progress, stored as a snapshot plus an append-only journal so a power cut never loses more
// than the review in flight.
//
//   progress.bin   header + one CardState per card (rewritten every kSnapshotEvery reviews and on exit)
//   events.log     8-byte record per review, appended immediately; also the queue of reviews to upload
//
// The header remembers how many journal records the snapshot already contains (`applied`) and how many
// were already uploaded to the server (`uploaded`); records beyond `applied` are replayed on open.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sm2.h"

namespace offline {

struct ReviewEvent {                // journal record, 8 bytes
    uint16_t card;
    uint8_t quality;
    uint8_t reserved;
    uint32_t ts;
};
static_assert(sizeof(ReviewEvent) == 8, "journal records are written as is");

class Progress {
public:
    static constexpr uint32_t kSnapshotEvery = 25;
    static constexpr uint32_t kCompactAfter = 1000;

    // `pack_key` identifies the deck (changes when another deck is installed); `count` is the card count.
    // Progress of a different deck is discarded; progress of the same deck survives a rebuilt pack.
    bool Open(const std::string& dir, uint32_t pack_key, size_t count);

    size_t Count() const { return cards_.size(); }
    const CardState& At(size_t i) const { return cards_[i]; }
    CardState& Mutable(size_t i) { return cards_[i]; }

    // Grades a card, appends it to the journal and snapshots every kSnapshotEvery reviews.
    // With `defer` the review is kept in RAM only until Flush() (or Save()) writes it: a write to the flash pauses the
    // cache for tens of milliseconds, which shows as noise on the RGB panel, so the study screen batches the writes.
    bool Record(size_t card, int quality, uint32_t now, bool defer = false);
    bool Flush();                       // writes the deferred reviews to the journal (and a snapshot when due)
    size_t Deferred() const { return pending_.size(); }
    bool Save();

    // Reviews not yet uploaded, oldest first.
    std::vector<ReviewEvent> PendingUpload() const;
    // Call after the server accepted every pending review. Compacts the journal when it has grown.
    bool MarkUploaded();

    // Number of new (never studied) cards started today; resets when the day changes.
    uint32_t NewToday(uint32_t now);
    uint32_t last_time() const { return last_time_; }
    void set_last_time(uint32_t t) { last_time_ = t; }

private:
    struct Header {
        char magic[4];
        uint32_t pack_key, count, applied, uploaded, new_day, new_count, last_time;
    };
    std::string snapshot_path() const { return dir_ + "/progress.bin"; }
    std::string journal_path() const { return dir_ + "/events.log"; }
    bool LoadSnapshot(uint32_t pack_key, size_t count);
    uint32_t JournalCount() const;

    std::string dir_;
    uint32_t pack_key_ = 0;
    std::vector<CardState> cards_;
    uint32_t applied_ = 0, uploaded_ = 0, journal_count_ = 0;
    uint32_t new_day_ = 0, new_count_ = 0, last_time_ = 0;
    uint32_t since_snapshot_ = 0;
    std::vector<ReviewEvent> pending_;      // reviews graded but not yet in the journal
    uint32_t saved_count_ = 0;          // card count recorded in the snapshot that was loaded
};

}  // namespace offline
