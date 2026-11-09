// Offline learning programmes, no network needed: new words, review, dictionary, achievements and Wi-Fi sync.
// The words come from the pack in the store (the built-in starter pack, or a bigger one downloaded once).
//
// Controls (the board has a single usable button, Boot):
//   word shown      click = show meaning      double click = hear the word again      hold = menu
//   meaning shown   click = REMEMBERED        double click = FORGOT                   hold = EASY
//   menu            click = next item         hold = choose                           double click = back
//   dictionary      click = next              double click = previous                 hold = open / choose
//
// Entry points: the home screen tiles, a double click on the Boot button while the device is starting or waiting
// for Wi-Fi setup, and the server's "offline" system command.
#pragma once

#include <atomic>

namespace offline {

enum class Gesture { kClick, kDouble, kLong };
enum class Mode { kNew, kReview, kDict, kStats, kSync };

class OfflineStudy {
public:
    static OfflineStudy& GetInstance();

    bool Active() const { return active_; }
    void Start(Mode mode = Mode::kNew);     // safe to call from any task
    void Post(Gesture gesture);             // called from the button callbacks
    void MarkActive(bool active) { active_ = active; }     // used by the study task only

private:
    OfflineStudy() = default;
    std::atomic<bool> active_{false};
    bool task_started_ = false;
};

}  // namespace offline
