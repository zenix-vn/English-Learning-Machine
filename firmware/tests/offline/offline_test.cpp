// Host tests for the portable offline-study core (main/offline/{sm2,pack,progress,queue}).
// Build and run with tests/offline/run.sh. No ESP-IDF needed.
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../../main/offline/pack.h"
#include "../../main/offline/progress.h"
#include "../../main/offline/card_queue.h"
#include "../../main/offline/sm2.h"
#include "../../main/offline/text_util.h"

using namespace offline;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) { ++g_failed; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static std::string MakeTempDir() {
    char tmpl[] = "/tmp/eltestXXXXXX";
    return mkdtemp(tmpl);
}

static long FileSize(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? st.st_size : -1;
}

static bool ValidUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0 || i + extra >= s.size() + (extra == 0 ? 1 : 0)) return false;
        for (int k = 1; k <= extra; ++k) if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}

static void TestText() {
    std::printf("text: screen helpers\n");
    CHECK(Utf8Length("") == 0 && Utf8Length("abc") == 3 && Utf8Length("đạt được") == 8);

    const std::string shortm = "đạt được, giành được";
    CHECK(ShortMeaning(shortm, 50) == shortm);
    const std::string longm = "(n) hình dáng, cấu tạo (của một vật); kiểu, mẫu (sản phẩm); sự chế tạo, sản phẩm chế tạo; tính chất";
    std::string cut = ShortMeaning(longm, 60);
    CHECK(Utf8Length(cut) <= 63 && ValidUtf8(cut));
    CHECK(cut == "(n) hình dáng, cấu tạo (của một vật); kiểu, mẫu (sản phẩm)");      // cut at the last ';' that fits
    std::string nosep = ShortMeaning("một hai ba bốn năm sáu bảy tám chín mười mười một mười hai", 30);
    CHECK(ValidUtf8(nosep) && Utf8Length(nosep) <= 33 && nosep.size() > 3 && nosep.substr(nosep.size() - 3) == "...");
    CHECK(nosep.find("...") == nosep.size() - 3);                                      // ends on a word boundary
    for (size_t n = 1; n < 40; ++n) CHECK(ValidUtf8(ShortMeaning(longm, n)));          // never splits a character

    std::string en, vi;
    SplitExample("to say the lesson \xE2\x80\x94 đọc bài", en, vi);
    CHECK(en == "to say the lesson" && vi == "đọc bài");
    SplitExample("She achieved her goal.", en, vi);
    CHECK(en == "She achieved her goal." && vi.empty());

    CHECK(FormatInterval(0) == "gặp lại sau 1 phút");
    CHECK(FormatInterval(10) == "gặp lại sau 10 phút");
    CHECK(FormatInterval(600) == "gặp lại sau 10 giờ");
    CHECK(FormatInterval(1440) == "gặp lại sau 1 ngày");
    CHECK(FormatInterval(3 * 1440) == "gặp lại sau 3 ngày");
    CHECK(FormatInterval(90 * 1440) == "gặp lại sau 3 tháng");

    CHECK(WordFontSize(7, true) == 64 && WordFontSize(8, true) == 44 && WordFontSize(12, true) == 28);
    CHECK(WordFontSize(7, false) == 64 && WordFontSize(16, false) == 64 && WordFontSize(17, false) == 44 &&
          WordFontSize(25, false) == 28);
}

static void TestSm2Rules() {
    std::printf("sm2: fixed sequences\n");
    CardState c;
    const uint32_t t0 = 1'000'000;
    Grade(c, 4, t0);
    CHECK(c.reps == 1 && c.interval_min == 1440 && c.ease_milli == 2500 && c.due == t0 + 86400);
    Grade(c, 4, t0);
    CHECK(c.reps == 2 && c.interval_min == 3 * 1440);
    Grade(c, 4, t0);
    CHECK(c.reps == 3 && c.interval_min == 10800);            // 3 days * 2.5
    Grade(c, 5, t0);
    CHECK(c.ease_milli == 2600);                              // easy raises the ease factor by 0.1
    Grade(c, 0, t0 + 5);
    CHECK(c.reps == 0 && c.interval_min == 10 && c.due == t0 + 5 + 600 && IsLearning(c));
    CardState hard;
    for (int i = 0; i < 40; ++i) Grade(hard, 3, t0);
    CHECK(hard.ease_milli == 1300);                           // never below 1.3
    CHECK(c.flags & kSeen);
}

