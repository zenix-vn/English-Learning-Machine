#include "sync.h"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <psa/crypto.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <vector>

#include "board.h"
#include "settings.h"
#include "storage.h"
#include "system_info.h"

#define TAG "OfflineSync"

namespace offline {

namespace {

constexpr const char* kPackFiles[] = {"cards.bin", "cards.idx", "audio.bin", "audio.idx"};
constexpr size_t kChunk = 4096;
constexpr size_t kUploadBatch = 400;
constexpr uint64_t kSlack = 128 * 1024;          // room for the FAT bookkeeping and the progress files

struct Endpoint {
    std::string base;        // https://host
    std::string token;
    std::string device_id;
    std::string client_id;
};

// The WebSocket address and token were stored by the OTA check at boot (see Ota::CheckVersion).
bool LoadEndpoint(Endpoint& ep) {
    Settings settings("websocket", false);
    std::string url = settings.GetString("url");
    ep.token = settings.GetString("token");
    std::string scheme = "https://";
    size_t pos = url.find("wss://");
    if (pos == 0) {
        pos = 6;
    } else if ((pos = url.find("ws://")) == 0) {
        scheme = "http://";
        pos = 5;
    } else {
        return false;
    }
    size_t end = url.find('/', pos);
    ep.base = scheme + url.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    ep.device_id = SystemInfo::GetMacAddress();
    ep.client_id = Board::GetInstance().GetUuid();
    return !ep.token.empty();
}

std::unique_ptr<Http> Open(const Endpoint& ep, const char* method, const std::string& path, std::string body = "") {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    http->SetHeader("Device-Id", ep.device_id.c_str());
    http->SetHeader("Client-Id", ep.client_id.c_str());
    http->SetHeader("Authorization", ("Bearer " + ep.token).c_str());
    if (!body.empty()) {
        http->SetHeader("Content-Type", "application/json");
        http->SetContent(std::move(body));
    }
    if (auto opened = http->Open(method, ep.base + path); !opened) {
        ESP_LOGE(TAG, "%s %s failed: %s", method, path.c_str(), opened.error().ToString().c_str());
        return nullptr;
    }
    return http;
}

// Returns the HTTP status (0 when the request could not be made) and the response body.
int Request(const Endpoint& ep, const char* method, const std::string& path, std::string& response,
            std::string body = "") {
    auto http = Open(ep, method, path, std::move(body));
    if (!http) return 0;
    auto status = http->GetStatusCode();
    if (!status) return 0;
    response = http->ReadAll();
    http->Close();
    return *status;
}

std::string Hex(const unsigned char* data, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += d[data[i] >> 4]; s += d[data[i] & 15]; }
    return s;
}

// Streams one pack file to `dest`, checking size and sha256.
bool Download(const Endpoint& ep, const std::string& pack_id, const std::string& name, const std::string& dest,
              size_t size, const std::string& sha256, const StatusFn& status, const std::string& label) {
    for (int attempt = 1; attempt <= 3; ++attempt) {
        auto http = Open(ep, "GET", "/sync/file/" + pack_id + "/" + name);
        if (!http) continue;
        auto code = http->GetStatusCode();
        if (!code || *code != 200) {
            ESP_LOGW(TAG, "%s: attempt %d failed", name.c_str(), attempt);
            http->Close();
            continue;
        }
        FILE* f = std::fopen(dest.c_str(), "wb");
        if (!f) return false;
        std::unique_ptr<char, decltype(&heap_caps_free)> buf(
            static_cast<char*>(heap_caps_malloc(kChunk, MALLOC_CAP_INTERNAL)), heap_caps_free);
        // mbedtls 4 (ESP-IDF 6) dropped the legacy mbedtls_sha256_* API; use PSA Crypto.
        psa_crypto_init();  // idempotent
        psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
        size_t total = 0;
        int64_t last_report = 0;
        bool failed = !buf || psa_hash_setup(&sha, PSA_ALG_SHA_256) != PSA_SUCCESS;
        while (!failed) {
            auto got = http->Read(buf.get(), kChunk);
            if (!got) { failed = true; break; }
            if (*got == 0) break;
            if (psa_hash_update(&sha, reinterpret_cast<const uint8_t*>(buf.get()), *got) != PSA_SUCCESS) {
                failed = true;
                break;
            }
            if (std::fwrite(buf.get(), 1, *got, f) != static_cast<size_t>(*got)) { failed = true; break; }
            total += *got;
            const int64_t now = esp_timer_get_time();
            if (now - last_report > 1'000'000) {
                last_report = now;
                status(label + " " + std::to_string(total * 100 / (size ? size : 1)) + "%");
            }
        }
        unsigned char digest[32] = {};
        size_t digest_len = 0;
        if (failed || psa_hash_finish(&sha, digest, sizeof(digest), &digest_len) != PSA_SUCCESS ||
            digest_len != sizeof(digest)) {
            failed = true;
        }
        psa_hash_abort(&sha);
        std::fclose(f);
        http->Close();
        if (!failed && total == size && Hex(digest, 32) == sha256) return true;
        ESP_LOGW(TAG, "%s: bad download (got %u of %u bytes), attempt %d", name.c_str(), (unsigned)total,
                 (unsigned)size, attempt);
        std::remove(dest.c_str());
    }
    return false;
}

void SetClockFromServer(double server_time) {
    if (std::time(nullptr) > 1'600'000'000 || server_time < 1'600'000'000) return;
    struct timeval tv = {static_cast<time_t>(server_time), 0};
    settimeofday(&tv, nullptr);
}

uint64_t InstalledBytes() {
    uint64_t sum = 0;
    for (const char* name : kPackFiles) {
        struct stat st;
        if (stat((PackDir() + "/" + name).c_str(), &st) == 0) sum += st.st_size;
    }
    return sum;
}

SyncOutcome Fail(const std::string& message) { return {false, false, message}; }

}  // namespace

