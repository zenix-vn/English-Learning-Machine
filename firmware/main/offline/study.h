// Offline study mode: flashcards with spoken words and spaced repetition, no network needed.
//
// Controls (the board has a single usable button, Boot):
//   word shown      click = show meaning      double click = hear the word again      hold = menu
//   meaning shown   click = REMEMBERED        double click = FORGOT                   hold = EASY
//   menu            click = next item         hold = choose                           double click = back
//
// Entry points: the double click on the Boot button while the device is starting or waiting for Wi-Fi setup,
// and the server's "offline" system command (the tutor starts it when the learner asks).
#pragma once

#include <atomic>

namespace offline {

enum class Gesture { kClick, kDouble, kLong };

class OfflineStudy {
public:
    static OfflineStudy& GetInstance();

    bool Active() const { return active_; }
    void Start();                       // safe to call from any task
    void Post(Gesture gesture);         // called from the button callbacks
    void MarkActive(bool active) { active_ = active; }     // used by the study task only

private:
    OfflineStudy() = default;
    std::atomic<bool> active_{false};
    bool task_started_ = false;
};

}  // namespace offline