// backend/app/tools.py apply_grade() is the reference. cases.txt is generated from the real function.
static void TestSm2AgainstServer(const std::string& cases) {
    std::printf("sm2: parity with the server implementation\n");
    std::ifstream in(cases);
    CHECK(in.good());
    std::string line;
    int n = 0, bad = 0;
    while (std::getline(in, line)) {
        const size_t bar = line.find('|');
        if (bar == std::string::npos) continue;
        std::stringstream qs(line.substr(0, bar));
        std::stringstream want(line.substr(bar + 1));
        long want_interval, want_ease, want_reps;
        want >> want_interval >> want_ease >> want_reps;
        CardState c;
        std::string tok;
        while (std::getline(qs, tok, ',')) Grade(c, std::atoi(tok.c_str()), 0);
        const double tol = std::fmax(2.0, 0.002 * want_interval);
        const bool ok = std::fabs(static_cast<double>(c.interval_min) - want_interval) <= tol &&
                        std::labs(static_cast<long>(c.ease_milli) - want_ease) <= 1 && c.reps == want_reps;
        if (!ok) {
            if (++bad <= 3) std::printf("  mismatch: %s -> device %u min / %u / %u, server %ld / %ld / %ld\n", line.c_str(),
                                         c.interval_min, c.ease_milli, c.reps, want_interval, want_ease, want_reps);
        }
        ++n;
    }
    std::printf("  %d sequences compared, %d mismatches\n", n, bad);
    CHECK(n >= 100);
    CHECK(bad == 0);
}

static void TestPack(const std::string& pack_dir) {
    std::printf("pack: reading a pack built by the server\n");
    PackReader pack;
    CHECK(pack.Open(pack_dir));
    CHECK(pack.Count() == 60);
    Card card;
    CHECK(pack.GetCard(0, card));
    CHECK(card.word == "say" && card.ipa == "/sei/" && !card.meaning.empty() && !card.example.empty());
    CHECK(pack.GetCard(59, card) && !card.word.empty());
    CHECK(!pack.GetCard(60, card));
    int with_audio = 0, ogg = 0;
    std::vector<uint8_t> clip;
    for (size_t i = 0; i < pack.Count(); ++i) {
        if (!pack.HasAudio(i)) continue;
        ++with_audio;
        if (pack.ReadAudio(i, clip) && clip.size() > 100 && std::memcmp(clip.data(), "OggS", 4) == 0) ++ogg;
    }
    CHECK(with_audio == 60 && ogg == 60);
    PackReader missing;
    CHECK(!missing.Open("/nonexistent/pack"));
}

