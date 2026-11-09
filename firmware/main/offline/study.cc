#include "study.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "application.h"
#include "backlight.h"
#include "board.h"
#include "card_queue.h"
#include "display.h"
#include "pack.h"
#include "progress.h"
#include "starter_pack.h"
#include "storage.h"
#include "study_view.h"
#include "sync.h"
#include "text_util.h"
#if CONFIG_BOARD_TYPE_ENGLISH_LEARNER
#include "home/home_screen.h"
#endif

#define TAG "OfflineStudy"

#ifndef CONFIG_EL_OFFLINE_NEW_PER_DAY
#define CONFIG_EL_OFFLINE_NEW_PER_DAY 10
#endif

namespace offline {

namespace {

constexpr int kDoubleWindowMs = 320;            // a click is held this long to see whether a second one follows
constexpr int64_t kDuplicateDoubleUs = 450'000;  // the button driver may report a double click twice
constexpr uint32_t kGuessedNow = 1'700'000'000;  // starting point of the clock estimate when nothing better is known
constexpr size_t kFlushEvery = 10;               // reviews kept in RAM before they are written to the flash
const char* const kMenuItems[] = {"Tiếp tục học", "Đồng bộ (Wi-Fi)", "Thoát"};

enum class Screen { kFront, kBack, kMenu, kNotice, kDone, kBusy, kLetters, kList, kDetail };

struct Msg {
    enum Type : uint8_t { kStart, kGesture } type;
    Gesture gesture;
    Mode mode;
};

struct TagStyle { const char* text; uint32_t color; };
TagStyle TagFor(PickKind kind) {
    switch (kind) {
        case PickKind::kNew: return {"TỪ MỚI", tone::kNew};
        case PickKind::kLearning: return {"HỌC LẠI", tone::kLearn};
        case PickKind::kDue: return {"ÔN TẬP", tone::kDue};
        default: return {"ÔN THÊM", tone::kExtra};
    }
}

struct DictEntry {
    char word[24];
    uint16_t card;
};

// Darkens the screen while the flash is written (see Backlight::SetBlank). The panel is blanked first and lit
// again a moment after the write, when its last scan has finished.
struct QuietWrite {
    QuietWrite() {
        Board::GetInstance().GetBacklight()->SetBlank(true);
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    ~QuietWrite() {
        vTaskDelay(pdMS_TO_TICKS(60));
        Board::GetInstance().GetBacklight()->SetBlank(false);
    }
};

}  // namespace

struct StudyImpl {
    QueueHandle_t queue = nullptr;
    bool pending_click = false;
    int64_t last_double_us = 0;

    StudyView view;
    Mode mode = Mode::kNew;
    Screen screen = Screen::kNotice;
    int menu_sel = 0;

    // what the notice screen shows, and where a click leads
    Icon notice_icon = Icon::kWarn;
    std::string notice_tag = "THÔNG BÁO", notice_title, notice_body;
    bool notice_resumes = false;            // true: back to the cards, false: open the menu
    bool notice_exits = false;              // true: leave the programme

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

    // dictionary
    std::vector<DictEntry> dict;            // every word, sorted
    int letter_begin[27] = {};              // first entry of each letter; [26] = dict.size()
    bool has_letter[26] = {};
    int letter_sel = 0;                     // 0..25 letters, 26 = exit
    int list_sel = 0;                       // 0 = "back" row, 1.. = words of the letter
    int detail_entry = -1;                  // index into dict

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
        dict.clear();
        pack.Close();
        if (!MountStore()) return;
        // A pack downloaded from the server (in the store) wins; otherwise the starter pack inside the firmware is
        // used directly, with no copy in the store.
        installed = ReadInstalledPack();
        const bool server_pack = installed.valid && installed.pack_id.rfind("starter-", 0) != 0;
        if (!(server_pack && pack.Open(PackDir()) && pack.Count() == installed.count)) {
            const StarterPack& st = Starter();
            if (!pack.OpenMemory(st.cards, st.cards_size, st.cards_idx, st.cards_idx_size, st.audio, st.audio_size,
                                 st.audio_idx, st.audio_idx_size)) {
                installed = {};
                return;
            }
            installed = {true, st.id, 0, static_cast<uint32_t>(st.count)};
        }
        mkdir((std::string(kStoreMount) + "/study").c_str(), 0775);
        if (!progress.Open(std::string(kStoreMount) + "/study", installed.deck_id, pack.Count())) return;
        est_base = progress.last_time() ? progress.last_time() : kGuessedNow;
        est_mono = esp_timer_get_time() / 1e6;
        ready = pack.Count() > 0;
    }

