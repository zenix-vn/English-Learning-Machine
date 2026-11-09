#include "starter_pack.h"

extern const uint8_t starter_cards_start[] asm("_binary_starter_cards_bin_start");
extern const uint8_t starter_cards_end[] asm("_binary_starter_cards_bin_end");
extern const uint8_t starter_idx_start[] asm("_binary_starter_cards_idx_start");
extern const uint8_t starter_idx_end[] asm("_binary_starter_cards_idx_end");
extern const uint8_t starter_audio_start[] asm("_binary_starter_audio_bin_start");
extern const uint8_t starter_audio_end[] asm("_binary_starter_audio_bin_end");
extern const uint8_t starter_aidx_start[] asm("_binary_starter_audio_idx_start");
extern const uint8_t starter_aidx_end[] asm("_binary_starter_audio_idx_end");

namespace offline {

const StarterPack& Starter() {
    static const StarterPack pack = {
        starter_cards_start, starter_idx_start, starter_audio_start, starter_aidx_start,
        static_cast<size_t>(starter_cards_end - starter_cards_start), static_cast<size_t>(starter_idx_end - starter_idx_start),
        static_cast<size_t>(starter_audio_end - starter_audio_start), static_cast<size_t>(starter_aidx_end - starter_aidx_start),
        static_cast<size_t>(starter_idx_end - starter_idx_start) / 4 - 1,
        "starter-2",
    };
    return pack;
}

}  // namespace offline
