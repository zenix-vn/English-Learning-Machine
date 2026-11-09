#include "pack.h"

#include <cstring>

namespace offline {

namespace {

constexpr uint32_t kMaxRecord = 4096;     // a card is a few hundred bytes; anything bigger is corrupt
constexpr uint32_t kMaxClip = 64 * 1024;  // a spoken word is about 1 KB

}  // namespace

// Reads a whole file into `out` when it is small enough; otherwise leaves `out` empty and the file is read on demand.
void PackReader::Preload(FILE* f, std::vector<uint8_t>& out) {
    out.clear();
    const long size = std::ftell(f);
    if (size <= 0 || static_cast<size_t>(size) > kPreloadLimit) return;
    out.resize(static_cast<size_t>(size));
    std::fseek(f, 0, SEEK_SET);
    if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
}

namespace {

bool ReadWords(const std::string& path, std::vector<uint32_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    bool ok = size >= 0 && size % 4 == 0;
    if (ok) {
        out.resize(static_cast<size_t>(size) / 4);
        ok = out.empty() || std::fread(out.data(), 4, out.size(), f) == out.size();
    }
    std::fclose(f);
    return ok;      // the format is little endian, as is the ESP32-S3
}

}  // namespace

bool PackReader::Open(const std::string& dir) {
    Close();
    if (!ReadWords(dir + "/cards.idx", cards_idx_) || cards_idx_.size() < 2) return Close(), false;
    count_ = cards_idx_.size() - 1;
    if (!ReadWords(dir + "/audio.idx", audio_idx_) || audio_idx_.size() != 2 * count_) return Close(), false;
    cards_ = std::fopen((dir + "/cards.bin").c_str(), "rb");
    audio_ = std::fopen((dir + "/audio.bin").c_str(), "rb");
    if (!cards_ || !audio_) return Close(), false;
    // the last index entry is the size of cards.bin
    std::fseek(cards_, 0, SEEK_END);
    if (static_cast<uint32_t>(std::ftell(cards_)) != cards_idx_.back()) return Close(), false;
    Preload(cards_, cards_mem_);
    std::fseek(audio_, 0, SEEK_END);
    Preload(audio_, audio_mem_);
    cards_ptr_ = cards_mem_.empty() ? nullptr : cards_mem_.data();
    cards_len_ = cards_mem_.size();
    audio_ptr_ = audio_mem_.empty() ? nullptr : audio_mem_.data();
    audio_len_ = audio_mem_.size();
    return true;
}

bool PackReader::OpenMemory(const uint8_t* cards, size_t cards_size, const uint8_t* cards_idx, size_t cards_idx_size,
                            const uint8_t* audio, size_t audio_size, const uint8_t* audio_idx, size_t audio_idx_size) {
    Close();
    if (cards_idx_size < 8 || cards_idx_size % 4 != 0) return false;
    cards_idx_.resize(cards_idx_size / 4);
    std::memcpy(cards_idx_.data(), cards_idx, cards_idx_size);
    count_ = cards_idx_.size() - 1;
    if (audio_idx_size != 8 * count_) return Close(), false;
    audio_idx_.resize(audio_idx_size / 4);
    std::memcpy(audio_idx_.data(), audio_idx, audio_idx_size);
    if (cards_idx_.back() != cards_size) return Close(), false;
    cards_ptr_ = cards;
    cards_len_ = cards_size;
    audio_ptr_ = audio;
    audio_len_ = audio_size;
    return true;
}

void PackReader::Close() {
    if (cards_) std::fclose(cards_);
    if (audio_) std::fclose(audio_);
    cards_ = audio_ = nullptr;
    count_ = 0;
    cards_idx_.clear();
    audio_idx_.clear();
    cards_ptr_ = audio_ptr_ = nullptr;
    cards_len_ = audio_len_ = 0;
    cards_mem_.clear();
    cards_mem_.shrink_to_fit();
    audio_mem_.clear();
    audio_mem_.shrink_to_fit();
}

bool PackReader::GetCard(size_t index, Card& out) {
    if ((!cards_ && !cards_ptr_) || index >= count_) return false;
    const uint32_t begin = cards_idx_[index], end = cards_idx_[index + 1];
    if (end <= begin || end - begin > kMaxRecord) return false;
    std::string buf(end - begin, '\0');
    if (cards_ptr_) {
        if (end > cards_len_) return false;
        std::memcpy(buf.data(), cards_ptr_ + begin, buf.size());
    } else if (std::fseek(cards_, begin, SEEK_SET) != 0 || std::fread(buf.data(), 1, buf.size(), cards_) != buf.size()) {
        return false;
    }
    std::string* fields[4] = {&out.word, &out.ipa, &out.meaning, &out.example};
    size_t pos = 0;
    for (auto* field : fields) {
        const size_t nul = buf.find('\0', pos);
        if (nul == std::string::npos) return false;
        field->assign(buf, pos, nul - pos);
        pos = nul + 1;
    }
    return true;
}

bool PackReader::HasAudio(size_t index) const {
    return index < count_ && audio_idx_[2 * index + 1] != 0;
}

bool PackReader::ReadAudio(size_t index, std::vector<uint8_t>& out) {
    if ((!audio_ && !audio_ptr_) || !HasAudio(index)) return false;
    const uint32_t offset = audio_idx_[2 * index], length = audio_idx_[2 * index + 1];
    if (length > kMaxClip) return false;
    out.resize(length);
    if (audio_ptr_) {
        if (static_cast<size_t>(offset) + length > audio_len_) return false;
        std::memcpy(out.data(), audio_ptr_ + offset, length);
        return true;
    }
    return std::fseek(audio_, offset, SEEK_SET) == 0 && std::fread(out.data(), 1, length, audio_) == length;
}

}  // namespace offline
