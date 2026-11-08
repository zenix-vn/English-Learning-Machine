// Reader for an offline study pack downloaded from the server (see backend/app/packbuild.py).
//
//   cards.bin  records "word\0ipa\0meaning\0example\0"      cards.idx  uint32 offsets, N+1 entries
//   audio.bin  one OGG/Opus clip per card                   audio.idx  per card: uint32 offset, uint32 length
//
// Only the two small index files are kept in memory; text and audio are read from storage on demand.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace offline {

struct Card {
    std::string word, ipa, meaning, example;
};

class PackReader {
public:
    PackReader() = default;
    ~PackReader() { Close(); }
    PackReader(const PackReader&) = delete;
    PackReader& operator=(const PackReader&) = delete;

    // Opens the pack stored in `dir`. Returns false when a file is missing or inconsistent.
    bool Open(const std::string& dir);
    void Close();

    size_t Count() const { return count_; }
    bool GetCard(size_t index, Card& out);
    bool HasAudio(size_t index) const;
    bool ReadAudio(size_t index, std::vector<uint8_t>& out);

private:
    FILE* cards_ = nullptr;
    FILE* audio_ = nullptr;
    size_t count_ = 0;
    std::vector<uint32_t> cards_idx_;     // N + 1 offsets
    std::vector<uint32_t> audio_idx_;     // 2 * N: offset, length
};

}  // namespace offline