    // One pass over the pack: every word with its card number, sorted for the dictionary.
    void BuildDictionary() {
        if (!dict.empty() || !ready) return;
        dict.reserve(pack.Count());
        pack.ForEachWord([this](size_t index, const std::string& word) {
            if (word.empty() || word[0] < 'a' || word[0] > 'z' || index > 0xFFFF) return;
            DictEntry e{};
            std::strncpy(e.word, word.c_str(), sizeof(e.word) - 1);
            e.card = static_cast<uint16_t>(index);
            dict.push_back(e);
        });
        std::sort(dict.begin(), dict.end(),
                  [](const DictEntry& a, const DictEntry& b) { return std::strcmp(a.word, b.word) < 0; });
        int pos = 0;
        for (int l = 0; l < 26; ++l) {
            letter_begin[l] = pos;
            while (pos < static_cast<int>(dict.size()) && dict[pos].word[0] == 'a' + l) ++pos;
            has_letter[l] = pos > letter_begin[l];
        }
        letter_begin[26] = pos;
    }

    int LetterCount(int letter) const { return letter_begin[letter + 1] - letter_begin[letter]; }

    // Writes the reviews kept in RAM to the flash with the screen darkened.
    void FlushQuietly() {
        if (progress.Deferred() == 0) return;
        QuietWrite quiet;
        if (!progress.Flush()) ESP_LOGE(TAG, "Could not save the reviews");
    }

    // ----------------------------------------------------------- rendering
    QueueStats CurrentStats(uint32_t now) {
        return ready ? Stats(progress, now, progress.NewToday(now), CONFIG_EL_OFFLINE_NEW_PER_DAY) : QueueStats{};
    }

    Focus CurrentFocus() const { return mode == Mode::kReview ? Focus::kReview : Focus::kNew; }

    uint32_t Remaining(const QueueStats& st) const {
        return mode == Mode::kReview ? st.learning + st.due : st.learning + st.new_left;
    }

