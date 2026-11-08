// Minimal declarations of the ESP-IDF / LVGL / firmware APIs used by main/offline/{storage,sync,study}.cc.
// They mirror the signatures the firmware itself uses (see ota.cc, application.cc, display/*.cc) so that
// `g++ -fsyntax-only` can catch typos and type errors in the glue code without ESP-IDF installed.
// This does NOT prove the glue links or runs on the device.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

// ---- ESP-IDF basics
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
const char* esp_err_to_name(esp_err_t);
#define ESP_LOGE(tag, fmt, ...) std::printf(fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) std::printf(fmt "\n", ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) std::printf(fmt "\n", ##__VA_ARGS__)
int64_t esp_timer_get_time();
void* heap_caps_malloc(size_t, uint32_t);
void heap_caps_free(void*);
constexpr uint32_t MALLOC_CAP_INTERNAL = 1;

// ---- FreeRTOS
using QueueHandle_t = void*;
using TickType_t = uint32_t;
using BaseType_t = int;
constexpr BaseType_t pdTRUE = 1, pdPASS = 1;
constexpr TickType_t portMAX_DELAY = 0xFFFFFFFF;
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
QueueHandle_t xQueueCreate(unsigned, unsigned);
BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t);
BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t);
BaseType_t xTaskCreate(void (*)(void*), const char*, uint32_t, void*, unsigned, void*);

// ---- FAT / wear levelling
using wl_handle_t = int32_t;
constexpr wl_handle_t WL_INVALID_HANDLE = -1;
struct esp_vfs_fat_mount_config_t {
    bool format_if_mount_failed;
    int max_files;
    size_t allocation_unit_size;
};
esp_err_t esp_vfs_fat_spiflash_mount_rw_wl(const char*, const char*, const esp_vfs_fat_mount_config_t*, wl_handle_t*);
esp_err_t esp_vfs_fat_info(const char*, uint64_t*, uint64_t*);

// ---- mbedtls
struct mbedtls_sha256_context { char opaque[128]; };
void mbedtls_sha256_init(mbedtls_sha256_context*);
int mbedtls_sha256_starts(mbedtls_sha256_context*, int);
int mbedtls_sha256_update(mbedtls_sha256_context*, const unsigned char*, size_t);
int mbedtls_sha256_finish(mbedtls_sha256_context*, unsigned char*);
void mbedtls_sha256_free(mbedtls_sha256_context*);

// ---- cJSON
struct cJSON {
    char* valuestring;
    int valueint;
    double valuedouble;
};
cJSON* cJSON_Parse(const char*);
void cJSON_Delete(cJSON*);
cJSON* cJSON_GetObjectItem(const cJSON*, const char*);
cJSON* cJSON_GetArrayItem(const cJSON*, int);
int cJSON_GetArraySize(const cJSON*);
bool cJSON_IsString(const cJSON*);
bool cJSON_IsNumber(const cJSON*);
bool cJSON_IsArray(const cJSON*);
char* cJSON_PrintUnformatted(const cJSON*);
void cJSON_free(void*);
cJSON* cJSON_Next(cJSON*);
#define cJSON_ArrayForEach(element, array) for (element = (array) ? cJSON_GetArrayItem(array, 0) : nullptr; element != nullptr; element = nullptr)

// ---- firmware classes (as used by ota.cc / application.cc / display code)
struct NetErr { std::string ToString() const; };
class Http {
public:
    void SetHeader(const char*, const char*);
    void SetContent(std::string&&);
    std::expected<void, NetErr> Open(const std::string& method, const std::string& url);
    std::expected<int, NetErr> GetStatusCode();
    size_t GetBodyLength();
    std::expected<int, NetErr> Read(char*, size_t);
    std::string ReadAll();
    void Close();
};
class NetworkInterface {
public:
    std::unique_ptr<Http> CreateHttp(int);
};
class Settings {
public:
    Settings(const std::string&, bool read_write = false);
    std::string GetString(const std::string&, const std::string& default_value = "");
};
struct SystemInfo { static std::string GetMacAddress(); };
class AudioService { public: void PlaySound(const std::string_view&); };
class Display { public: int width() const; int height() const; virtual bool Lock(int = 0) = 0; virtual void Unlock() = 0; virtual ~Display() = default; };
class DisplayLockGuard { public: explicit DisplayLockGuard(Display*); ~DisplayLockGuard(); };
class Board {
public:
    static Board& GetInstance();
    Display* GetDisplay();
    NetworkInterface* GetNetwork();
    std::string GetUuid();
};
class Application {
public:
    static Application& GetInstance();
    AudioService& GetAudioService();
};

