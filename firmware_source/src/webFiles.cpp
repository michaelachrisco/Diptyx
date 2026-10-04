#include "webFiles.h"

#include <string>

#include "device.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "webFilesServer.h"
#include "webFilesWifi.h"

static const char* TAG = "WEB_FILES";

namespace {
constexpr int64_t kSessionTimeoutUs = 30LL * 60LL * 1000000LL;
constexpr int64_t kPowerHoldUs = 3000000LL;

void showMessage(const std::string& message) {
    auto& device = Device::getInstance();
    if (device.notificationHandler) {
        device.notificationHandler->drawNotification(message);
    }
}

void shutdownFromWebMode() {
    auto& device = Device::getInstance();
    showMessage("POWER OFF");
    vTaskDelay(pdMS_TO_TICKS(500));
    if (device.renderer) device.renderer->deepSleep();
    Device::shutdown();
}
}

void WebFiles::run() {
    auto& device = Device::getInstance();

    if (!device.sd || !device.sd->mountedSuccesfully) {
        showMessage("SD card unavailable");
        vTaskDelay(pdMS_TO_TICKS(1200));
        if (device.menuHandler) device.menuHandler->drawMenu();
        return;
    }

    device.clearButtonLatches();
    // Do not treat the button press that selected Web Files as an exit press.
    while (gpio_get_level(PAGE_LEFT_BUTTON) == 0 || gpio_get_level(MIDDLE_BUTTON) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    WebFilesWifi wifi;
    if (wifi.start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi AP");
        showMessage("WiFi start failed");
        vTaskDelay(pdMS_TO_TICKS(1200));
        if (device.menuHandler) device.menuHandler->drawMenu();
        return;
    }

    WebFilesServer server;
    if (!server.start()) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        wifi.stop();
        showMessage("Web server failed");
        vTaskDelay(pdMS_TO_TICKS(1200));
        if (device.menuHandler) device.menuHandler->drawMenu();
        return;
    }

    const std::string ip = wifi.ipAddress();
   showMessage(
    std::string("WEB ") +
    wifi.ssid() +
    " " +
    ip +
    " PW:" +
    wifi.password()
);

    const int64_t startUs = esp_timer_get_time();
    int64_t powerPressedAt = -1;

    while (!server.stopRequested()) {
        vTaskDelay(pdMS_TO_TICKS(50));

        if (esp_timer_get_time() - startUs >= kSessionTimeoutUs) {
            ESP_LOGI(TAG, "Web Files session timed out");
            break;
        }

        // Page-left or middle button provides a physical way back to the menu.
        if (gpio_get_level(PAGE_LEFT_BUTTON) == 0 || gpio_get_level(MIDDLE_BUTTON) == 0) {
            break;
        }

        // Preserve the existing long-power-button shutdown behavior while the
        // main task is occupied by this modal web session.
        if (gpio_get_level(GPIO_NUM_42) != 0) {
            if (powerPressedAt < 0) powerPressedAt = esp_timer_get_time();
            if (esp_timer_get_time() - powerPressedAt >= kPowerHoldUs) {
                server.stop();
                wifi.stop();
                shutdownFromWebMode();
                return;
            }
        } else {
            powerPressedAt = -1;
        }
    }

    const bool changed = server.filesChanged();
    server.stop();
    wifi.stop();

    if (changed) {
        // Rebooting gives the existing BookHandler a clean startup scan. This
        // deliberately avoids touching the existing library/indexing code.
        showMessage("FILES CHANGED - RESTARTING");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        return;
    }

    if (device.menuHandler) device.menuHandler->drawMenu();
}