    void Render() {
        if (!view.built()) return;
        const uint32_t now = Now();
        const QueueStats st = CurrentStats(now);
        const uint32_t remaining = Remaining(st);

        HeaderInfo header;
        if (!extra_mode && remaining > 0) header.right = "Còn " + std::to_string(remaining) + " từ";
        header.progress_pct =
            (answered + remaining) ? static_cast<int>(answered * 100 / (answered + remaining)) : 0;

        std::string tag_storage;
        switch (screen) {
            case Screen::kFront:
            case Screen::kBack: {
                const TagStyle t = TagFor(kind);
                header.tag = t.text;
                header.tag_color = t.color;
                break;
            }
            case Screen::kMenu: header.tag = "MENU"; header.progress_pct = -1; header.right.clear(); break;
            case Screen::kBusy:
                header.tag = "ĐỒNG BỘ";
                header.tag_color = tone::kNew;
                header.progress_pct = -1;
                header.right.clear();
                break;
            case Screen::kDone:
                header.tag = "XONG";
                header.tag_color = tone::kGood;
                header.progress_pct = 100;
                header.right.clear();
                break;
            case Screen::kNotice:
                tag_storage = notice_tag;
                header.tag = tag_storage.c_str();
                header.tag_color = notice_icon == Icon::kWarn ? tone::kLearn : tone::kGood;
                header.progress_pct = -1;
                header.right.clear();
                break;
            case Screen::kLetters:
            case Screen::kList:
            case Screen::kDetail:
                header.tag = "TỪ ĐIỂN";
                header.tag_color = tone::kDict;
                header.progress_pct = -1;
                header.right = std::to_string(dict.size()) + " từ";
                break;
        }
        view.SetHeader(header);

        switch (screen) {
            case Screen::kFront:
                view.ShowFront(data, pack.HasAudio(card) ? "Nghe và nhắc lại theo nhé!" : "Bạn nhớ nghĩa của từ này chưa?");
                view.SetHints({{Glyph::kClick, "Xem nghĩa", 0}, {Glyph::kDouble, "Nghe lại", 0}, {Glyph::kHold, "Menu", 0}});
                break;
            case Screen::kBack:
                view.ShowBack(data);
                view.SetHints({{Glyph::kClick, "Nhớ", tone::kGood}, {Glyph::kDouble, "Quên", tone::kAgain},
                               {Glyph::kHold, "Dễ", tone::kEasy}});
                break;
            case Screen::kMenu:
                view.ShowMenu(kMenuItems, 3, menu_sel);
                view.SetHints({{Glyph::kClick, "Mục kế", 0}, {Glyph::kHold, "Chọn", 0}, {Glyph::kDouble, "Quay lại", 0}});
                break;
            case Screen::kNotice:
                view.ShowNotice(notice_icon, notice_title, notice_body);
                view.SetHints({{Glyph::kClick, notice_exits ? "Xong" : notice_resumes ? "Tiếp tục" : "Mở menu", 0}});
                break;
            case Screen::kDone: {
                const bool review = mode == Mode::kReview;
                std::string title = review ? (answered ? "Ôn xong rồi!" : "Chưa có từ cần ôn")
                                           : "Xong từ mới hôm nay!";
                std::string body;
                if (answered) body = "Hôm nay bạn đã học " + std::to_string(answered) + " từ. Giỏi quá!";
                else body = review ? "Hôm nay không còn từ nào đến hạn ôn." : "Hôm nay bạn đã học đủ từ mới rồi.";
                if (!review && ready && st.fresh == 0) body += "\nBạn đã học hết các từ trong gói.";
                view.ShowNotice(Icon::kOk, title, body);
                view.SetHints({{Glyph::kClick, review ? "Ôn thêm" : "Học thêm", 0}, {Glyph::kHold, "Menu", 0}});
                break;
            }
            case Screen::kBusy:
                view.SetHints({});
                break;      // ShowBusy() is called by RunSync()
            case Screen::kLetters:
                view.ShowLetters(has_letter, letter_sel);
                view.SetHints({{Glyph::kClick, "Chữ kế", 0}, {Glyph::kDouble, "Chữ trước", 0}, {Glyph::kHold, "Chọn", 0}});
                break;
            case Screen::kList:
                RenderList();
                view.SetHints({{Glyph::kClick, "Từ kế", 0}, {Glyph::kDouble, "Từ trước", 0}, {Glyph::kHold, "Xem", 0}});
                break;
            case Screen::kDetail:
                view.ShowBack(data);
                view.SetHints({{Glyph::kClick, "Nghe lại", 0}, {Glyph::kDouble, "Quay lại", 0}, {Glyph::kHold, "Quay lại", 0}});
                break;
        }
    }

    void RenderList() {
        const int count = LetterCount(letter_sel);
        const int items = 1 + count;
        int first = list_sel - StudyView::kListRows / 2;
        first = std::max(0, std::min(first, items - StudyView::kListRows));
        std::vector<ListRow> rows;
        for (int i = first; i < items && static_cast<int>(rows.size()) < StudyView::kListRows; ++i) {
            ListRow row;
            if (i == 0) {
                row.title = "Về bảng chữ cái";
                row.special = true;
            } else {
                const DictEntry& e = dict[letter_begin[letter_sel] + i - 1];
                Card c;
                row.title = e.word;
                if (pack.GetCard(e.card, c)) row.subtitle = ShortMeaning(c.meaning, 38);
            }
            rows.push_back(std::move(row));
        }
        view.ShowList(rows, list_sel - first);
    }