// ---- LVGL (subset)
struct lv_obj_t;
struct lv_display_t;
struct lv_font_t { int dummy; };
struct lv_color_t { uint32_t rgb; };
using lv_align_t = int;
using lv_opa_t = int;
constexpr lv_align_t LV_ALIGN_TOP_LEFT = 1, LV_ALIGN_TOP_MID = 2, LV_ALIGN_TOP_RIGHT = 3, LV_ALIGN_BOTTOM_MID = 4;
constexpr int LV_TEXT_ALIGN_LEFT = 1, LV_TEXT_ALIGN_CENTER = 2, LV_TEXT_ALIGN_RIGHT = 3;
constexpr int LV_LABEL_LONG_WRAP = 0, LV_LABEL_LONG_DOT = 1;
constexpr lv_opa_t LV_OPA_COVER = 255;
constexpr int LV_OBJ_FLAG_HIDDEN = 1, LV_OBJ_FLAG_SCROLLABLE = 2;
extern const lv_font_t lv_font_montserrat_14;
#define LV_FONT_DEFAULT (&lv_font_montserrat_14)
lv_color_t lv_color_hex(uint32_t);
lv_display_t* lv_display_get_default();
int lv_display_get_horizontal_resolution(lv_display_t*);
int lv_display_get_vertical_resolution(lv_display_t*);
lv_obj_t* lv_layer_top();
lv_obj_t* lv_obj_create(lv_obj_t*);
void lv_obj_delete(lv_obj_t*);
void lv_obj_set_size(lv_obj_t*, int, int);
void lv_obj_set_width(lv_obj_t*, int);
void lv_obj_set_height(lv_obj_t*, int);
void lv_obj_set_pos(lv_obj_t*, int, int);
void lv_obj_align(lv_obj_t*, lv_align_t, int, int);
void lv_obj_set_style_bg_color(lv_obj_t*, lv_color_t, int);
void lv_obj_set_style_bg_opa(lv_obj_t*, lv_opa_t, int);
void lv_obj_set_style_border_width(lv_obj_t*, int, int);
void lv_obj_set_style_radius(lv_obj_t*, int, int);
void lv_obj_set_style_pad_all(lv_obj_t*, int, int);
void lv_obj_set_style_text_font(lv_obj_t*, const lv_font_t*, int);
void lv_obj_set_style_text_color(lv_obj_t*, lv_color_t, int);
void lv_obj_set_style_text_align(lv_obj_t*, int, int);
void lv_obj_add_flag(lv_obj_t*, int);
void lv_obj_remove_flag(lv_obj_t*, int);
lv_obj_t* lv_label_create(lv_obj_t*);
void lv_label_set_text(lv_obj_t*, const char*);
void lv_label_set_long_mode(lv_obj_t*, int);
constexpr int LV_FLEX_FLOW_ROW = 1, LV_FLEX_ALIGN_CENTER = 2, LV_SIZE_CONTENT = 0x1FFF, LV_RADIUS_CIRCLE = 0x7FFF;
constexpr int LV_SCROLLBAR_MODE_OFF = 0;
constexpr lv_opa_t LV_OPA_TRANSP = 0;
constexpr lv_align_t LV_ALIGN_LEFT_MID = 5, LV_ALIGN_RIGHT_MID = 6;
void lv_obj_set_scrollbar_mode(lv_obj_t*, int);
void lv_obj_set_flex_flow(lv_obj_t*, int);
void lv_obj_set_flex_align(lv_obj_t*, int, int, int);
void lv_obj_set_style_pad_left(lv_obj_t*, int, int);
void lv_obj_set_style_pad_right(lv_obj_t*, int, int);
void lv_obj_set_style_pad_column(lv_obj_t*, int, int);
void lv_obj_set_style_border_color(lv_obj_t*, lv_color_t, int);
void lv_obj_clean(lv_obj_t*);
void lv_obj_center(lv_obj_t*);
struct lv_timer_t;
lv_timer_t* lv_timer_create(void (*)(lv_timer_t*), uint32_t, void*);
void lv_timer_set_repeat_count(lv_timer_t*, int32_t);
void lv_timer_reset(lv_timer_t*);
void lv_timer_delete(lv_timer_t*);
void* lv_timer_get_user_data(lv_timer_t*);
