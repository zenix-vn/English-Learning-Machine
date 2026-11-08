// Wi-Fi synchronisation of the offline study data (server side: backend/app/sync.py).
//
//   InstallPack()       downloads the pack built for this device's deck when it differs from the installed one
//   ExchangeProgress()  merges progress made online, then uploads reviews made offline
//
// Both block (seconds to a few minutes) and must run on a worker task, never on the UI or button task.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "progress.h"

namespace offline {

using StatusFn = std::function<void(const std::string&)>;      // human-readable progress, shown on screen

struct SyncOutcome {
    bool ok = false;
    bool pack_changed = false;      // a different pack was installed: reopen it
    std::string message;            // what to tell the user
};

struct InstalledPack {
    bool valid = false;
    std::string pack_id;
    uint32_t deck_id = 0;
    uint32_t count = 0;
};

InstalledPack ReadInstalledPack();
SyncOutcome InstallPack(const StatusFn& status);
SyncOutcome ExchangeProgress(Progress& progress, const InstalledPack& pack, const StatusFn& status);

}  // namespace offline