    void ShowNotice(Icon icon, const std::string& tag, const std::string& title, const std::string& body, bool resumes,
                    bool exits = false) {
        screen = Screen::kNotice;
        notice_icon = icon;
        notice_tag = tag;
        notice_title = title;
        notice_body = body;
        notice_resumes = resumes;
        notice_exits = exits;
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
            ShowNotice(Icon::kWarn, "CHƯA CÓ TỪ", "Chưa có bộ từ", "Kết nối Wi-Fi rồi chọn \"Đồng bộ\" trong menu.", false);
            return;
        }
        const uint32_t now = Now();
        Pick pick = NextCardFor(progress, now, progress.NewToday(now), CONFIG_EL_OFFLINE_NEW_PER_DAY, avoid,
                                CurrentFocus(), extra_mode);
        if (pick.kind == PickKind::kNone || !pack.GetCard(pick.card, data)) {
            card = -1;
            FlushQuietly();
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
        // Deferred: a flash write pauses the cache and the RGB panel shows noise, so reviews are written in batches
        // (every kFlushEvery answers, and when the learner opens the menu, finishes, syncs or leaves).
        if (!progress.Record(card, quality, Now(), true)) ESP_LOGE(TAG, "Could not record the review");
        ++answered;
        const std::string when = FormatInterval(progress.At(card).interval_min);
        const bool flush_now = progress.Deferred() >= kFlushEvery;
        ShowNext(card);
        if (flush_now) FlushQuietly();
        if (quality == 0) view.Toast("Quên · " + when, tone::kAgain);
        else if (quality >= 5) view.Toast("Dễ · " + when, tone::kEasy);
        else view.Toast("Nhớ · " + when, tone::kGood);
    }

    void RunSync() {
        FlushQuietly();
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
        if (pack_result.ok && ready && installed.deck_id != 0) {        // deck 0 is the built-in starter pack
            SyncOutcome progress_result = ExchangeProgress(progress, installed, status_cb);
            ok = progress_result.ok;
            text += "\n" + progress_result.message;
        }
        card = -1;
        extra_mode = false;
        ShowNotice(ok ? Icon::kOk : Icon::kWarn, ok ? "ĐÃ ĐỒNG BỘ" : "LỖI ĐỒNG BỘ",
                   ok ? "Đồng bộ xong" : "Chưa đồng bộ được", text, ready, mode == Mode::kSync);
    }

    void Exit() {
        // Back to the home screen: it is prepared off-screen while this one stays visible, then the screen dims,
        // the reviews are written (only the journal: one small append; the snapshot is rewritten every
        // kSnapshotEvery reviews and Open() replays the journal on top of it) and the pages are flipped.
        const std::function<void()> change = [this] {
                pack.Close();
                dict.clear();
                view.Destroy();
                screen = Screen::kNotice;
                OfflineStudy::GetInstance().MarkActive(false);
#if CONFIG_BOARD_TYPE_ENGLISH_LEARNER
                home::HomeScreen::GetInstance().RefreshNow();
#endif
        };
        const std::function<void()> while_dark = [this] {
            if (!progress.Flush()) ESP_LOGE(TAG, "Could not save the reviews");
        };
        // drawing a page needs a deep stack: it is done by the LVGL task, this task waits
        ui::RunInLvglTask([&] { ui::SwitchPage(change, while_dark); });
    }

    // Back to where the learner was.
    void Resume() {
        if (mode == Mode::kDict) { screen = Screen::kLetters; Render(); }
        else if (card >= 0) { screen = Screen::kFront; Render(); }
        else ShowNext(-1);
    }

    void ShowStats() {
        const uint32_t now = Now();
        const QueueStats st = CurrentStats(now);
        size_t seen = 0;
        for (size_t i = 0; i < progress.Count(); ++i) {
            if (progress.At(i).flags & kSeen) ++seen;
        }
        std::string body = "Đã học: " + std::to_string(seen) + " từ\nCần ôn hôm nay: " +
                           std::to_string(st.learning + st.due) + " từ\nCòn mới: " + std::to_string(st.fresh) + " từ";
        ShowNotice(Icon::kOk, "THÀNH TÍCH", "Thành tích của bạn", body, false, true);
    }

    // ------------------------------------------------------------- gestures
    int NextLetter(int from, int step) const {
        for (int n = 1; n <= 27; ++n) {
            const int i = ((from + step * n) % 27 + 27) % 27;
            if (i == 26 || has_letter[i]) return i;
        }
        return 26;
    }

