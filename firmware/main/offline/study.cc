#include "study.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sys/stat.h>

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "application.h"
#include "card_queue.h"
#include "pack.h"
#include "progress.h"
#include "storage.h"
#include "study_view.h"
#include "sync.h"
#include "text_util.h"

#define TAG "OfflineStudy"

#ifndef CONFIG_EL_OFFLINE_NEW_PER_DAY
#define CONFIG_EL_OFFLINE_NEW_PER_DAY 10
#endif

namespace offline {

namespace {

constexpr int kDoubleWindowMs = 320;            // a click is held this long to see whether a second one follows
constexpr int64_t kDuplicateDoubleUs = 450'000;  // the button driver may report a double click twice
constexpr uint32_t kGuessedNow = 1'700'000'000;  // starting point of the clock estimate when nothing better is known
const char* const kMenuItems[] = {"Tiếp tục học", "Đồng bộ ngay (Wi-Fi)", "Thoát học offline"};

enum class Screen { kFront, kBack, kMenu, kNotice, kDone, kBusy };

struct Msg {
    enum Type : uint8_t { kStart, kGesture } type;
    Gesture gesture;
};

struct TagStyle { const char* text; uint32_t color; };
TagStyle TagFor(PickKind kind) {
    switch (kind) {
        case PickKind::kNew: return {"MỚI", ui::kNew};
        case PickKind::kLearning: return {"HỌC LẠI", ui::kLearn};
        case PickKind::kDue: return {"ÔN TẬP", ui::kDue};
        default: return {"ÔN THÊM", ui::kExtra};
    }
}

}  // namespace

struct StudyImpl {
    QueueHandle_t queue = nullptr;
    bool pending_click = false;
    int64_t last_double_us = 0;

    StudyView view;
    Screen screen = Screen::kNotice;
    int menu_sel = 0;

    // what the notice screen shows, and where a click leads
    Icon notice_icon = Icon::kWarn;
    std::string notice_tag = "THÔNG BÁO", notice_title, notice_body;
    bool notice_resumes = false;            // true: back to the cards, false: open the menu

    PackReader pack;
    Progress progress;
    InstalledPack installed;
    bool ready = false;
    int card = -1;
    PickKind kind = PickKind::kNone;
    Card data;

    uint32_t answered = 0;                  // cards answered in this session, drives the progress bar
    bool extra_mode = false;                // the learner asked for more after finishing today's goal

    double est_base = kGuessedNow;          // clock estimate used while the device has no real time
    double est_mono = 0;