static void TestProgress() {
    std::printf("progress: journal, snapshot and crash recovery\n");
    const std::string dir = MakeTempDir();
    const uint32_t key = 1234, now = 5'000'000;
    Progress p;
    CHECK(p.Open(dir, key, 100));
    for (int i = 0; i < 3; ++i) CHECK(p.Record(i, 4, now + i));
    CHECK(p.NewToday(now) == 3);

    // power cut before any snapshot: the journal alone must bring the state back
    Progress q;
    CHECK(q.Open(dir, key, 100));
    CHECK(q.At(0).reps == 1 && q.At(1).reps == 1 && q.At(2).reps == 1 && q.At(3).reps == 0);
    CHECK(q.PendingUpload().size() == 3);

    for (int i = 0; i < 30; ++i) CHECK(q.Record(10 + i % 5, i % 4 == 0 ? 0 : 4, now + 100 + i));   // crosses a snapshot
    Progress r;
    CHECK(r.Open(dir, key, 100));
    bool same = true;
    for (size_t i = 0; i < 100; ++i) same &= std::memcmp(&q.At(i), &r.At(i), sizeof(CardState)) == 0;
    CHECK(same);
    CHECK(r.PendingUpload().size() == 33);

    // a torn record at the end of the journal (power cut while writing) is ignored
    FILE* f = std::fopen((dir + "/events.log").c_str(), "ab");
    std::fwrite("xyz", 1, 3, f);
    std::fclose(f);
    Progress torn;
    CHECK(torn.Open(dir, key, 100));
    CHECK(torn.PendingUpload().size() == 33);

    // upload, then compaction once the journal is long
    for (int i = 0; i < 1100; ++i) CHECK(torn.Record(20 + i % 30, 4, now + 1000 + i));
    CHECK(FileSize(dir + "/events.log") > 8000);
    CHECK(torn.MarkUploaded());
    CHECK(FileSize(dir + "/events.log") == 0 && torn.PendingUpload().empty());
    CHECK(torn.Record(77, 5, now + 5000));
    Progress after;
    CHECK(after.Open(dir, key, 100));
    CHECK(after.PendingUpload().size() == 1 && after.PendingUpload()[0].card == 77);
    CHECK(std::memcmp(&torn.At(25), &after.At(25), sizeof(CardState)) == 0);
    CHECK(after.At(77).ease_milli == 2600);

    // crash after the journal was emptied but before the final snapshot: the old snapshot says "applied=N"
    {
        Progress c1;
        CHECK(c1.Open(dir, key, 100));
        CHECK(c1.Save());                                   // snapshot with applied == journal count
        std::remove((dir + "/events.log").c_str());         // journal emptied by compaction, snapshot not rewritten yet
        Progress c2;
        CHECK(c2.Open(dir, key, 100));
        CHECK(std::memcmp(&c1.At(77), &c2.At(77), sizeof(CardState)) == 0);
        CHECK(c2.PendingUpload().empty());
        CHECK(c2.Record(5, 4, now + 9000));
        Progress c3;
        CHECK(c3.Open(dir, key, 100));
        CHECK(c3.At(5).reps == 1 && c3.At(77).reps == c1.At(77).reps);
    }

    // the same deck rebuilt with more cards: progress is kept, new cards start fresh
    {
        Progress grown;
        CHECK(grown.Open(dir, key, 150));
        CHECK(grown.Count() == 150 && grown.At(77).ease_milli == 2600 && grown.At(120).flags == 0);
        CHECK(grown.PendingUpload().size() == 1);
        CHECK(grown.Record(140, 4, now + 9500));
        Progress again;
        CHECK(again.Open(dir, key, 150));
        CHECK(again.At(140).reps == 1 && again.At(77).ease_milli == 2600);
        Progress shrunk;
        CHECK(shrunk.Open(dir, key, 60));                 // fewer cards: cards beyond the end are dropped
        CHECK(shrunk.Count() == 60 && shrunk.At(5).reps == 1);
    }

    // another deck installed: progress is discarded
    Progress other;
    CHECK(other.Open(dir, key + 1, 100));
    CHECK(other.At(0).flags == 0 && other.PendingUpload().empty());
}

static void TestQueue() {
    std::printf("queue: ordering\n");
    const std::string dir = MakeTempDir();
    Progress p;
    CHECK(p.Open(dir, 1, 20));
    const uint32_t now = 10'000'000;
    // nothing studied: new cards in deck order, up to the daily limit
    Pick pick = NextCard(p, now, 0, 3);
    CHECK(pick.kind == PickKind::kNew && pick.card == 0);
    for (int i = 0; i < 3; ++i) { auto n = NextCard(p, now, p.NewToday(now), 3); CHECK(n.kind == PickKind::kNew); p.Record(n.card, 4, now); }
    // limit reached: the new cards are not due, so extra practice (the soonest due card)
    pick = NextCard(p, now, p.NewToday(now), 3);
    CHECK(pick.kind == PickKind::kExtra);
    // a card forgotten earlier becomes a due "learning" card and beats reviews and new cards
    p.Mutable(1).due = now - 10; p.Mutable(1).interval_min = 10;                  // learning, due
    p.Mutable(2).due = now - 5000; p.Mutable(2).interval_min = 3 * 1440;          // review, due earlier
    pick = NextCard(p, now, p.NewToday(now), 3);
    CHECK(pick.kind == PickKind::kLearning && pick.card == 1);
    pick = NextCard(p, now, p.NewToday(now), 3, 1);                                // just answered: avoid it
    CHECK(pick.kind == PickKind::kDue && pick.card == 2);
    QueueStats s = Stats(p, now, p.NewToday(now), 3);
    CHECK(s.learning == 1 && s.due == 1 && s.fresh == 17 && s.new_left == 0);
    // next day the allowance renews
    CHECK(p.NewToday(now + 86400 * 2) == 0);
    CHECK(Stats(p, now + 86400 * 2, 0, 3).new_left == 3);
    // an empty deck
    Progress empty;
    CHECK(empty.Open(MakeTempDir(), 2, 0));
    CHECK(NextCard(empty, now, 0, 5).kind == PickKind::kNone);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: %s <pack_dir> <sm2_cases.txt>\n", argv[0]); return 2; }
    TestText();
    TestSm2Rules();
    TestSm2AgainstServer(argv[2]);
    TestPack(argv[1]);
    TestProgress();
    TestQueue();
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
