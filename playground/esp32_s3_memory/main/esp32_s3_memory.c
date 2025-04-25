#include <stdio.h>
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_ota_ops.h"

void app_main(void)
{
    printf("========== ESP32 記憶體使用狀況 ==========\n");

    // --- SRAM（內部記憶體） ---
    size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    printf("📦 SRAM（內部）總容量：%d bytes\n", internal_total);
    printf("🟢 SRAM 剩餘可用：%d bytes\n", internal_free);

    // --- PSRAM（外部記憶體，如有） ---
    if (esp_psram_is_initialized()) {
        size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        printf("📦 PSRAM（外部）總容量：%d bytes\n", psram_total);
        printf("🟢 PSRAM 剩餘可用：%d bytes\n", psram_free);
    } else {
        printf("⚠️ 未偵測到 PSRAM\n");
    }

    // --- Flash 相關 ---
    esp_flash_t* flash_chip = NULL;
    esp_flash_init(flash_chip); // 初始化內建 flash
    uint32_t flash_size = 0;
    if (esp_flash_get_size(flash_chip, &flash_size) == ESP_OK) {
        printf("💾 Flash 總容量：%ld MB\n", flash_size / (1024 * 1024));
    } else {
        printf("⚠️ 無法取得 Flash 容量\n");
    }

    // --- 當前分區資訊（firmware 佔用 flash 多大） ---
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        printf("🔧 當前運行的 firmware 分區大小：%ld bytes\n", running->size);
    }

    printf("==========================================\n");
}
