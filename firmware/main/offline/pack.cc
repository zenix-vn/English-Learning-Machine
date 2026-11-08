#include "pack.h"

#include <cstring>

namespace offline {

namespace {

constexpr uint32_t kMaxRecord = 4096;     // a card is a few hundred bytes; anything bigger is corrupt
constexpr uint32_t kMaxClip = 64 * 1024;  // a spoken word is about 1 KB

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
    return true;
}

void PackReader::Close() {
    if (cards_) std::fclose(cards_);
    if (audio_) std::fclose(audio_);
    cards_ = audio_ = nullptr;
    count_ = 0;
    cards_idx_.clear();
    audio_idx_.clear();
}

bool PackReader::GetCard(size_t index, Card& out) {
    if (!cards_ || index >= count_) return false;
    const uint32_t begin = cards_idx_[index], end = cards_idx_[index + 1];
    if (end <= begin || end - begin > kMaxRecord) return false;
    std::string buf(end - begin, '\0');
    if (std::fseek(cards_, begin, SEEK_SET) != 0 || std::fread(buf.data(), 1, buf.size(), cards_) != buf.size())
        return false;
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
    if (!audio_ || !HasAudio(index)) return false;
    const uint32_t offset = audio_idx_[2 * index], length = audio_idx_[2 * index + 1];
    if (length > kMaxClip) return false;
    out.resize(length);
    return std::fseek(audio_, offset, SEEK_SET) == 0 && std::fread(out.data(), 1, length, audio_) == length;
}

}  // namespace offline
