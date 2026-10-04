#include "webFilesWifi.h"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "lwip/inet.h"

static const char* TAG = "WEB_FILES_WIFI";

WebFilesWifi::~WebFilesWifi() {
    stop();
}

esp_err_t WebFilesWifi::start() {
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        netifInitializedByUs_ = true;
    } else if (err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err == ESP_OK) {
        eventLoopCreatedByUs_ = true;
    } else if (err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    netif_ = esp_netif_create_default_wifi_ap();
    if (!netif_) return ESP_ERR_NO_MEM;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    wifiInitialized_ = true;

    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) return err;

    uint8_t mac[6] = {};
    err = esp_wifi_get_mac(WIFI_IF_AP, mac);
    if (err != ESP_OK) return err;

    char ssid[32] = {};
    std::snprintf(ssid, sizeof(ssid), "DIPTYX-%02X%02X", mac[4], mac[5]);
    ssid_ = ssid;

    // Fresh password every web session. It is shown on the e-paper screen.
    const uint32_t randomValue = esp_random();
    char password[16] = {};
    std::snprintf(password, sizeof(password), "%08lX",
                  static_cast<unsigned long>(randomValue));
    password_ = password;

    wifi_config_t apConfig = {};
    std::memcpy(apConfig.ap.ssid, ssid_.c_str(), ssid_.size());
    std::memcpy(apConfig.ap.password, password_.c_str(), password_.size());
    apConfig.ap.ssid_len = static_cast<uint8_t>(ssid_.size());
    apConfig.ap.channel = 1;
    apConfig.ap.max_connection = 3;
    apConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
    apConfig.ap.pmf_cfg.capable = true;
    apConfig.ap.pmf_cfg.required = false;

    err = esp_wifi_set_config(WIFI_IF_AP, &apConfig);
    if (err != ESP_OK) return err;

    err = esp_wifi_start();
    if (err != ESP_OK) return err;
    wifiStarted_ = true;

    ESP_LOGI(TAG, "AP started: SSID=%s", ssid_.c_str());
    return ESP_OK;
}

void WebFilesWifi::stop() {
    if (wifiStarted_) {
        esp_wifi_stop();
        wifiStarted_ = false;
    }

    if (wifiInitialized_) {
        esp_wifi_deinit();
        wifiInitialized_ = false;
    }

    if (netif_) {
        esp_netif_destroy_default_wifi(netif_);
        netif_ = nullptr;
    }

    if (eventLoopCreatedByUs_) {
        esp_event_loop_delete_default();
        eventLoopCreatedByUs_ = false;
    }

    if (netifInitializedByUs_) {
        esp_netif_deinit();
        netifInitializedByUs_ = false;
    }

    ssid_.clear();
    password_.clear();
}

std::string WebFilesWifi::ipAddress() const {
    if (!netif_) return {};

    esp_netif_ip_info_t info{};
    if (esp_netif_get_ip_info(netif_, &info) != ESP_OK) return {};

    char address[16] = {};
    std::snprintf(address, sizeof(address), IPSTR, IP2STR(&info.ip));
    return address;
}