    // ---------------------------------------------------------------- time
    uint32_t Now() {
        const time_t t = std::time(nullptr);
        if (t > 1'600'000'000) return static_cast<uint32_t>(t);
        return static_cast<uint32_t>(est_base + (esp_timer_get_time() / 1e6 - est_mono));
    }

    // ------------------------------------------------------------- loading
    void LoadAll() {
        ready = false;
        card = -1;
        pack.Close();
        if (!MountStore()) return;
        installed = ReadInstalledPack();
        if (!installed.valid || !pack.Open(PackDir()) || pack.Count() != installed.count) {
            installed = {};
            return;
        }
        mkdir((std::string(kStoreMount) + "/study").c_str(), 0775);
        if (!progress.Open(std::string(kStoreMount) + "/study", installed.deck_id, pack.Count())) return;
        est_base = progress.last_time() ? progress.last_time() : kGuessedNow;
        est_mono = esp_timer_get_time() / 1e6;
        ready = pack.Count() > 0;
    }

    // ----------------------------------------------------------- rendering
    QueueStats CurrentStats(uint32_t now) {
        return ready ? Stats(progress, now, progress.NewToday(now), CONFIG_EL_OFFLINE_NEW_PER_DAY) : QueueStats{};
    }

    void Render() {
        if (!view.built()) return;
        const uint32_t now = Now();
        const QueueStats st = CurrentStats(now);
        const uint32_t remaining = st.learning + st.due + st.new_left;

        HeaderInfo header;
        header.learning = st.learning;
        header.due = st.due;
        header.fresh = st.new_left;
        header.progress_pct = (answered + remaining) ? static_cast<int>(answered * 100 / (answered + remaining)) : 0;

        std::string tag_storage;
        switch (screen) {
            case Screen::kFront:
            case Screen::kBack: {
                const TagStyle t = TagFor(kind);
                header.tag = t.text;
                header.tag_color = t.color;
                break;
            }
            case Screen::kMenu: header.tag = "MENU"; break;
            case Screen::kBusy: header.tag = "ĐỒNG BỘ"; header.show_counters = false; break;
            case Screen::kDone: header.tag = "XONG"; header.progress_pct = 100; break;
            case Screen::kNotice:
                tag_storage = notice_tag;
                header.tag = tag_storage.c_str();
                header.tag_color = notice_icon == Icon::kWarn ? ui::kLearn : ui::kExtra;
                header.show_counters = ready;
                break;
        }
        view.SetHeader(header);

        switch (screen) {
            case Screen::kFront:
                view.ShowFront(data);
                view.SetHints({{Glyph::kClick, "Xem nghĩa", 0}, {Glyph::kDouble, "Nghe lại", 0}, {Glyph::kHold, "Menu", 0}});
                break;
            case Screen::kBack:
                view.ShowBack(data);
                view.SetHints({{Glyph::kClick, "Nhớ", ui::kGood}, {Glyph::kDouble, "Quên", ui::kAgain},
                               {Glyph::kHold, "Dễ", ui::kEasy}});
                break;
            case Screen::kMenu:
                view.ShowMenu(kMenuItems, 3, menu_sel);
                view.SetHints({{Glyph::kClick, "Mục kế", 0}, {Glyph::kHold, "Chọn", 0}, {Glyph::kDouble, "Quay lại", 0}});
                break;
            case Screen::kNotice:
                view.ShowNotice(notice_icon, notice_title, notice_body);
                view.SetHints({{Glyph::kClick, notice_resumes ? "Tiếp tục" : "Mở menu", 0}});
                break;
            case Screen::kDone: {
                std::string body = answered ? "Đã ôn " + std::to_string(answered) + " thẻ trong lần học này"
                                            : "Không còn thẻ nào đến hạn";
                if (ready && st.fresh == 0) body += "\nBạn đã học hết các từ trong gói.";
                view.ShowNotice(Icon::kOk, "Hôm nay bạn đã học xong!", body);
                view.SetHints({{Glyph::kClick, "Ôn thêm", 0}, {Glyph::kHold, "Menu", 0}});
                break;
            }
            case Screen::kBusy:
                view.SetHints({});
                break;      // ShowBusy() is called by RunSync()
        }
    }

    void ShowNotice(Icon icon, const std::string& tag, const std::string& title, const std::string& body, bool resumes) {
        screen = Screen::kNotice;
        notice_icon = icon;
        notice_tag = tag;
        notice_title = title;
        notice_body = body;
        notice_resumes = resumes;
        Render();
    }

    // ---------------------------------------------------------------- flow
    void PlayAudio(int index) {
        std::vector<uint8_t> clip;
        if (index < 0 || !pack.ReadAudio(index, clip)) return;
        Application::GetInstance().GetAudioService().PlaySound(
            std::string_view(reinterpret_cast<const char*>(clip.data()), clip.size()));
    }

    void ShowNext(int avoid) {
        if (!ready) {
            ShowNotice(Icon::kWarn, "CHƯA CÓ GÓI", "Chưa có gói học",
                       "Kết nối Wi-Fi rồi chọn \"Đồng bộ ngay\" trong menu để tải về.", false);
            return;
        }
        const uint32_t now = Now();
        const QueueStats st = CurrentStats(now);
        if (!extra_mode && st.learning + st.due == 0 && st.new_left == 0) {      // today's goal is complete
            card = -1;
            screen = Screen::kDone;
            Render();
            return;
        }
        Pick pick = NextCard(progress, now, progress.NewToday(now), CONFIG_EL_OFFLINE_NEW_PER_DAY, avoid);
        if (pick.kind == PickKind::kNone || !pack.GetCard(pick.card, data)) {
            card = -1;
            screen = Screen::kDone;
            Render();
            return;
        }
        card = pick.card;
        kind = pick.kind;
        screen = Screen::kFront;
        Render();
        PlayAudio(card);
    }

    void Answer(int quality) {
        if (!progress.Record(card, quality, Now())) ESP_LOGE(TAG, "Could not record the review");
        ++answered;
        const std::string when = FormatInterval(progress.At(card).interval_min);
        ShowNext(card);
        if (quality == 0) view.Toast("Quên · " + when, ui::kAgain);
        else if (quality >= 5) view.Toast("Dễ · " + when, ui::kEasy);
        else view.Toast("Nhớ · " + when, ui::kGood);
    }

    void RunSync() {
        screen = Screen::kBusy;
        Render();
        view.ShowBusy("Đang đồng bộ", "Đang kết nối với server...", -1);
        auto status_cb = [this](const std::string& s) {
            int pct = -1;
            const size_t pos = s.rfind('%');
            if (pos != std::string::npos) {
                size_t begin = pos;
                while (begin > 0 && std::isdigit(static_cast<unsigned char>(s[begin - 1]))) --begin;
                if (begin < pos) pct = std::atoi(s.substr(begin, pos - begin).c_str());
            }
            view.SetBusy(s, pct);
        };
        SyncOutcome pack_result = InstallPack(status_cb);
        if (pack_result.ok && pack_result.pack_changed) LoadAll();
        std::string text = pack_result.message;
        bool ok = pack_result.ok;
        if (pack_result.ok && ready) {
            SyncOutcome progress_result = ExchangeProgress(progress, installed, status_cb);
            ok = progress_result.ok;
            text += "\n" + progress_result.message;
        }
        card = -1;
        extra_mode = false;
        ShowNotice(ok ? Icon::kOk : Icon::kWarn, ok ? "ĐÃ ĐỒNG BỘ" : "LỖI ĐỒNG BỘ",
                   ok ? "Đồng bộ xong" : "Chưa đồng bộ được", text, ready);
    }

    void Exit() {
        progress.Save();
        pack.Close();
        view.Destroy();
        screen = Screen::kNotice;
    }

    // Back to the card that was on screen, or fetch the next one.
    void Resume() {
        if (card >= 0) { screen = Screen::kFront; Render(); }
        else ShowNext(-1);
    }

    // ------------------------------------------------------------- gestures
    // Returns false when the study mode was left.
    bool Handle(Gesture g) {
        switch (screen) {
            case Screen::kFront:
                if (g == Gesture::kClick) { screen = Screen::kBack; Render(); }
                else if (g == Gesture::kDouble) PlayAudio(card);
                else { menu_sel = 0; screen = Screen::kMenu; Render(); }
                break;
            case Screen::kBack:
                Answer(g == Gesture::kClick ? 4 : g == Gesture::kDouble ? 0 : 5);
                break;
            case Screen::kMenu:
                if (g == Gesture::kClick) { menu_sel = (menu_sel + 1) % 3; Render(); }
                else if (g == Gesture::kDouble || menu_sel == 0) Resume();
                else if (menu_sel == 1) RunSync();
                else return false;
                break;
            case Screen::kNotice:
                if (notice_resumes) { ShowNext(-1); }
                else { menu_sel = 0; screen = Screen::kMenu; Render(); }
                break;
            case Screen::kDone:
                if (g == Gesture::kLong) { menu_sel = 0; screen = Screen::kMenu; Render(); }
                else if (g == Gesture::kClick) { extra_mode = true; ShowNext(-1); }
                break;
            case Screen::kBusy:
                break;
        }
        return true;
    }

    void OnGesture(Gesture g, bool& active) {
        if (g == Gesture::kClick) {
            if (pending_click) {                       // second click inside the window = double click
                pending_click = false;
                last_double_us = esp_timer_get_time();
                active = Handle(Gesture::kDouble);
            } else {
                pending_click = true;
            }
        } else if (g == Gesture::kDouble) {
            pending_click = false;
            if (esp_timer_get_time() - last_double_us < kDuplicateDoubleUs) return;
            last_double_us = esp_timer_get_time();
            active = Handle(Gesture::kDouble);
        } else {
            pending_click = false;
            active = Handle(Gesture::kLong);
        }
    }

    void Begin(bool& active) {
        if (active) return;
        LoadAll();
        answered = 0;
        extra_mode = false;
        view.Build();
        active = true;
        if (ready) {
            ShowNext(-1);
        } else {
            ShowNotice(Icon::kWarn, "CHƯA CÓ GÓI", "Chưa có gói học", "Đang thử tải từ server...", false);
            RunSync();
        }
    }
};

namespace {

StudyImpl& Impl() {
    static StudyImpl impl;
    return impl;
}

void TaskMain(void*) {
    StudyImpl& s = Impl();
    OfflineStudy& owner = OfflineStudy::GetInstance();
    bool active = false;
    Msg m;
    for (;;) {
        const TickType_t wait = s.pending_click ? pdMS_TO_TICKS(kDoubleWindowMs) : portMAX_DELAY;
        const bool was_active = active;
        if (xQueueReceive(s.queue, &m, wait) != pdTRUE) {      // no second click came: it was a single click
            s.pending_click = false;
            if (active) active = s.Handle(Gesture::kClick);
        } else if (m.type == Msg::kStart) {
            s.Begin(active);
        } else if (active) {
            s.OnGesture(m.gesture, active);
        }
        if (was_active && !active) s.Exit();                   // the learner chose "Thoát"
        owner.MarkActive(active);
    }
}

}  // namespace

OfflineStudy& OfflineStudy::GetInstance() {
    static OfflineStudy instance;
    return instance;
}

void OfflineStudy::Start() {
    StudyImpl& s = Impl();
    if (!task_started_) {
        s.queue = xQueueCreate(8, sizeof(Msg));
        task_started_ = xTaskCreate(TaskMain, "offline_study", 12288, nullptr, 3, nullptr) == pdPASS;
        if (!task_started_) { ESP_LOGE(TAG, "Cannot start the study task"); return; }
    }
    active_ = true;                 // the button callbacks route to us from now on
    Msg m{Msg::kStart, Gesture::kClick};
    xQueueSend(s.queue, &m, 0);
}

void OfflineStudy::Post(Gesture gesture) {
    StudyImpl& s = Impl();
    if (!s.queue) return;
    Msg m{Msg::kGesture, gesture};
    xQueueSend(s.queue, &m, 0);
}

}  // namespace offline