InstalledPack ReadInstalledPack() {
    InstalledPack out;
    FILE* f = std::fopen((PackDir() + "/manifest.json").c_str(), "rb");
    if (!f) return out;
    std::string text(2048, '\0');
    text.resize(std::fread(text.data(), 1, text.size(), f));
    std::fclose(f);
    if (cJSON* root = cJSON_Parse(text.c_str())) {
        cJSON* id = cJSON_GetObjectItem(root, "pack_id");
        cJSON* deck = cJSON_GetObjectItem(root, "deck_id");
        cJSON* count = cJSON_GetObjectItem(root, "count");
        if (cJSON_IsString(id) && cJSON_IsNumber(deck) && cJSON_IsNumber(count)) {
            out = {true, id->valuestring, static_cast<uint32_t>(deck->valueint), static_cast<uint32_t>(count->valueint)};
        }
        cJSON_Delete(root);
    }
    return out;
}

SyncOutcome InstallPack(const StatusFn& status) {
    if (!MountStore()) return Fail("Không mở được bộ nhớ lưu trữ");
    Endpoint ep;
    if (!LoadEndpoint(ep)) return Fail("Thiết bị chưa đăng ký với server. Hãy kết nối Wi-Fi và khởi động lại.");

    status("Đang kiểm tra gói học...");
    std::string body;
    const int code = Request(ep, "GET", "/sync/manifest", body);
    if (code == 0) return Fail("Không kết nối được server");
    if (code == 404) return Fail("Server chưa dựng gói học cho bộ từ của thiết bị");
    if (code == 403) return Fail("Thiết bị chưa được kích hoạt hoặc gói cước đã hết hạn");
    if (code != 200) return Fail("Server báo lỗi " + std::to_string(code));

    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(body.c_str()), cJSON_Delete);
    cJSON* pack = root ? cJSON_GetObjectItem(root.get(), "pack") : nullptr;
    cJSON* pack_id = pack ? cJSON_GetObjectItem(pack, "pack_id") : nullptr;
    cJSON* total_bytes = pack ? cJSON_GetObjectItem(pack, "total_bytes") : nullptr;
    cJSON* files = pack ? cJSON_GetObjectItem(pack, "files") : nullptr;
    if (!cJSON_IsString(pack_id) || !cJSON_IsNumber(total_bytes) || !cJSON_IsArray(files)) {
        return Fail("Phản hồi của server không hợp lệ");
    }
    if (cJSON* t = cJSON_GetObjectItem(root.get(), "server_time"); cJSON_IsNumber(t)) SetClockFromServer(t->valuedouble);

    const InstalledPack current = ReadInstalledPack();
    if (current.valid && current.pack_id == pack_id->valuestring) {
        return {true, false, "Gói học đã là bản mới nhất"};
    }

    const uint64_t need = static_cast<uint64_t>(total_bytes->valuedouble) + kSlack;
    const uint64_t have = StoreFreeBytes() + InstalledBytes();
    if (need > have) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "Bộ nhớ không đủ: cần %.1f MB, còn %.1f MB. Hãy dựng gói nhỏ hơn.",
                      need / 1048576.0, have / 1048576.0);
        return Fail(msg);
    }

    RemoveDir(PackTempDir());
    mkdir(PackTempDir().c_str(), 0775);
    int index = 0, n = cJSON_GetArraySize(files);
    cJSON* file = nullptr;
    cJSON_ArrayForEach(file, files) {
        ++index;
        cJSON* name = cJSON_GetObjectItem(file, "name");
        cJSON* size = cJSON_GetObjectItem(file, "size");
        cJSON* sha = cJSON_GetObjectItem(file, "sha256");
        if (!cJSON_IsString(name) || !cJSON_IsNumber(size) || !cJSON_IsString(sha)) return Fail("Phản hồi của server không hợp lệ");
        const std::string label = "Đang tải " + std::to_string(index) + "/" + std::to_string(n) + " (" + name->valuestring + ")";
        status(label);
        if (!Download(ep, pack_id->valuestring, name->valuestring, PackTempDir() + "/" + name->valuestring,
                      static_cast<size_t>(size->valuedouble), sha->valuestring, status, label)) {
            RemoveDir(PackTempDir());
            return Fail(std::string("Tải ") + name->valuestring + " thất bại");
        }
    }
    char* manifest = cJSON_PrintUnformatted(pack);
    FILE* mf = std::fopen((PackTempDir() + "/manifest.json").c_str(), "wb");
    bool wrote = manifest && mf && std::fwrite(manifest, 1, std::strlen(manifest), mf) == std::strlen(manifest);
    if (mf) std::fclose(mf);
    cJSON_free(manifest);
    if (!wrote) { RemoveDir(PackTempDir()); return Fail("Không ghi được manifest"); }

    RemoveDir(PackDir());
    if (std::rename(PackTempDir().c_str(), PackDir().c_str()) != 0) return Fail("Không thể cài gói học mới");
    return {true, true, "Đã cài gói học mới"};
}

