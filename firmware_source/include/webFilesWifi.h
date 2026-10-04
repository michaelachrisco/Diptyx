#pragma once

#include <string>

#include "esp_err.h"
#include "esp_netif.h"

class WebFilesWifi {
public:
    ~WebFilesWifi();

    esp_err_t start();
    void stop();

    const std::string& ssid() const { return ssid_; }
    const std::string& password() const { return password_; }
    std::string ipAddress() const;

private:
    bool eventLoopCreatedByUs_ = false;
    bool netifInitializedByUs_ = false;
    bool wifiInitialized_ = false;
    bool wifiStarted_ = false;
    esp_netif_t* netif_ = nullptr;
    std::string ssid_;
    std::string password_;
};
