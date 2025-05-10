#ifndef SOFTAP_PROVISIONING_H
#define SOFTAP_PROVISIONING_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stdint.h> // For uint32_t

/**
 * @brief 啟動 SoftAP 配網流程
 *
 * 此函數會啟動一個 SoftAP 熱點和一個 HTTP 伺服器，
 * 等待使用者透過瀏覽器提交 Wi-Fi 憑證。
 * 成功收到並儲存憑證後函數返回。
 * 注意：此函數內部會管理 Wi-Fi 的啟動(AP模式)和停止。
 *
 * @param timeout_ms 等待使用者設定的超時時間 (毫秒)，0 或 portMAX_DELAY 表示永不超時。
 *
 * @return esp_err_t
 * - ESP_OK: 成功收到並儲存 Wi-Fi 憑證。
 * - ESP_ERR_TIMEOUT: 等待使用者設定超時。
 * - ESP_FAIL: 過程中發生其他錯誤 (例如無法啟動 SoftAP 或 Server)。
 */
esp_err_t start_softap_provisioning(TickType_t timeout_ticks); // 改用 TickType_t

#endif // SOFTAP_PROVISIONING_H