    // Returns false when the programme was left.
    bool Handle(Gesture g) {
        switch (screen) {
            case Screen::kFront:
                if (g == Gesture::kClick) { screen = Screen::kBack; Render(); }
                else if (g == Gesture::kDouble) PlayAudio(card);
                else { FlushQuietly(); menu_sel = 0; screen = Screen::kMenu; Render(); }
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
                if (notice_exits) return false;
                if (notice_resumes) Resume();
                else { menu_sel = 0; screen = Screen::kMenu; Render(); }
                break;
            case Screen::kDone:
                if (g == Gesture::kLong) { menu_sel = 0; screen = Screen::kMenu; Render(); }
                else if (g == Gesture::kClick) { extra_mode = true; ShowNext(-1); }
                break;
            case Screen::kBusy:
                break;
            case Screen::kLetters:
                if (g == Gesture::kClick) { letter_sel = NextLetter(letter_sel, 1); Render(); }
                else if (g == Gesture::kDouble) { letter_sel = NextLetter(letter_sel, -1); Render(); }
                else if (letter_sel == 26) return false;
                else { list_sel = 1; screen = Screen::kList; Render(); }
                break;
            case Screen::kList: {
                const int items = 1 + LetterCount(letter_sel);
                if (g == Gesture::kClick) { list_sel = (list_sel + 1) % items; Render(); }
                else if (g == Gesture::kDouble) { list_sel = (list_sel + items - 1) % items; Render(); }
                else if (list_sel == 0) { screen = Screen::kLetters; Render(); }
                else {
                    detail_entry = letter_begin[letter_sel] + list_sel - 1;
                    if (pack.GetCard(dict[detail_entry].card, data)) {
                        screen = Screen::kDetail;
                        Render();
                        PlayAudio(dict[detail_entry].card);
                    }
                }
                break;
            }
            case Screen::kDetail:
                if (g == Gesture::kClick) PlayAudio(dict[detail_entry].card);
                else { screen = Screen::kList; Render(); }
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

    void Begin(Mode m, bool& active) {
        if (active) return;
        mode = m;
        active = true;
        answered = 0;
        extra_mode = false;
        card = -1;
        {
            // The first screen of the programme is prepared off-screen while the home screen stays visible.
            const std::function<void()> change = [this] {
                view.Build();
                ShowNotice(Icon::kOk, "ĐANG MỞ", "Đang mở...", "", false);
#if CONFIG_BOARD_TYPE_ENGLISH_LEARNER
                home::HomeScreen::GetInstance().RefreshNow();       // hides the home screen
#endif
            };
            ui::RunInLvglTask([&] { ui::SwitchPage(change); });
        }
        LoadAll();
        if (mode == Mode::kSync) {
            RunSync();
        } else if (!ready) {
            ShowNotice(Icon::kWarn, "CHƯA CÓ TỪ", "Chưa có bộ từ", "Không mở được bộ nhớ. Hãy thử đồng bộ trong Cài đặt.",
                       false, true);
        } else if (mode == Mode::kStats) {
            ShowStats();
        } else if (mode == Mode::kDict) {
            BuildDictionary();
            letter_sel = NextLetter(-1, 1);
            screen = Screen::kLetters;
            Render();
        } else {
            ShowNext(-1);
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
            s.Begin(m.mode, active);
        } else if (active) {
            s.OnGesture(m.gesture, active);
        }
        if (was_active && !active) s.Exit();                   // the learner chose to leave
        owner.MarkActive(active);
    }
}

}  // namespace

OfflineStudy& OfflineStudy::GetInstance() {
    static OfflineStudy instance;
    return instance;
}

void OfflineStudy::Start(Mode mode) {
    StudyImpl& s = Impl();
    if (!task_started_) {
        s.queue = xQueueCreate(8, sizeof(Msg));
        task_started_ = xTaskCreate(TaskMain, "offline_study", 12288, nullptr, 3, nullptr) == pdPASS;
        if (!task_started_) { ESP_LOGE(TAG, "Cannot start the study task"); return; }
    }
    active_ = true;                 // the button callbacks route to us from now on
    Msg m{Msg::kStart, Gesture::kClick, mode};
    xQueueSend(s.queue, &m, 0);
}

void OfflineStudy::Post(Gesture gesture) {
    StudyImpl& s = Impl();
    if (!s.queue) return;
    Msg m{Msg::kGesture, gesture, Mode::kNew};
    xQueueSend(s.queue, &m, 0);
}

}  // namespace offline
