// Where offline study data lives.
//
// The board has no SD card slot and no spare GPIO (see docs/offline-learning.md), so the data is kept in a FAT
// partition named "store" inside the 16 MB flash (partitions/v2/16m.csv). Everything else in main/offline only
// uses paths below kStoreMount, so another medium (for example an SD card on different hardware) can be mounted
// at the same place without touching the rest of the code.
#pragma once

#include <cstdint>
#include <string>

namespace offline {

constexpr const char* kStoreMount = "/store";

// Mounts (and, on first use, formats) the store. Safe to call more than once.
bool MountStore();
bool StoreMounted();
uint64_t StoreFreeBytes();
uint64_t StoreTotalBytes();

inline std::string PackDir() { return std::string(kStoreMount) + "/pack"; }
inline std::string PackTempDir() { return std::string(kStoreMount) + "/pack.new"; }

// Removes a directory and the regular files in it (the pack directories contain no subdirectories).
void RemoveDir(const std::string& dir);

}  // namespace offline
