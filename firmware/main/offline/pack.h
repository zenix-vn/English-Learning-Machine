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
    // Opens a pack that already lives in memory (for example data embedded in the firmware image): nothing is copied,
    // so it needs no storage at all and showing a card never touches the flash chip. The memory must outlive the reader.
    bool OpenMemory(const uint8_t* cards, size_t cards_size, const uint8_t* cards_idx, size_t cards_idx_size,
                    const uint8_t* audio, size_t audio_size, const uint8_t* audio_idx, size_t audio_idx_size);
    void Close();

    size_t Count() const { return count_; }
    bool GetCard(size_t index, Card& out);
    bool HasAudio(size_t index) const;
    // Calls fn(index, word) for every card in one sequential pass over cards.bin (much faster than GetCard in a loop).
    template <typename Fn> bool ForEachWord(Fn&& fn);
    bool ReadAudio(size_t index, std::vector<uint8_t>& out);

private:
    static void Preload(FILE* f, std::vector<uint8_t>& out);
    // Small packs are read into RAM once, when opened, so showing a card never touches the flash. A flash read
    // pauses the cache for a moment, which starves the RGB panel (it reads its frame buffer through that cache)
    // and shows as noise on the screen.
    static constexpr size_t kPreloadLimit = 1024 * 1024;
    FILE* cards_ = nullptr;
    FILE* audio_ = nullptr;
    std::vector<uint8_t> cards_mem_, audio_mem_;
    const uint8_t* cards_ptr_ = nullptr;     // cards.bin and audio.bin in memory, or null when read from the files
    const uint8_t* audio_ptr_ = nullptr;
    size_t cards_len_ = 0, audio_len_ = 0;
    size_t count_ = 0;
    std::vector<uint32_t> cards_idx_;     // N + 1 offsets
    std::vector<uint32_t> audio_idx_;     // 2 * N: offset, length
};

template <typename Fn>
bool PackReader::ForEachWord(Fn&& fn) {
    if (!cards_ && !cards_ptr_) return false;
    std::string word;
    size_t index = 0;
    int field = 0;              // 0 = word, 1..3 = ipa, meaning, example
    bool word_done = false;
    auto feed = [&](const char* data, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            const char ch = data[i];
            if (ch != '\0') {
                if (field == 0 && !word_done) word.push_back(ch);
                continue;
            }
            if (field == 0) { fn(index, word); word.clear(); word_done = true; }
            if (++field == 4) { field = 0; word_done = false; ++index; }
        }
    };
    if (cards_ptr_) {
        feed(reinterpret_cast<const char*>(cards_ptr_), cards_len_);
    } else {
        std::fseek(cards_, 0, SEEK_SET);
        std::vector<char> buf(4096);
        size_t got;
        while ((got = std::fread(buf.data(), 1, buf.size(), cards_)) > 0) feed(buf.data(), got);
    }
    return index == count_;
}

}  // namespace offline
