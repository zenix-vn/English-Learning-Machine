// The starter vocabulary built into the firmware (scripts/gen_starter_pack.py and gen_starter_audio.py), so
// vocabulary, review and the dictionary work with no network and no server pack. It is read straight from the
// firmware image: nothing is copied to the flash storage. A pack downloaded from the server replaces it.
#pragma once

#include <cstddef>
#include <cstdint>

namespace offline {

struct StarterPack {
    const uint8_t *cards, *cards_idx, *audio, *audio_idx;
    size_t cards_size, cards_idx_size, audio_size, audio_idx_size;
    size_t count;
    const char* id;         // "starter-N"; bump N when the words or clips change (the progress file is kept only
                            // while the card order stays the same)
};

const StarterPack& Starter();

}  // namespace offline
