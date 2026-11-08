#include "progress.h"

#include <cstdio>
#include <cstring>

namespace offline {

namespace {
constexpr char kMagic[4] = {'E', 'L', 'P', '1'};
constexpr uint32_t kDay = 86400;

bool WriteAll(FILE* f, const void* data, size_t size) { return std::fwrite(data, 1, size, f) == size; }
}  // namespace

uint32_t Progress::JournalCount() const {
    FILE* f = std::fopen(journal_path().c_str(), "rb");
    if (!f) return 0;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fclose(f);
    return size < 0 ? 0 : static_cast<uint32_t>(size / sizeof(ReviewEvent));    // a torn last record is ignored
}

bool Progress::LoadSnapshot(uint32_t pack_key, size_t count) {
    std::string path = snapshot_path();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {                                             // a crash between remove() and rename() leaves only the temp file
        f = std::fopen((path + ".tmp").c_str(), "rb");
        if (!f) return false;
    }
    Header h;
    bool ok = std::fread(&h, sizeof h, 1, f) == 1 && std::memcmp(h.magic, kMagic, 4) == 0 &&
              h.pack_key == pack_key && h.count <= 0xFFFF;
    if (ok) {
        // A rebuilt pack of the same deck may hold more (or fewer) cards; card numbers are stable, so keep progress.
        cards_.assign(h.count, CardState{});
        ok = std::fread(cards_.data(), sizeof(CardState), h.count, f) == h.count;
        if (ok) cards_.resize(count);
    }
    std::fclose(f);
    if (!ok) return false;
    saved_count_ = h.count;
    applied_ = h.applied;
    uploaded_ = h.uploaded;
    new_day_ = h.new_day;
    new_count_ = h.new_count;
    last_time_ = h.last_time;
    return true;
}

bool Progress::Open(const std::string& dir, uint32_t pack_key, size_t count) {
    dir_ = dir;
    pack_key_ = pack_key;
    cards_.assign(count, CardState{});
    applied_ = uploaded_ = new_day_ = new_count_ = last_time_ = since_snapshot_ = 0;
    const bool had_snapshot = LoadSnapshot(pack_key, count);
    bool fix_snapshot = false;
    if (!had_snapshot) {
        cards_.assign(count, CardState{});
        applied_ = uploaded_ = new_day_ = new_count_ = last_time_ = 0;
        std::remove(journal_path().c_str());              // a journal without its snapshot belongs to another deck
        fix_snapshot = true;                              // write a baseline now, so a power cut can never orphan the journal
    }
    if (had_snapshot && count != saved_count_) fix_snapshot = true;          // card count changed: rewrite the header
    journal_count_ = JournalCount();
    if (applied_ > journal_count_ || uploaded_ > journal_count_) {
        // The journal was emptied by a compaction whose final snapshot never got written: everything the
        // old journal held is already inside the snapshot. Restart counting and fix the header at once,
        // otherwise a review recorded later would be mistaken for one the snapshot already contains.
        applied_ = uploaded_ = 0;
        fix_snapshot = true;
    }

    // replay reviews made after the last snapshot
    if (applied_ < journal_count_) {
        FILE* f = std::fopen(journal_path().c_str(), "rb");
        if (f) {
            std::fseek(f, static_cast<long>(applied_ * sizeof(ReviewEvent)), SEEK_SET);
            ReviewEvent e;
            while (applied_ < journal_count_ && std::fread(&e, sizeof e, 1, f) == 1) {
                if (e.card < cards_.size()) {
                    if (!(cards_[e.card].flags & kSeen)) {
                        if (new_day_ != e.ts / kDay) { new_day_ = e.ts / kDay; new_count_ = 0; }
                        new_count_++;
                    }
                    Grade(cards_[e.card], e.quality, e.ts);
                }
                if (e.ts > last_time_) last_time_ = e.ts;
                applied_++;
            }
            std::fclose(f);
        }
    }
    return fix_snapshot ? Save() : true;
}

uint32_t Progress::NewToday(uint32_t now) {
    if (new_day_ != now / kDay) { new_day_ = now / kDay; new_count_ = 0; }
    return new_count_;
}

bool Progress::Record(size_t card, int quality, uint32_t now) {
    if (card >= cards_.size() || card > 0xFFFF) return false;
    NewToday(now);
    if (!(cards_[card].flags & kSeen)) new_count_++;
    Grade(cards_[card], quality, now);
    last_time_ = now;

    ReviewEvent e{static_cast<uint16_t>(card), static_cast<uint8_t>(quality), 0, now};
    FILE* f = std::fopen(journal_path().c_str(), "ab");
    if (!f) return false;
    bool ok = WriteAll(f, &e, sizeof e) && std::fflush(f) == 0;
    std::fclose(f);
    if (!ok) return false;
    journal_count_++;
    applied_ = journal_count_;
    if (++since_snapshot_ >= kSnapshotEvery) return Save();
    return true;
}

bool Progress::Save() {
    const std::string path = snapshot_path(), tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    Header h{};
    std::memcpy(h.magic, kMagic, 4);
    h.pack_key = pack_key_;
    h.count = static_cast<uint32_t>(cards_.size());
    h.applied = applied_;
    h.uploaded = uploaded_;
    h.new_day = new_day_;
    h.new_count = new_count_;
    h.last_time = last_time_;
    bool ok = WriteAll(f, &h, sizeof h) && WriteAll(f, cards_.data(), cards_.size() * sizeof(CardState)) &&
              std::fflush(f) == 0;
    std::fclose(f);
    if (!ok) return false;
    std::remove(path.c_str());                            // FAT rename() may refuse to replace a file
    if (std::rename(tmp.c_str(), path.c_str()) != 0) return false;
    since_snapshot_ = 0;
    return true;
}

std::vector<ReviewEvent> Progress::PendingUpload() const {
    std::vector<ReviewEvent> out;
    FILE* f = std::fopen(journal_path().c_str(), "rb");
    if (!f) return out;
    std::fseek(f, static_cast<long>(uploaded_ * sizeof(ReviewEvent)), SEEK_SET);
    ReviewEvent e;
    while (std::fread(&e, sizeof e, 1, f) == 1) out.push_back(e);
    std::fclose(f);
    return out;
}

bool Progress::MarkUploaded() {
    uploaded_ = journal_count_;
    if (!Save()) return false;
    if (journal_count_ < kCompactAfter) return true;
    // Everything is uploaded and contained in the snapshot: start a fresh journal.
    // Order matters for crash safety: truncate first, then record the empty journal in the snapshot.
    FILE* f = std::fopen(journal_path().c_str(), "wb");
    if (!f) return false;
    std::fclose(f);
    journal_count_ = applied_ = uploaded_ = 0;
    return Save();
}

}  // namespace offline