SyncOutcome ExchangeProgress(Progress& progress, const InstalledPack& pack, const StatusFn& status) {
    Endpoint ep;
    if (!pack.valid || !LoadEndpoint(ep)) return Fail("Chưa có gói học hoặc thiết bị chưa đăng ký");

    // 1) progress made online (words taught by the tutor) for cards not yet studied here
    status("Đang nhận tiến độ từ server...");
    std::string body;
    int code = Request(ep, "GET", "/sync/state?pack=" + pack.pack_id, body);
    if (code == 0) return Fail("Không kết nối được server");
    if (code != 200) return Fail("Server báo lỗi " + std::to_string(code));
    int merged = 0;
    if (std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root{cJSON_Parse(body.c_str()), cJSON_Delete}) {
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, cJSON_GetObjectItem(root.get(), "items")) {
            if (cJSON_GetArraySize(item) != 5) continue;
            const int idx = cJSON_GetArrayItem(item, 0)->valueint;
            if (idx < 0 || static_cast<size_t>(idx) >= progress.Count()) continue;
            CardState& card = progress.Mutable(idx);
            if (card.flags & kSeen) continue;                  // what this device did offline wins
            card.due = static_cast<uint32_t>(cJSON_GetArrayItem(item, 1)->valuedouble);
            card.interval_min = static_cast<uint32_t>(cJSON_GetArrayItem(item, 2)->valuedouble * kDayMinutes + 0.5);
            card.ease_milli = static_cast<uint16_t>(cJSON_GetArrayItem(item, 3)->valuedouble * 1000 + 0.5);
            card.reps = static_cast<uint8_t>(cJSON_GetArrayItem(item, 4)->valueint);
            card.flags |= kSeen;
            ++merged;
        }
    }
    if (merged) progress.Save();

    // 2) reviews made offline
    const auto pending = progress.PendingUpload();
    for (size_t at = 0; at < pending.size(); at += kUploadBatch) {
        status("Đang gửi kết quả học (" + std::to_string(std::min(at + kUploadBatch, pending.size())) + "/" +
               std::to_string(pending.size()) + ")...");
        std::string payload = "{\"pack\":\"" + pack.pack_id + "\",\"events\":[";
        for (size_t i = at; i < std::min(at + kUploadBatch, pending.size()); ++i) {
            char one[48];
            std::snprintf(one, sizeof one, "%s[%u,%u,%u]", i > at ? "," : "", pending[i].card, pending[i].quality,
                          static_cast<unsigned>(pending[i].ts));
            payload += one;
        }
        payload += "]}";
        std::string reply;
        code = Request(ep, "POST", "/sync/progress", reply, std::move(payload));
        if (code != 200) return Fail(code == 0 ? "Không kết nối được server" : "Server từ chối kết quả học (" + std::to_string(code) + ")");
    }
    if (!pending.empty()) progress.MarkUploaded();
    char msg[96];
    std::snprintf(msg, sizeof msg, "Đã đồng bộ: nhận %d thẻ, gửi %u lượt ôn", merged, (unsigned)pending.size());
    return {true, false, msg};
}

}  // namespace offline
