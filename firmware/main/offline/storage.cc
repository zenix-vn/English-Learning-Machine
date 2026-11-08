#include "storage.h"

#include <dirent.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sys/stat.h>

#include <cstdio>

#define TAG "OfflineStore"

namespace offline {

namespace {
bool g_mounted = false;
wl_handle_t g_wl = WL_INVALID_HANDLE;
}  // namespace

bool MountStore() {
    if (g_mounted) return true;
    esp_vfs_fat_mount_config_t cfg = {};
    cfg.format_if_mount_failed = true;      // first boot after flashing: the partition is blank
    cfg.max_files = 6;
    cfg.allocation_unit_size = 4096;
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(kStoreMount, "store", &cfg, &g_wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot mount the store partition: %s", esp_err_to_name(err));
        return false;
    }
    g_mounted = true;
    ESP_LOGI(TAG, "Store mounted: %llu of %llu bytes free", (unsigned long long)StoreFreeBytes(), (unsigned long long)StoreTotalBytes());
    return true;
}

bool StoreMounted() { return g_mounted; }

uint64_t StoreFreeBytes() {
    uint64_t total = 0, free_bytes = 0;
    return esp_vfs_fat_info(kStoreMount, &total, &free_bytes) == ESP_OK ? free_bytes : 0;
}

uint64_t StoreTotalBytes() {
    uint64_t total = 0, free_bytes = 0;
    return esp_vfs_fat_info(kStoreMount, &total, &free_bytes) == ESP_OK ? total : 0;
}

void RemoveDir(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::remove((dir + "/" + e->d_name).c_str());
    }
    closedir(d);
    rmdir(dir.c_str());
}

}  // namespace offline
